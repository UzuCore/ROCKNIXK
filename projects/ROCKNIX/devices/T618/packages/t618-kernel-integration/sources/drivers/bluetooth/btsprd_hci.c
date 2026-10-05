// SPDX-License-Identifier: GPL-2.0-only
/*
 * Unisoc Marlin3-Lite (SC2355) Bluetooth HCI driver over the sprdwcn SDIO bus.
 *
 * Forked from btsprdsipc.c (Copyright (C) 2024 Otto Pflüger): the hci_dev
 * setup, the H4 RX reassembly (h4_recv_buf + sprd_recv_pkts) and the Unisoc
 * pskey/RF/enable vendor-command init sequence are kept verbatim. Only the
 * transport is swapped from rpmsg/SIPC (the SoC-integrated chip) to the
 * sprdwcn SDIO mchn bus (the external Marlin3-Lite combo, the same bus the
 * Wi-Fi driver rides). This mirrors the Wi-Fi bring-up, where the mainline
 * core spoke SIPC and we wrote the SDIO backend.
 *
 * The pskey/RF parameters are parsed in-kernel from the vendor text INI
 * (btsprd_ini.c) instead of a pre-packed .bin, for parity with the Wi-Fi
 * config path (which chose an in-kernel INI parser to avoid an offline
 * encoder). The parsed pskey blob has the BD address at offset 20, which
 * btsprdsdio_set_bdaddr() overwrites before sending, exactly as upstream.
 *
 * Copyright (C) 2026
 */

#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/random.h>
#include <linux/semaphore.h>
#include <linux/slab.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include <misc/marlin_platform.h>
#include <misc/wcn_bus.h>

#include "hci_uart.h"
#include "btsprd_ini.h"

/* Channels on the sprdwcn bus (vendor tty-sdio tty.h: BT_TX 3, BT_RX 17). */
#define BT_TX_CHANNEL		3
#define BT_RX_CHANNEL		17
#define BT_TX_POOL_SIZE		64
#define BT_RX_POOL_SIZE		1
/* sdiohal reserves a public-header word at the front of every buffer. */
#define BT_SDIO_HEAD_LEN	SDIOHAL_PUB_HEAD_RSV

#define BT_PSKEY_INI		"sprd/bt_configure_pskey.ini"
#define BT_RF_INI		"sprd/bt_configure_rf.ini"
#define BT_PSKEY_MAX		160
#define BT_RF_MAX		252

struct btsprdsdio {
	struct hci_dev *hdev;
	struct hci_uart hu;		/* only .hdev used, by h4_recv_buf */
	struct sk_buff *rx_skb;
	struct semaphore tx_sem;	/* gates the TX mbuf pool */
	size_t rf_cfg_len;
	u8 rf_cfg[BT_RF_MAX];
	size_t pskey_cfg_len;
	u8 pskey_cfg[BT_PSKEY_MAX];
};

/* Single instance: the mchn pop_link callbacks carry no context pointer, and
 * there is only ever one WCN combo chip (same idiom as the Wi-Fi backend).
 */
static struct btsprdsdio *btsprdsdio_dev;

static const struct h4_recv_pkt sprd_recv_pkts[] = {
	{ H4_RECV_ACL,      .recv = hci_recv_frame },
	{ H4_RECV_SCO,      .recv = hci_recv_frame },
	{ H4_RECV_EVENT,    .recv = hci_recv_frame },
};

/*
 * RX: sdiohal hands us a list of buffers, each holding one packet prefixed by
 * the 4-byte public header. Feed the payload to h4_recv_buf (which reassembles
 * across buffer boundaries) and return the buffers to sdiohal. Runs on the
 * sdiohal RX thread (process context).
 */
static int btsprdsdio_rx_cb(int chn, struct mbuf_t *head, struct mbuf_t *tail,
			    int num)
{
	struct btsprdsdio *bts = btsprdsdio_dev;
	struct mbuf_t *pos = head;
	struct bus_puh_t *puh;
	u16 len;
	int i;

	if (!bts)
		goto out;

	for (i = 0; i < num && pos; i++, pos = pos->next) {
		if (!pos->buf)
			continue;

		puh = (struct bus_puh_t *)pos->buf;
		len = puh->len;
		/* sdiohal folds a trailing 2-byte checksum into puh->len */
		if (puh->check_sum && len >= 2)
			len -= 2;
		if (!len)
			continue;

		bts->rx_skb = h4_recv_buf(&bts->hu, bts->rx_skb,
					  pos->buf + BT_SDIO_HEAD_LEN, len,
					  sprd_recv_pkts,
					  ARRAY_SIZE(sprd_recv_pkts));
		if (IS_ERR(bts->rx_skb)) {
			bt_dev_err(bts->hdev, "frame reassembly failed (%ld)",
				   PTR_ERR(bts->rx_skb));
			bts->rx_skb = NULL;
			continue;
		}
		bts->hdev->stat.byte_rx += len;
	}

out:
	sprdwcn_bus_push_list(chn, head, tail, num);
	return 0;
}

/* TX completion: sdiohal is done with the buffers, free them and release the
 * pool slot taken in btsprdsdio_send().
 */
static int btsprdsdio_tx_cb(int chn, struct mbuf_t *head, struct mbuf_t *tail,
			    int num)
{
	struct btsprdsdio *bts = btsprdsdio_dev;
	struct mbuf_t *pos = head;
	int i;

	for (i = 0; i < num && pos; i++, pos = pos->next) {
		kfree(pos->buf);
		pos->buf = NULL;
	}

	if (sprdwcn_bus_list_free(chn, head, tail, num) == 0 && bts)
		up(&bts->tx_sem);

	return 0;
}

static int btsprdsdio_send(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btsprdsdio *bts = hci_get_drvdata(hdev);
	struct mbuf_t *head = NULL, *tail = NULL;
	unsigned char *block;
	int num = 1, ret;
	u16 count;

	/* Prepend the H4 packet-type byte; BlueZ hands us the bare packet. */
	memcpy(skb_push(skb, 1), &hci_skb_pkt_type(skb), 1);
	count = skb->len;

	/*
	 * Leave BT_SDIO_HEAD_LEN of headroom for the public header, which
	 * sdiohal fills from the channel -- but it does not touch the
	 * check_sum bit, so zero the header region first (a stray check_sum=1
	 * makes the chip treat the last two payload bytes as a checksum).
	 */
	block = kmalloc(count + BT_SDIO_HEAD_LEN, GFP_KERNEL);
	if (!block) {
		hdev->stat.err_tx++;
		return -ENOMEM;
	}
	memset(block, 0, BT_SDIO_HEAD_LEN);
	memcpy(block + BT_SDIO_HEAD_LEN, skb->data, count);

	down(&bts->tx_sem);
	ret = sprdwcn_bus_list_alloc(BT_TX_CHANNEL, &head, &tail, &num);
	if (ret || !head || !tail) {
		up(&bts->tx_sem);
		kfree(block);
		hdev->stat.err_tx++;
		return -ENOMEM;
	}

	head->buf = block;
	head->len = count;
	head->next = NULL;

	ret = sprdwcn_bus_push_list(BT_TX_CHANNEL, head, tail, num);
	if (ret) {
		head->buf = NULL;
		kfree(block);
		sprdwcn_bus_list_free(BT_TX_CHANNEL, head, tail, num);
		up(&bts->tx_sem);
		hdev->stat.err_tx++;
		return -EBUSY;
	}

	hdev->stat.byte_tx += count;
	switch (hci_skb_pkt_type(skb)) {
	case HCI_COMMAND_PKT:
		hdev->stat.cmd_tx++;
		break;
	case HCI_ACLDATA_PKT:
		hdev->stat.acl_tx++;
		break;
	case HCI_SCODATA_PKT:
		hdev->stat.sco_tx++;
		break;
	}

	kfree_skb(skb);
	return 0;
}

static int btsprdsdio_open(struct hci_dev *hdev)
{
	int ret;

	/* Power the BT subsystem of the WCN combo. Refcounted per subsys, so
	 * this is independent of the Wi-Fi client holding the chip up.
	 */
	ret = start_marlin(MARLIN_BLUETOOTH);
	if (ret)
		bt_dev_err(hdev, "start_marlin(BT) failed (%d)", ret);

	return ret;
}

static int btsprdsdio_close(struct hci_dev *hdev)
{
	struct btsprdsdio *bts = hci_get_drvdata(hdev);

	stop_marlin(MARLIN_BLUETOOTH);

	kfree_skb(bts->rx_skb);
	bts->rx_skb = NULL;

	return 0;
}

static int btsprdsdio_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *bdaddr)
{
	const u8 enable_cmd[] = { 0, 9, 1 };
	struct btsprdsdio *bts = hci_get_drvdata(hdev);
	struct sk_buff *skb;

	/* This must always be the first command sent to the controller in
	 * order to actually initialize the BDADDR, so all initialization is
	 * done here and not in setup(). The BD address sits at offset 20 in
	 * the pskey blob (device_class[4] + feature_set[16]).
	 */
	memcpy(bts->pskey_cfg + 20, bdaddr, 6);
	skb = __hci_cmd_sync(hdev, 0xfca0, bts->pskey_cfg_len, bts->pskey_cfg,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb))
		goto err;
	kfree_skb(skb);

	skb = __hci_cmd_sync(hdev, 0xfca2, bts->rf_cfg_len, bts->rf_cfg,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb))
		goto err;
	kfree_skb(skb);

	skb = __hci_cmd_sync(hdev, 0xfca1, sizeof(enable_cmd), enable_cmd,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb))
		goto err;
	kfree_skb(skb);

	skb = __hci_cmd_sync(hdev, HCI_OP_RESET, 0, NULL, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb))
		goto err;
	kfree_skb(skb);

	return 0;

err:
	bt_dev_err(hdev, "initialization sequence failed (%ld)", PTR_ERR(skb));
	return PTR_ERR(skb);
}

static struct mchn_ops_t bt_rx_ops = {
	.channel = BT_RX_CHANNEL,
	.hif_type = HW_TYPE_SDIO,
	.inout = 0,
	.pool_size = BT_RX_POOL_SIZE,
	.pop_link = btsprdsdio_rx_cb,
};

static struct mchn_ops_t bt_tx_ops = {
	.channel = BT_TX_CHANNEL,
	.hif_type = HW_TYPE_SDIO,
	.inout = 1,
	.pool_size = BT_TX_POOL_SIZE,
	.pop_link = btsprdsdio_tx_cb,
};

static int btsprdsdio_load_cfg(struct device *dev, const char *name,
			       u8 *out, size_t out_max, size_t *out_len)
{
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, name, dev);
	if (ret < 0) {
		dev_err(dev, "firmware request for %s failed (%d)\n", name, ret);
		return ret;
	}

	ret = btsprd_ini_pack((const char *)fw->data, fw->size, out, out_max);
	release_firmware(fw);
	if (ret < 0) {
		dev_err(dev, "failed to parse %s (%d)\n", name, ret);
		return ret;
	}

	*out_len = ret;
	return 0;
}

/* Read the SoC's 64-bit die UID from the two efuse nvmem cells (uid-end@58
 * and uid-start@5c, contiguous). Propagates -EPROBE_DEFER if the nvmem
 * provider isn't ready yet.
 */
static int btsprdsdio_read_uid(struct device *dev, u8 uid[8])
{
	static const char * const names[2] = { "uid-end", "uid-start" };
	struct nvmem_cell *cell;
	size_t len;
	void *buf;
	int i;

	for (i = 0; i < 2; i++) {
		cell = nvmem_cell_get(dev, names[i]);
		if (IS_ERR(cell))
			return PTR_ERR(cell);

		buf = nvmem_cell_read(cell, &len);
		nvmem_cell_put(cell);
		if (IS_ERR(buf))
			return PTR_ERR(buf);
		if (len < 4) {
			kfree(buf);
			return -EINVAL;
		}

		memcpy(uid + i * 4, buf, 4);
		kfree(buf);
	}

	return 0;
}

/*
 * Give hci0 a valid public BD address. Priority: an explicit DT
 * "local-bd-address" override, then a stable per-unit address derived from
 * the die UID, then a random locally-administered address as a last resort.
 * The result is stamped into hdev->public_addr; combined with
 * HCI_QUIRK_INVALID_BDADDR this makes the core call set_bdaddr() during setup
 * (running the pskey init) and marks the controller configured.
 */
static int btsprdsdio_setup_bdaddr(struct hci_dev *hdev, struct device *dev)
{
	bdaddr_t ba;
	u8 uid[8];
	int ret;

	if (!device_property_read_u8_array(dev, "local-bd-address",
					   (u8 *)&ba, sizeof(ba)) &&
	    bacmp(&ba, BDADDR_ANY)) {
		bacpy(&hdev->public_addr, &ba);
		return 0;
	}

	ret = btsprdsdio_read_uid(dev, uid);
	if (ret == -EPROBE_DEFER)
		return ret;
	if (ret) {
		bt_dev_warn(hdev, "chip UID unavailable (%d), using random BD address",
			    ret);
		get_random_bytes(&ba, sizeof(ba));
	} else {
		/* fold all 8 UID bytes into the 6-byte address */
		ba.b[0] = uid[0] ^ uid[6];
		ba.b[1] = uid[1] ^ uid[7];
		ba.b[2] = uid[2];
		ba.b[3] = uid[3];
		ba.b[4] = uid[4];
		ba.b[5] = uid[5];
	}

	/* force a locally-administered unicast address (MSB bit1=1, bit0=0) */
	ba.b[5] = (ba.b[5] & 0xfe) | 0x02;
	bacpy(&hdev->public_addr, &ba);

	return 0;
}

static int btsprdsdio_probe(struct platform_device *pdev)
{
	struct btsprdsdio *bts;
	struct hci_dev *hdev;
	const char *pskey_file = BT_PSKEY_INI;
	const char *rf_file = BT_RF_INI;
	int ret;

	bts = devm_kzalloc(&pdev->dev, sizeof(*bts), GFP_KERNEL);
	if (!bts)
		return -ENOMEM;

	of_property_read_string(pdev->dev.of_node,
				"sprd,bt-pskey-file-name", &pskey_file);
	of_property_read_string(pdev->dev.of_node,
				"sprd,bt-rf-file-name", &rf_file);

	ret = btsprdsdio_load_cfg(&pdev->dev, pskey_file, bts->pskey_cfg,
				  BT_PSKEY_MAX, &bts->pskey_cfg_len);
	if (ret)
		return ret;

	ret = btsprdsdio_load_cfg(&pdev->dev, rf_file, bts->rf_cfg,
				  BT_RF_MAX, &bts->rf_cfg_len);
	if (ret)
		return ret;

	/*
	 * The vendor feature_set advertises LMP Park (feature page 0, byte 1,
	 * bit 0), but the SC2355 firmware rejects a default link policy that
	 * includes park mode: BlueZ builds WRITE_DEF_LINK_POLICY from the
	 * advertised features and the controller then fails it with 0x12
	 * "Invalid HCI Command Parameters", aborting hci0 bring-up. Park mode
	 * has been deprecated since BT 5.0 and no modern stack uses it, so
	 * clear the bit here. feature_set starts at pskey offset 4 (after
	 * device_class[4]); park is in byte 1, i.e. offset 5.
	 */
	if (bts->pskey_cfg_len > 5)
		bts->pskey_cfg[5] &= ~0x01;

	sema_init(&bts->tx_sem, BT_TX_POOL_SIZE - 1);

	hdev = hci_alloc_dev();
	if (!hdev)
		return -ENOMEM;

	hci_set_drvdata(hdev, bts);
	bts->hdev = hdev;
	bts->hu.hdev = hdev;
	SET_HCIDEV_DEV(hdev, &pdev->dev);

	hdev->manufacturer = 1855;
	hdev->bus = HCI_SDIO;
	hdev->open = btsprdsdio_open;
	hdev->close = btsprdsdio_close;
	hdev->send = btsprdsdio_send;
	hdev->set_bdaddr = btsprdsdio_set_bdaddr;

	/* We stamp a valid public_addr below; this quirk makes the core call
	 * set_bdaddr() (running the pskey init) and mark hci0 configured.
	 */
	hci_set_quirk(hdev, HCI_QUIRK_INVALID_BDADDR);

	ret = btsprdsdio_setup_bdaddr(hdev, &pdev->dev);
	if (ret)
		goto free_dev;

	btsprdsdio_dev = bts;

	ret = sprdwcn_bus_chn_init(&bt_rx_ops);
	if (ret)
		goto free_dev;
	ret = sprdwcn_bus_chn_init(&bt_tx_ops);
	if (ret)
		goto rx_deinit;

	ret = hci_register_dev(hdev);
	if (ret < 0)
		goto tx_deinit;

	platform_set_drvdata(pdev, bts);
	dev_info(&pdev->dev,
		 "registered %s (pskey %zu B, rf %zu B, tx chn %d, rx chn %d)\n",
		 hdev->name, bts->pskey_cfg_len, bts->rf_cfg_len,
		 BT_TX_CHANNEL, BT_RX_CHANNEL);
	return 0;

tx_deinit:
	sprdwcn_bus_chn_deinit(&bt_tx_ops);
rx_deinit:
	sprdwcn_bus_chn_deinit(&bt_rx_ops);
free_dev:
	btsprdsdio_dev = NULL;
	hci_free_dev(hdev);
	return ret;
}

static void btsprdsdio_remove(struct platform_device *pdev)
{
	struct btsprdsdio *bts = platform_get_drvdata(pdev);

	hci_unregister_dev(bts->hdev);
	sprdwcn_bus_chn_deinit(&bt_tx_ops);
	sprdwcn_bus_chn_deinit(&bt_rx_ops);
	hci_free_dev(bts->hdev);
	btsprdsdio_dev = NULL;
}

static const struct of_device_id btsprdsdio_of_match[] = {
	{ .compatible = "sprd,mtty", },
	{ },
};
MODULE_DEVICE_TABLE(of, btsprdsdio_of_match);

static struct platform_driver btsprdsdio_driver = {
	.probe = btsprdsdio_probe,
	.remove = btsprdsdio_remove,
	.driver = {
		.name = "btsprdsdio",
		.of_match_table = btsprdsdio_of_match,
	},
};

module_platform_driver(btsprdsdio_driver);

MODULE_DESCRIPTION("Unisoc Marlin3-Lite SDIO HCI driver");
MODULE_LICENSE("GPL v2");
