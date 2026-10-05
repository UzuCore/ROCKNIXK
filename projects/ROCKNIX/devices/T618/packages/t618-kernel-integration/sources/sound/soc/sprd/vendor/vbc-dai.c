#include <linux/overflow.h>
/*
 * SPRD SoC VBC -- SpreadTrum SOC for VBC DAI function.
 *
 * Copyright (C) 2015 SpreadTrum Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include "sprd-asoc-debug.h"

#define pr_fmt(fmt) pr_sprd_fmt(" VBC ") "%s: %d:"fmt, __func__, __LINE__

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <sound/core.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

#include "audio-sipc.h"
#include "mcdt_hw.h"
#include "sprd-asoc-card-utils.h"
#include "sprd-asoc-common.h"
#include "sprd-dmaengine-pcm.h"
#include "sprd-platform-pcm-routing.h"
#include "sprd-string.h"
#include "vbc-phy-v4.h"
#include "audio_mem.h"

/*
 * DAI-driver .aif_name was dropped upstream; the vendor names follow a
 * strict "<x>_DAI_<y>" -> "<x>_IF_<y>" pattern, so derive the AIF widget
 * name from the stream name instead.
 */
static const char *sprd_stream_aif_name(struct device *dev, const char *stream)
{
	const char *p = strstr(stream, "_DAI_");

	if (!p)
		return NULL;
	return devm_kasprintf(dev, GFP_KERNEL, "%.*s_IF_%s",
			      (int)(p - stream), stream, p + strlen("_DAI_"));
}

static int sprd_dai_vbc_probe(struct snd_soc_dai *dai);


static int normal_suspend(struct snd_soc_dai *dai);
static int normal_resume(struct snd_soc_dai *dai);


/* for vbc define here for pcm define in pcm driver */
#define sprd_is_normal_playback(cpu_dai_id, stream) \
	((((cpu_dai_id) == BE_DAI_ID_NORMAL_AP01_CODEC) ||\
	  ((cpu_dai_id) == BE_DAI_ID_NORMAL_AP01_USB)) && \
	 (stream) == SNDRV_PCM_STREAM_PLAYBACK)


static struct aud_pm_vbc *pm_vbc;
static struct aud_pm_vbc *aud_pm_vbc_get(void);
#define TO_STRING(e) #e

static const char *stream_to_str(int stream)
{
	return (stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		"playback" : "capture";
}

static const char *vbc_data_fmt_to_str(int data_fmt)
{
	const char *str_data_fmt;

	switch (data_fmt) {
	case VBC_DAT_H24:
		str_data_fmt = TO_STRING(VBC_DAT_H24);
		break;
	case VBC_DAT_L24:
		str_data_fmt = TO_STRING(VBC_DAT_L24);
		break;
	case VBC_DAT_H16:
		str_data_fmt = TO_STRING(VBC_DAT_H16);
		break;
	case VBC_DAT_L16:
		str_data_fmt = TO_STRING(VBC_DAT_L16);
		break;
	default:
		str_data_fmt = "";
		break;
	}

	return str_data_fmt;
}

static const char *dai_id_to_str(int dai_id)
{
	const char * const dai_id_str[BE_DAI_ID_MAX] = {
		[BE_DAI_ID_NORMAL_AP01_CODEC] =
			TO_STRING(BE_DAI_ID_NORMAL_AP01_CODEC),
		[BE_DAI_ID_NORMAL_AP23_CODEC] =
			TO_STRING(BE_DAI_ID_NORMAL_AP23_CODEC),
		[BE_DAI_ID_CAPTURE_DSP_CODEC] =
			TO_STRING(BE_DAI_ID_CAPTURE_DSP_CODEC),
		[BE_DAI_ID_FAST_P_CODEC] = TO_STRING(BE_DAI_ID_FAST_P_CODEC),
		[BE_DAI_ID_FAST_P_CODEC_MM] = TO_STRING(BE_DAI_ID_FAST_P_CODEC_MM),
		[BE_DAI_ID_OFFLOAD_CODEC] = TO_STRING(BE_DAI_ID_OFFLOAD_CODEC),
		[BE_DAI_ID_VOICE_CODEC] = TO_STRING(BE_DAI_ID_VOICE_CODEC),
		[BE_DAI_ID_VOIP_CODEC] = TO_STRING(BE_DAI_ID_VOIP_CODEC),
		[BE_DAI_ID_FM_CODEC] = TO_STRING(BE_DAI_ID_FM_CODEC),
		[BE_DAI_ID_LOOP_CODEC] = TO_STRING(BE_DAI_ID_LOOP_CODEC),
		[BE_DAI_ID_NORMAL_AP01_USB] =
			TO_STRING(BE_DAI_ID_NORMAL_AP01_USB),
		[BE_DAI_ID_NORMAL_AP23_USB] =
			TO_STRING(BE_DAI_ID_NORMAL_AP23_USB),
		[BE_DAI_ID_CAPTURE_DSP_USB] =
			TO_STRING(BE_DAI_ID_CAPTURE_DSP_USB),
		[BE_DAI_ID_FAST_P_USB] = TO_STRING(BE_DAI_ID_FAST_P_USB),
		[BE_DAI_ID_OFFLOAD_USB] = TO_STRING(BE_DAI_ID_OFFLOAD_USB),
		[BE_DAI_ID_VOICE_USB] = TO_STRING(BE_DAI_ID_VOICE_USB),
		[BE_DAI_ID_VOIP_USB] = TO_STRING(BE_DAI_ID_VOIP_USB),
		[BE_DAI_ID_FM_USB] = TO_STRING(BE_DAI_ID_FM_USB),
		[BE_DAI_ID_LOOP_USB] = TO_STRING(BE_DAI_ID_LOOP_USB),
		[BE_DAI_ID_OFFLOAD_A2DP] = TO_STRING(BE_DAI_ID_OFFLOAD_A2DP),
		[BE_DAI_ID_PCM_A2DP] = TO_STRING(BE_DAI_ID_PCM_A2DP),
		[BE_DAI_ID_VOICE_BT] = TO_STRING(BE_DAI_ID_VOICE_BT),
		[BE_DAI_ID_VOIP_BT] = TO_STRING(BE_DAI_ID_VOIP_BT),
		[BE_DAI_ID_LOOP_BT] = TO_STRING(BE_DAI_ID_LOOP_BT),
		[BE_DAI_ID_CAPTURE_BT] = TO_STRING(BE_DAI_ID_CAPTURE_BT),
		[BE_DAI_ID_VOICE_CAPTURE] = TO_STRING(BE_DAI_ID_VOICE_CAPTURE),
		[BE_DAI_ID_FM_CAPTURE] = TO_STRING(BE_DAI_ID_FM_CAPTURE),
		[BE_DAI_ID_FM_CAPTURE_DSP] =
			TO_STRING(BE_DAI_ID_FM_CAPTURE_DSP),
		[BE_DAI_ID_CAPTURE_DSP_BTSCO] =
			TO_STRING(BE_DAI_ID_CAPTURE_DSP_BTSCO),
		[BE_DAI_ID_FM_DSP_CODEC] = TO_STRING(BE_DAI_ID_FM_DSP_CODEC),
		[BE_DAI_ID_FM_DSP_USB] = TO_STRING(BE_DAI_ID_FM_DSP_USB),
		[BE_DAI_ID_DUMP] = TO_STRING(BE_DAI_ID_DUMP),
		[BE_DAI_ID_FAST_P_BTSCO] = TO_STRING(BE_DAI_ID_FAST_P_BTSCO),
		[BE_DAI_ID_NORMAL_AP01_P_BTSCO] =
			TO_STRING(BE_DAI_ID_NORMAL_AP01_P_BTSCO),
		[BE_DAI_ID_NORMAL_AP01_P_HIFI] =
			TO_STRING(BE_DAI_ID_NORMAL_AP01_P_HIFI),
		[BE_DAI_ID_NORMAL_AP23_HIFI] =
			TO_STRING(BE_DAI_ID_NORMAL_AP23_HIFI),
		[BE_DAI_ID_FAST_P_HIFI] = TO_STRING(BE_DAI_ID_FAST_P_HIFI),
		[BE_DAI_ID_OFFLOAD_HIFI] = TO_STRING(BE_DAI_ID_OFFLOAD_HIFI),
		[BE_DAI_ID_VOICE_HIFI] = TO_STRING(BE_DAI_ID_VOICE_HIFI),
		[BE_DAI_ID_VOIP_HIFI] = TO_STRING(BE_DAI_ID_VOIP_HIFI),
		[BE_DAI_ID_FM_HIFI] = TO_STRING(BE_DAI_ID_FM_HIFI),
		[BE_DAI_ID_LOOP_HIFI] = TO_STRING(BE_DAI_ID_LOOP_HIFI),
		[BE_DAI_ID_FM_DSP_HIFI] = TO_STRING(BE_DAI_ID_FM_DSP_HIFI),
		[BE_DAI_ID_HFP] = TO_STRING(BE_DAI_ID_HFP),
		[BE_DAI_ID_RECOGNISE_CAPTURE] =
			TO_STRING(BE_DAI_ID_RECOGNISE_CAPTURE),
		[BE_DAI_ID_VOICE_PCM_P] = TO_STRING(BE_DAI_ID_VOICE_PCM_P),

		[BE_DAI_ID_NORMAL_AP01_P_SMTPA] =
			TO_STRING(BE_DAI_ID_NORMAL_AP01_P_SMTPA),
		[BE_DAI_ID_NORMAL_AP23_SMTPA] =
			TO_STRING(BE_DAI_ID_NORMAL_AP23_SMTPA),
		[BE_DAI_ID_FAST_P_SMTPA] = TO_STRING(BE_DAI_ID_FAST_P_SMTPA),
		[BE_DAI_ID_FAST_P_SMART_AMP] = TO_STRING(BE_DAI_ID_FAST_P_SMART_AMP),
		[BE_DAI_ID_OFFLOAD_SMTPA] = TO_STRING(BE_DAI_ID_OFFLOAD_SMTPA),
		[BE_DAI_ID_VOICE_SMTPA] = TO_STRING(BE_DAI_ID_VOICE_SMTPA),
		[BE_DAI_ID_VOIP_SMTPA] = TO_STRING(BE_DAI_ID_VOIP_SMTPA),
		[BE_DAI_ID_FM_SMTPA] = TO_STRING(BE_DAI_ID_FM_SMTPA),
		[BE_DAI_ID_LOOP_SMTPA] = TO_STRING(BE_DAI_ID_LOOP_SMTPA),
		[BE_DAI_ID_FM_DSP_SMTPA] = TO_STRING(BE_DAI_ID_FM_DSP_SMTPA),
		[BE_DAI_ID_HIFI_P] = TO_STRING(BE_DAI_ID_HIFI_P),
		[BE_DAI_ID_HIFI_FAST_P] = TO_STRING(BE_DAI_ID_HIFI_FAST_P),
	};

	if (dai_id >= BE_DAI_ID_MAX) {
		pr_err("invalid dai_id %d\n", dai_id);
		return "";
	}
	if (!dai_id_str[dai_id]) {
		pr_err("null dai_id string dai_id=%d\n", dai_id);
		return "";
	}

	return dai_id_str[dai_id];
}

static const char *scene_id_to_str(int scene_id)
{
	const char *scene_id_str[VBC_DAI_ID_MAX] = {
		[VBC_DAI_ID_NORMAL_AP01] = TO_STRING(VBC_DAI_ID_NORMAL_AP01),
		[VBC_DAI_ID_NORMAL_AP23] = TO_STRING(VBC_DAI_ID_NORMAL_AP23),
		[VBC_DAI_ID_CAPTURE_DSP] = TO_STRING(VBC_DAI_ID_CAPTURE_DSP),
		[VBC_DAI_ID_FAST_P] = TO_STRING(VBC_DAI_ID_FAST_P),
		[VBC_DAI_ID_FAST_P_SMART_AMP] = TO_STRING(VBC_DAI_ID_FAST_P_SMART_AMP),
		[VBC_DAI_ID_OFFLOAD] = TO_STRING(VBC_DAI_ID_OFFLOAD),
		[VBC_DAI_ID_VOICE] = TO_STRING(VBC_DAI_ID_VOICE),
		[VBC_DAI_ID_VOIP] = TO_STRING(VBC_DAI_ID_VOIP),
		[VBC_DAI_ID_FM] = TO_STRING(VBC_DAI_ID_FM),
		[VBC_DAI_ID_FM_CAPTURE_AP] =
			TO_STRING(VBC_DAI_ID_FM_CAPTURE_AP),
		[VBC_DAI_ID_VOICE_CAPTURE] =
			TO_STRING(VBC_DAI_ID_VOICE_CAPTURE),
		[VBC_DAI_ID_LOOP] = TO_STRING(VBC_DAI_ID_LOOP),
		[VBC_DAI_ID_PCM_A2DP] = TO_STRING(VBC_DAI_ID_PCM_A2DP),
		[VBC_DAI_ID_OFFLOAD_A2DP] = TO_STRING(VBC_DAI_ID_OFFLOAD_A2DP),
		[VBC_DAI_ID_BT_CAPTURE_AP] =
			TO_STRING(VBC_DAI_ID_BT_CAPTURE_AP),
		[VBC_DAI_ID_FM_CAPTURE_DSP] =
			TO_STRING(VBC_DAI_ID_FM_CAPTURE_DSP),
		[VBC_DAI_ID_BT_SCO_CAPTURE_DSP] =
			TO_STRING(VBC_DAI_ID_BT_SCO_CAPTURE_DSP),
		[VBC_DAI_ID_FM_DSP] = TO_STRING(VBC_DAI_ID_FM_DSP),
		[VBC_DAI_ID_HFP] = TO_STRING(VBC_DAI_ID_HFP),
		[VBC_DAI_ID_RECOGNISE_CAPTURE] =
			TO_STRING(VBC_DAI_ID_RECOGNISE_CAPTURE),
		[VBC_DAI_ID_VOICE_PCM_P] = TO_STRING(VBC_DAI_ID_VOICE_PCM_P),
		[AUDCP_DAI_ID_HIFI] = TO_STRING(AUDCP_DAI_ID_HIFI),
		[AUDCP_DAI_ID_FAST] = TO_STRING(AUDCP_DAI_ID_FAST),
	};

	if (scene_id >= VBC_DAI_ID_MAX) {
		pr_err("invalid scene_id %d\n", scene_id);
		return "";
	}
	if (!scene_id_str[scene_id]) {
		pr_err("null scene_id string, scene_id=%d\n", scene_id);
		return "";
	}

	return scene_id_str[scene_id];

}

static int check_enable_ivs_smtpa(int scene_id, int stream,
	struct vbc_codec_priv *vbc_codec)
{
	int used_ivs;
	int enable;

	used_ivs = vbc_codec->is_use_ivs_smtpa;
	pr_debug("scene_id =%s stream =%s use ivs %s\n",
		scene_id_to_str(scene_id), stream_to_str(stream),
		used_ivs ? "true" : "false");

	if (!used_ivs)
		return 0;
	if (stream == SNDRV_PCM_STREAM_CAPTURE)
		return 0;

	switch (scene_id) {
	case VBC_DAI_ID_FAST_P:
	case VBC_DAI_ID_FAST_P_SMART_AMP:
	case VBC_DAI_ID_OFFLOAD:
		enable = 1;
		pr_info("scene %s enabled ivsense smartpa\n",
			scene_id_to_str(scene_id));
		break;
	default:
		enable = 0;
		break;
	}

	pr_debug("%s %s ivsence smartpa\n", scene_id_to_str(scene_id),
		enable ? "enabled" : "disabled");

	return enable;
}

static int get_ivsense_adc_id(void)
{
	return VBC_AD1;
}

//#include "vbc-codec.c"
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <sound/core.h>
#include <sound/pcm_params.h>
#include <linux/pinctrl/consumer.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

#define MDG_STP_MAX_VAL (0x1fff)
#define DG_MAX_VAL (0x7f)
#define SMTHDG_MAX_VAL (0xffff)
#define SMTHDG_STEP_MAX_VAL (0x1fff)
#define MIXERDG_MAX_VAL (0xffff)
#define MIXERDG_STP_MAX_VAL (0xffff)
#define OFFLOAD_DG_MAX (4096)
#define MAX_32_BIT (0xffffffff)
#define MAX_12_BIT (0xfff)
#define SRC_MAX_VAL (48000)

#define SPRD_VBC_ENUM(xreg, xmax, xtexts)\
	SOC_ENUM_SINGLE(xreg, 0, xmax, xtexts)

#undef sp_asoc_pr_dbg
#define sp_asoc_pr_dbg pr_info

static const char * const dsp_loopback_type_txt[] = {
	/* type 0, type 1, type 2 */
	"ADDA", "AD_ULDL_DA_PROCESS", "AD_UL_ENCODE_DECODE_DL_DA_PROCESS",

};

static const char * const enable_disable_txt[] = {
	"disable", "enable",
};

static const char * const mute_unmute_txt[] = {
	"mute", "unmute",
};


static const char * const ag_iis0_mode_txt[] = {
	"top_dac_iis", "pdm_top_iis0", "l5_top",
};

static const char * const ag_iis1_mode_txt[] = {
	"top_adc_iis0", "pdm_top_iis1", "l5_top",
};

static const char * const ag_iis2_mode_txt[] = {
	"l5_top_usb_iism", "top_adc_iis1", "pdm_iis2",
};

static const char * const ag_iis4_mode_txt[] = {
	"top_adc_iis1", "pdm_iis2", "l5_top",
};

static const char * const ag_iis0_mode_v2_txt[] = {
	"pad_top", "aud_4ad_iis0_da0", "pdm_top_iis0",
};

static const char * const ag_iis1_mode_v2_txt[] = {
	"pad_top", "aud_4ad_iis0_ad0", "pdm_top_iis1",
};

static const char * const ag_iis2_mode_v2_txt[] = {
	"pad_top", "aud_4ad_iis1_ad0", "pdm_top_iis2",
};

static const char * const ag_iis4_mode_v2_txt[] = {
	"pad_top", "aud_4ad_iis1_ad0", "pdm_top_iis2",
};

static const char * const ag_iis3_mode_v2_txt[] = {
	"pad_top", "aud_4ad_iis_da0",
};

static const char * const dsp_voice_capture_type_txt[] = {
	/* type 0, type 1, type 2 */
	"VOICE_CAPTURE_DOWNLINK", "VOICE_CAPTURE_UPLINK",
	"VOICE_CAPTURE_UPLINK_DOWNLINK",
};

static const char * const dsp_voice_pcm_play_mode_txt[] = {
	/* type 0, type 1 */
	"VOICE_PCM_PLAY_UPLINK_MIX", "VOICE_PCM_PLAY_UPLINK_ONLY",
};

static const struct soc_enum dsp_loopback_enum  =
SPRD_VBC_ENUM(SND_SOC_NOPM, 3, dsp_loopback_type_txt);

static const struct soc_enum vbc_ag_iis_ext_sel_enum[AG_IIS_MAX] = {
	SPRD_VBC_ENUM(AG_IIS0, 2, enable_disable_txt),
	SPRD_VBC_ENUM(AG_IIS1, 2, enable_disable_txt),
	SPRD_VBC_ENUM(AG_IIS2, 2, enable_disable_txt),
};

static const struct soc_enum vbc_ag_iis_ext_sel_enum_v1[AG_IIS_V1_MAX] = {
	SPRD_VBC_ENUM(AG_IIS0_V1, 3, ag_iis0_mode_txt),
	SPRD_VBC_ENUM(AG_IIS1_V1, 3, ag_iis1_mode_txt),
	SPRD_VBC_ENUM(AG_IIS2_V1, 3, ag_iis2_mode_txt),
	SPRD_VBC_ENUM(AG_IIS4_V1, 3, ag_iis4_mode_txt),
};

static const struct soc_enum vbc_ag_iis_ext_sel_enum_v2[AG_IIS_V2_MAX] = {
	SPRD_VBC_ENUM(AG_IIS0_V2, 3, ag_iis0_mode_v2_txt),
	SPRD_VBC_ENUM(AG_IIS1_V2, 3, ag_iis1_mode_v2_txt),
	SPRD_VBC_ENUM(AG_IIS2_V2, 3, ag_iis2_mode_v2_txt),
	SPRD_VBC_ENUM(AG_IIS4_V2, 3, ag_iis4_mode_v2_txt),
	SPRD_VBC_ENUM(AG_IIS3_V2, 2, ag_iis3_mode_v2_txt),
};

static const struct soc_enum vbc_dump_enum =
SPRD_VBC_ENUM(SND_SOC_NOPM, 2, enable_disable_txt);

static const struct soc_enum dsp_voice_capture_enum  =
SPRD_VBC_ENUM(SND_SOC_NOPM, 3, dsp_voice_capture_type_txt);

static const struct soc_enum dsp_voice_pcm_play_enum  =
SPRD_VBC_ENUM(SND_SOC_NOPM, 2, dsp_voice_pcm_play_mode_txt);

static const struct soc_enum dsp_aux_mic2_enum  =
SPRD_VBC_ENUM(SND_SOC_NOPM, 2, enable_disable_txt);

static const struct soc_enum dsp_smartamp_iv_exchange_enum  =
SPRD_VBC_ENUM(SND_SOC_NOPM, 2, enable_disable_txt);

static const struct soc_enum dsp_dac0_lr_exchange_enum  =
SPRD_VBC_ENUM(SND_SOC_NOPM, 2, enable_disable_txt);

static const char * const dsp_device_change_txt[DEVICE_TYPE_MAX] = {
	[TYPE_INIT] = TO_STRING(TYPE_INIT),
	[TYPE_RCV] = TO_STRING(TYPE_RCV),
	[TYPE_HP] = TO_STRING(TYPE_HP),
	[TYPE_SPK] = TO_STRING(TYPE_SPK),
	[TYPE_MIC] = TO_STRING(TYPE_MIC),
	[TYPE_BT] = TO_STRING(TYPE_BT),
	[TYPE_USB_HP] = TO_STRING(TYPE_USB_HP),
};

static const char * const dsp_custm_vol_change_txt[] = {
	"VOL_DOWN", "VOL_UP",
};

static const struct soc_enum dsp_custm_dev_change_enum  =
	SPRD_VBC_ENUM(SND_SOC_NOPM, DEVICE_TYPE_MAX, dsp_device_change_txt);

static const struct soc_enum dsp_system_dev_change_enum  =
	SPRD_VBC_ENUM(SND_SOC_NOPM, DEVICE_TYPE_MAX, dsp_device_change_txt);

static const struct soc_enum dsp_custm_vol_change_enum  =
	SPRD_VBC_ENUM(SND_SOC_NOPM, 2, dsp_custm_vol_change_txt);

static const char * const sprd_profile_name[] = {
	"audio_structure", "dsp_vbc", "cvs", "dsp_smartamp",
};

const char *vbc_get_profile_name(int profile_id)
{
	return sprd_profile_name[profile_id];
}

/********************************************************************
 * KCONTROL get/put define
 ********************************************************************/

/* MDG */
static const char *vbc_mdg_id2name(int id)
{
	const char * const vbc_mdg_name[VBC_MDG_MAX] = {
		[VBC_MDG_DAC0_DSP] = TO_STRING(VBC_MDG_DAC0_DSP),
		[VBC_MDG_DAC1_DSP] = TO_STRING(VBC_MDG_DAC1_DSP),
		[VBC_MDG_AP01] = TO_STRING(VBC_MDG_AP01),
		[VBC_MDG_AP23] = TO_STRING(VBC_MDG_AP23),
	};

	if (id >= VBC_MDG_MAX) {
		pr_err("invalid id %s %d\n", __func__, id);
		return "";
	}
	if (!vbc_mdg_name[id]) {
		pr_err("null string =%d\n", id);
		return "";
	}

	return vbc_mdg_name[id];
}

static int vbc_mdg_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int id = mc->shift;

	ucontrol->value.integer.value[0] = vbc_codec->mdg[id].mdg_mute;
	ucontrol->value.integer.value[1] = vbc_codec->mdg[id].mdg_step;

	return 0;
}

static int vbc_mdg_put(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	u32 val1, val2;
	int id = mc->shift;

	val1 = ucontrol->value.integer.value[0];
	val2 = ucontrol->value.integer.value[1];

	sp_asoc_pr_dbg("%s %s mute:%02d, step:%02d\n",
		       __func__, vbc_mdg_id2name(id), val1, val2);
	vbc_codec->mdg[id].mdg_id = id;
	vbc_codec->mdg[id].mdg_mute = val1;
	vbc_codec->mdg[id].mdg_step = val2;
	dsp_vbc_mdg_set(id, val1, val2);

	return 0;
}

/* SRC */
static const char *vbc_src_id2name(int id)
{
	const char * const vbc_src_name[VBC_SRC_MAX] = {
		[VBC_SRC_DAC0] = TO_STRING(VBC_SRC_DAC0),
		[VBC_SRC_DAC1] = TO_STRING(VBC_SRC_DAC1),
		[VBC_SRC_ADC0] = TO_STRING(VBC_SRC_ADC0),
		[VBC_SRC_ADC1] = TO_STRING(VBC_SRC_ADC1),
		[VBC_SRC_ADC2] = TO_STRING(VBC_SRC_ADC2),
		[VBC_SRC_ADC3] = TO_STRING(VBC_SRC_ADC3),
		[VBC_SRC_BT_DAC] = TO_STRING(VBC_SRC_BT_DAC),
		[VBC_SRC_BT_ADC] = TO_STRING(VBC_SRC_BT_ADC),
		[VBC_SRC_FM] = TO_STRING(VBC_SRC_FM),
	};

	if (id >= VBC_SRC_MAX) {
		pr_err("invalid id %s %d\n", __func__, id);
		return "";
	}
	if (!vbc_src_name[id]) {
		pr_err("null string =%d\n", id);
		return "";
	}

	return vbc_src_name[id];
}

static int vbc_src_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int id = mc->shift;

	ucontrol->value.integer.value[0] = vbc_codec->src_fs[id];

	return 0;
}

/* @ucontrol->val: 48000, 8000 ...... */
static int vbc_src_put(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	u32 val1;
	int id = mc->shift;

	val1 = ucontrol->value.integer.value[0];
	sp_asoc_pr_dbg("%s %s, src_fs=%d\n", __func__,
		       vbc_src_id2name(id), val1);
	vbc_codec->src_fs[id] = val1;
	dsp_vbc_src_set(id, val1);

	return 0;
}

/* DG */
static const char *vbc_dg_id2name(int id)
{
	const char * const vbc_dg_name[VBC_DG_MAX] = {
		[VBC_DG_DAC0] = TO_STRING(VBC_DG_DAC0),
		[VBC_DG_DAC1] = TO_STRING(VBC_DG_DAC1),
		[VBC_DG_ADC0] = TO_STRING(VBC_DG_ADC0),
		[VBC_DG_ADC1] = TO_STRING(VBC_DG_ADC1),
		[VBC_DG_ADC2] = TO_STRING(VBC_DG_ADC2),
		[VBC_DG_ADC3] = TO_STRING(VBC_DG_ADC3),
		[VBC_DG_FM] = TO_STRING(VBC_DG_FM),
		[VBC_DG_ST] = TO_STRING(VBC_DG_ST),
		[OFFLOAD_DG] = TO_STRING(OFFLOAD_DG),
	};

	if (id >= VBC_DG_MAX) {
		pr_err("invalid id %s %d\n", __func__, id);
		return "";
	}
	if (!vbc_dg_name[id]) {
		pr_err("null string =%d\n", id);
		return "";
	}

	return vbc_dg_name[id];
}

static int vbc_dg_get(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int id = mc->shift;

	ucontrol->value.integer.value[0] = vbc_codec->dg[id].dg_left;
	ucontrol->value.integer.value[1] = vbc_codec->dg[id].dg_right;

	return 0;
}

static int vbc_dg_put(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	u32 val1 = ucontrol->value.integer.value[0], val2 =
		ucontrol->value.integer.value[1];
	int id = mc->shift;

	if (id == OFFLOAD_DG &&
	    (val1 > OFFLOAD_DG_MAX || val2 > OFFLOAD_DG_MAX))
		return -EINVAL;

	vbc_codec->dg[id].dg_id = id;
	vbc_codec->dg[id].dg_left = val1;
	vbc_codec->dg[id].dg_right = val2;

	dsp_vbc_dg_set(id, val1, val2);
	sp_asoc_pr_dbg("%s %s l:%02d r:%02d\n",
		       __func__, vbc_dg_id2name(id), vbc_codec->dg[id].dg_left,
		       vbc_codec->dg[id].dg_right);

	return 0;
}

/* SMTHDG */
static const char *vbc_smthdg_id2name(int id)
{
	const char * const vbc_smthdg_name[VBC_SMTHDG_MAX] = {
		[VBC_SMTHDG_DAC0] = TO_STRING(VBC_SMTHDG_DAC0),
	};

	if (id >= VBC_SMTHDG_MAX) {
		pr_err("invalid id %s %d\n", __func__, id);
		return "";
	}
	if (!vbc_smthdg_name[id]) {
		pr_err("null string =%d\n", id);
		return "";
	}

	return vbc_smthdg_name[id];
}

static int vbc_smthdg_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int id = mc->shift;

	ucontrol->value.integer.value[0] = vbc_codec->smthdg[id].smthdg_left;
	ucontrol->value.integer.value[1] = vbc_codec->smthdg[id].smthdg_right;

	return 0;
}

static int vbc_smthdg_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	u32 val1, val2;
	int id = mc->shift;

	val1 = ucontrol->value.integer.value[0];
	val2 = ucontrol->value.integer.value[1];
	sp_asoc_pr_dbg("%s %s l:%02d, r:%02d\n",
		       __func__, vbc_smthdg_id2name(id), val1, val2);
	vbc_codec->smthdg[id].smthdg_id = id;
	vbc_codec->smthdg[id].smthdg_left = val1;
	vbc_codec->smthdg[id].smthdg_right = val2;

	dsp_vbc_smthdg_set(id, val1, val2);

	return 0;
}

static int vbc_smthdg_step_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int id = mc->shift;

	ucontrol->value.integer.value[0] = vbc_codec->smthdg_step[id].step;

	return 0;
}

static int vbc_smthdg_step_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	u32 val;
	int id = mc->shift;

	val = ucontrol->value.integer.value[0];
	sp_asoc_pr_dbg("%s %s %02d\n",
		       __func__, vbc_smthdg_id2name(id), val);
	vbc_codec->smthdg_step[id].smthdg_id = id;
	vbc_codec->smthdg_step[id].step = val;
	dsp_vbc_smthdg_step_set(id, val);

	return 0;
}

/* MIXERDG */
static const char *vbc_mixerdg_id2name(int id)
{
	const char * const vbc_mixerdg_name[VBC_MIXERDG_MAX] = {
		[VBC_MIXERDG_DAC0] = TO_STRING(VBC_MIXERDG_DAC0),
		[VBC_MIXERDG_DAC1] = TO_STRING(VBC_MIXERDG_DAC1),
	};

	if (id >= VBC_MIXERDG_MAX) {
		pr_err("invalid id %s %d\n", __func__, id);
		return "";
	}
	if (!vbc_mixerdg_name[id]) {
		pr_err("null string =%d\n", id);
		return "";
	}

	return vbc_mixerdg_name[id];
}

static int vbc_mixerdg_mainpath_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int id = mc->shift;

	ucontrol->value.integer.value[0] =
		vbc_codec->mixerdg[id].main_path.mixerdg_main_left;
	ucontrol->value.integer.value[1] =
		vbc_codec->mixerdg[id].main_path.mixerdg_main_right;

	return 0;
}

static int vbc_mixerdg_mainpath_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	u32 val1, val2;
	int id = mc->shift;

	val1 = ucontrol->value.integer.value[0];
	val2 = ucontrol->value.integer.value[1];
	sp_asoc_pr_dbg("%s %s: main_path l:%02d, r:%02d\n",
		       __func__, vbc_mixerdg_id2name(id), val1, val2);
	vbc_codec->mixerdg[id].main_path.mixerdg_id = id;
	vbc_codec->mixerdg[id].main_path.mixerdg_main_left = val1;
	vbc_codec->mixerdg[id].main_path.mixerdg_main_right = val2;
	dsp_vbc_mixerdg_mainpath_set(id, val1, val2);

	return 0;
}

static int vbc_mixerdg_mixpath_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int id = mc->shift;

	ucontrol->value.integer.value[0] =
		vbc_codec->mixerdg[id].mix_path.mixerdg_mix_left;
	ucontrol->value.integer.value[1] =
		vbc_codec->mixerdg[id].mix_path.mixerdg_mix_right;

	return 0;
}

static int vbc_mixerdg_mixpath_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	u32 val1, val2;
	int id = mc->shift;

	val1 = ucontrol->value.integer.value[0];
	val2 = ucontrol->value.integer.value[1];
	sp_asoc_pr_dbg("%s %s: mix_paht l:%02d, r:%02d\n",
		       __func__, vbc_mixerdg_id2name(id), val1, val2);
	vbc_codec->mixerdg[id].mix_path.mixerdg_id = id;
	vbc_codec->mixerdg[id].mix_path.mixerdg_mix_left = val1;
	vbc_codec->mixerdg[id].mix_path.mixerdg_mix_right = val2;
	dsp_vbc_mixerdg_mixpath_set(id, val1, val2);

	return 0;
}

static int vbc_mixerdg_step_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->mixerdg_step;

	return 0;
}

static int vbc_mixerdg_step_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	int val;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	val = ucontrol->value.integer.value[0];
	sp_asoc_pr_dbg("%s set %02d\n", __func__, val);
	vbc_codec->mixerdg_step = val;
	dsp_vbc_mixerdg_step_set(val);

	return 0;
}

/* MIXER */
static const char *vbc_mixer_id2name(int id)
{
	const char * const vbc_mixer_name[VBC_MIXER_MAX] = {
		[VBC_MIXER0_DAC0] = TO_STRING(VBC_MIXER0_DAC0),
		[VBC_MIXER1_DAC0] = TO_STRING(VBC_MIXER1_DAC0),
		[VBC_MIXER0_DAC1] = TO_STRING(VBC_MIXER0_DAC1),
		[VBC_MIXER_ST] = TO_STRING(VBC_MIXER_ST),
		[VBC_MIXER_FM] = TO_STRING(VBC_MIXER_FM),
	};

	if (id >= VBC_MIXER_MAX) {
		pr_err("invalid id %s %d\n", __func__, id);
		return "";
	}
	if (!vbc_mixer_name[id]) {
		pr_err("null string =%d\n", id);
		return "";
	}

	return vbc_mixer_name[id];
}

static const char * const mixer_ops_type_txt[MIXER_OPS_TYPE_MAX] = {
	[NOT_MIX] = TO_STRING(NOT_MIX),
	[INTERCHANGE] = TO_STRING(INTERCHANGE),
	[HALF_ADD] = TO_STRING(HALF_ADD),
	[HALF_SUB] = TO_STRING(HALF_SUB),
	[DATA_INV] = TO_STRING(DATA_INV),
	[INTERCHANGE_INV] = TO_STRING(INTERCHANGE_INV),
	[HALF_ADD_INV] = TO_STRING(HALF_ADD_INV),
	[HALF_SUB_INV] = TO_STRING(HALF_SUB_INV),
};

static const struct soc_enum vbc_mixer_enum[VBC_MIXER_MAX] = {
	SPRD_VBC_ENUM(VBC_MIXER0_DAC0, MIXER_OPS_TYPE_MAX, mixer_ops_type_txt),
	SPRD_VBC_ENUM(VBC_MIXER1_DAC0, MIXER_OPS_TYPE_MAX, mixer_ops_type_txt),
	SPRD_VBC_ENUM(VBC_MIXER0_DAC1, MIXER_OPS_TYPE_MAX, mixer_ops_type_txt),
	SPRD_VBC_ENUM(VBC_MIXER_ST, MIXER_OPS_TYPE_MAX, mixer_ops_type_txt),
	SPRD_VBC_ENUM(VBC_MIXER_FM, MIXER_OPS_TYPE_MAX, mixer_ops_type_txt),
};

static int vbc_get_mixer_ops(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	ucontrol->value.integer.value[0] = vbc_codec->mixer[id].type;

	return 0;
}

static int vbc_put_mixer_ops(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;
	int val;

	val = ucontrol->value.integer.value[0];
	if (val >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}

	sp_asoc_pr_dbg("%s %s -> %s\n", __func__,
		       vbc_mixer_id2name(id), texts->texts[val]);

	vbc_codec->mixer[id].mixer_id = id;
	vbc_codec->mixer[id].type = val;
	dsp_vbc_mixer_set(id, vbc_codec->mixer[id].type);

	return 0;
}

/* MUX ADC_SOURCE */
static const char * const adc_source_sel_txt[ADC_SOURCE_VAL_MAX] = {
	[ADC_SOURCE_IIS] = TO_STRING(ADC_SOURCE_IIS),
	[ADC_SOURCE_VBCIF] = TO_STRING(ADC_SOURCE_VBCIF),
};

static const struct soc_enum
vbc_mux_adc_source_enum[VBC_MUX_ADC_SOURCE_MAX] = {
	[VBC_MUX_ADC0_SOURCE] = SPRD_VBC_ENUM(VBC_MUX_ADC0_SOURCE,
		ADC_SOURCE_VAL_MAX, adc_source_sel_txt),
	[VBC_MUX_ADC1_SOURCE] = SPRD_VBC_ENUM(VBC_MUX_ADC0_SOURCE,
		ADC_SOURCE_VAL_MAX, adc_source_sel_txt),
	[VBC_MUX_ADC2_SOURCE] = SPRD_VBC_ENUM(VBC_MUX_ADC0_SOURCE,
		ADC_SOURCE_VAL_MAX, adc_source_sel_txt),
	[VBC_MUX_ADC3_SOURCE] = SPRD_VBC_ENUM(VBC_MUX_ADC0_SOURCE,
		ADC_SOURCE_VAL_MAX, adc_source_sel_txt),
};

/* SND_KCTL_TYPE_MAIN_MIC_PATH_FROM */
static const char * const vbc_mainmic_path_val_txt[MAINMIC_FROM_MAX] = {
	[MAINMIC_FROM_LEFT] = TO_STRING(MAINMIC_FROM_LEFT),
	[MAINMIC_FROM_RIGHT] = TO_STRING(MAINMIC_FROM_RIGHT),
};

static const struct soc_enum
vbc_mainmic_path_enum[MAINMIC_USED_MAINMIC_TYPE_MAX] = {
	[MAINMIC_USED_DSP_NORMAL_ADC] = SPRD_VBC_ENUM(
		MAINMIC_USED_DSP_NORMAL_ADC, MAINMIC_FROM_MAX,
		vbc_mainmic_path_val_txt),
	[MAINMIC_USED_DSP_REF_ADC] = SPRD_VBC_ENUM(MAINMIC_USED_DSP_REF_ADC,
		MAINMIC_FROM_MAX, vbc_mainmic_path_val_txt),
};

static const char *vbc_mux_adc_source_id2name(int id)
{
	const char * const vbc_mux_adc_source_name[VBC_MUX_ADC_SOURCE_MAX] = {
		[VBC_MUX_ADC0_SOURCE] = TO_STRING(VBC_MUX_ADC0_SOURCE),
		[VBC_MUX_ADC1_SOURCE] = TO_STRING(VBC_MUX_ADC1_SOURCE),
		[VBC_MUX_ADC2_SOURCE] = TO_STRING(VBC_MUX_ADC2_SOURCE),
		[VBC_MUX_ADC3_SOURCE] = TO_STRING(VBC_MUX_ADC3_SOURCE),
	};

	if (id >= VBC_MUX_ADC_SOURCE_MAX) {
		pr_err("invalid id %s %d\n", __func__, id);
		return "";
	}
	if (!vbc_mux_adc_source_name[id]) {
		pr_err("null string =%d\n", id);
		return "";
	}

	return vbc_mux_adc_source_name[id];
}

static int vbc_mux_adc_source_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	ucontrol->value.integer.value[0] = vbc_codec->mux_adc_source[id].val;

	return 0;
}

static int vbc_mux_adc_source_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;
	int val;

	val = ucontrol->value.integer.value[0];
	if (val >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}
	sp_asoc_pr_dbg("%s, %s to %s\n",
		__func__, vbc_mux_adc_source_id2name(id), texts->texts[val]);
	vbc_codec->mux_adc_source[id].id = id;
	vbc_codec->mux_adc_source[id].val = val;

	dsp_vbc_mux_adc_source_set(id, val);

	return 1;
}

/* MUX DAC_OUT */
static const char * const dac_out_sel_txt[DAC_OUT_FORM_MAX] = {
	[DAC_OUT_FROM_IIS] = TO_STRING(DAC_OUT_FROM_IIS),
	[DAC_OUT_FROM_VBCIF] = TO_STRING(DAC_OUT_FROM_VBCIF),
};

static const struct soc_enum
vbc_mux_dac_out_enum[VBC_MUX_DAC_OUT_MAX] = {
	[VBC_MUX_DAC0_OUT_SEL] = SPRD_VBC_ENUM(VBC_MUX_DAC0_OUT_SEL,
		DAC_OUT_FORM_MAX, dac_out_sel_txt),
	[VBC_MUX_DAC1_OUT_SEL] = SPRD_VBC_ENUM(VBC_MUX_DAC1_OUT_SEL,
		DAC_OUT_FORM_MAX, dac_out_sel_txt),
};

static const char *vbc_mux_dac_out_id2name(int id)
{
	const char * const vbc_mux_dac_out_name[VBC_MUX_DAC_OUT_MAX] = {
		[VBC_MUX_DAC0_OUT_SEL] = TO_STRING(VBC_MUX_DAC0_OUT_SEL),
		[VBC_MUX_DAC1_OUT_SEL] = TO_STRING(VBC_MUX_DAC1_OUT_SEL),
	};

	if (id >= VBC_MUX_DAC_OUT_MAX) {
		pr_err("invalid id %s %d\n", __func__, id);
		return "";
	}
	if (!vbc_mux_dac_out_name[id]) {
		pr_err("null string =%d\n", id);
		return "";
	}

	return vbc_mux_dac_out_name[id];
}

static int vbc_mux_dac_out_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	ucontrol->value.integer.value[0] = vbc_codec->mux_dac_out[id].val;

	return 0;
}

static int vbc_mux_dac_out_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;
	int val;

	val = ucontrol->value.integer.value[0];
	if (val >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}
	sp_asoc_pr_dbg("%s, %s to %s\n",
		__func__, vbc_mux_dac_out_id2name(id), texts->texts[val]);
	vbc_codec->mux_dac_out[id].id = id;
	vbc_codec->mux_dac_out[id].val = val;
	dsp_vbc_mux_dac_out_set(id, val);

	return 1;
}

/* MUX ADC */
static const char * const mux_adc_sel_txt[ADC_IN_MAX] = {
	[ADC_IN_IIS0_ADC] = TO_STRING(ADC_IN_IIS0_ADC),
	[ADC_IN_IIS1_ADC] = TO_STRING(ADC_IN_IIS1_ADC),
	[ADC_IN_IIS2_ADC] = TO_STRING(ADC_IN_IIS2_ADC),
	[ADC_IN_IIS3_ADC] = TO_STRING(ADC_IN_IIS3_ADC),
	[ADC_IN_DAC0] = TO_STRING(ADC_IN_DAC0),
	[ADC_IN_DAC1] = TO_STRING(ADC_IN_DAC1),
	[ADC_IN_DAC_LOOP] = TO_STRING(ADC_IN_DAC_LOOP),
	[ADC_IN_TDM] = TO_STRING(ADC_IN_TDM),
};

static const struct soc_enum
vbc_mux_adc_enum[VBC_MUX_IN_ADC_ID_MAX] = {
	[VBC_MUX_IN_ADC0] = SPRD_VBC_ENUM(VBC_MUX_IN_ADC0,
					  ADC_IN_MAX, mux_adc_sel_txt),
	[VBC_MUX_IN_ADC1] = SPRD_VBC_ENUM(VBC_MUX_IN_ADC1,
					  ADC_IN_MAX, mux_adc_sel_txt),
	[VBC_MUX_IN_ADC2] = SPRD_VBC_ENUM(VBC_MUX_IN_ADC2,
					  ADC_IN_MAX, mux_adc_sel_txt),
	[VBC_MUX_IN_ADC3] = SPRD_VBC_ENUM(VBC_MUX_IN_ADC3,
					  ADC_IN_MAX, mux_adc_sel_txt),
};

/* convert enum VBC_DMIC_SEL_E to string */
static const char * const vbc_dmic_sel_txt[VBC_DMIC_MAX] = {
	[VBC_DMIC_NONE] = TO_STRING(VBC_DMIC_NONE),
	[VBC_DMIC_0L] = TO_STRING(VBC_DMIC_0L),
	[VBC_DMIC_0R] = TO_STRING(VBC_DMIC_0R),
	[VBC_DMIC_0L_0R] = TO_STRING(VBC_DMIC_0L_0R),
	[VBC_DMIC_1L] = TO_STRING(VBC_DMIC_1L),
	[VBC_DMIC_0L_1L] = TO_STRING(VBC_DMIC_0L_1L),
	[VBC_DMIC_0R_1L] = TO_STRING(VBC_DMIC_0R_1L),
	[VBC_DMIC_0L_0R_1L] = TO_STRING(VBC_DMIC_0L_0R_1L),
	[VBC_DMIC_1R] = TO_STRING(VBC_DMIC_1R),
	[VBC_DMIC_0L_1R] = TO_STRING(VBC_DMIC_0L_1R),
	[VBC_DMIC_0R_1R] = TO_STRING(VBC_DMIC_0R_1R),
	[VBC_DMIC_0L_0R_1R] = TO_STRING(VBC_DMIC_0L_0R_1R),
	[VBC_DMIC_1L_1R] = TO_STRING(VBC_DMIC_1L_1R),
	[VBC_DMIC_0L_1L_1R] = TO_STRING(VBC_DMIC_0L_1L_1R),
	[VBC_DMIC_0R_1L_1R] = TO_STRING(VBC_DMIC_0R_1L_1R),
	[VBC_DMIC_0L_0R_1L_1R] = TO_STRING(VBC_DMIC_0L_0R_1L_1R),
};

static const struct soc_enum dmic_chn_sel_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0, VBC_DMIC_MAX, vbc_dmic_sel_txt);

static int vbc_dmic_chn_sel_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->dmic_chn_sel;

	return 0;
}

static int vbc_dmic_chn_sel_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("set dmic_chn_sel, index outof bounds error\n");
		return -EINVAL;
	}
	vbc_codec->dmic_chn_sel = ucontrol->value.integer.value[0];

	return 0;
}

static const char *vbc_mux_adc_id2name(int id)
{
	const char * const vbc_mux_adc_name[VBC_MUX_IN_ADC_ID_MAX] = {
		[VBC_MUX_IN_ADC0] = TO_STRING(VBC_MUX_IN_ADC0),
		[VBC_MUX_IN_ADC1] = TO_STRING(VBC_MUX_IN_ADC1),
		[VBC_MUX_IN_ADC2] = TO_STRING(VBC_MUX_IN_ADC2),
		[VBC_MUX_IN_ADC3] = TO_STRING(VBC_MUX_IN_ADC3),
	};

	if (id >= VBC_MUX_IN_ADC_ID_MAX) {
		pr_err("invalid id %s %d\n", __func__, id);
		return "";
	}
	if (!vbc_mux_adc_name[id]) {
		pr_err("null string =%d\n", id);
		return "";
	}

	return vbc_mux_adc_name[id];
}

static int vbc_mux_adc_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	ucontrol->value.integer.value[0] = vbc_codec->mux_adc_in[id].val;

	return 0;
}

static int vbc_mux_adc_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;
	int val;

	val = ucontrol->value.integer.value[0];
	if (val >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}
	sp_asoc_pr_dbg("%s, %s to %s\n",
		       __func__, vbc_mux_adc_id2name(id), texts->texts[val]);
	vbc_codec->mux_adc_in[id].id = id;
	vbc_codec->mux_adc_in[id].val = val;
	dsp_vbc_mux_adc_set(id, val);

	return 1;
}

/* MUX FM */
static const char * const mux_fm_sel_txt[FM_IN_VAL_MAX] = {
	[FM_IN_FM_SRC_OUT] = TO_STRING(FM_IN_FM_SRC_OUT),
	[FM_IN_VBC_IF_ADC0] = TO_STRING(FM_IN_VBC_IF_ADC0),
	[FM_IN_VBC_IF_ADC1] = TO_STRING(FM_IN_VBC_IF_ADC1),
	[FM_IN_VBC_IF_ADC2] = TO_STRING(FM_IN_VBC_IF_ADC2),
};

static const struct soc_enum
vbc_mux_fm_enum[VBC_FM_MUX_ID_MAX] = {
	[VBC_FM_MUX] = SPRD_VBC_ENUM(VBC_FM_MUX, FM_IN_VAL_MAX, mux_fm_sel_txt),
};

static const char *vbc_mux_fm_id2name(int id)
{
	const char * const vbc_mux_fm_name[VBC_FM_MUX_ID_MAX] = {
		[VBC_FM_MUX] = TO_STRING(VBC_FM_MUX),
	};

	if (id >= VBC_FM_MUX_ID_MAX) {
		pr_err("invalid id %s %d\n", __func__, id);
		return "";
	}
	if (!vbc_mux_fm_name[id]) {
		pr_err("null string =%d\n", id);
		return "";
	}

	return vbc_mux_fm_name[id];
}

static int vbc_mux_fm_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	ucontrol->value.integer.value[0] = vbc_codec->mux_fm[id].val;

	return 0;
}

static int vbc_mux_fm_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;
	int val;

	val = ucontrol->value.integer.value[0];
	if (val >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}
	sp_asoc_pr_dbg("%s, %s to %s\n",
		       __func__, vbc_mux_fm_id2name(id), texts->texts[val]);
	vbc_codec->mux_fm[id].id = id;
	vbc_codec->mux_fm[id].val = val;
	dsp_vbc_mux_fm_set(id, val);

	return 1;
}

/* MUX ST */
static const char * const mux_st_sel_txt[ST_IN_VAL_MAX] = {
	[ST_IN_ADC0] = TO_STRING(ST_IN_ADC0),
	[ST_IN_ADC0_DG] = TO_STRING(ST_IN_ADC0_DG),
	[ST_IN_ADC1] = TO_STRING(ST_IN_ADC1),
	[ST_IN_ADC1_DG] = TO_STRING(ST_IN_ADC1_DG),
	[ST_IN_ADC2] = TO_STRING(ST_IN_ADC2),
	[ST_IN_ADC2_DG] = TO_STRING(ST_IN_ADC2_DG),
	[ST_IN_ADC3] = TO_STRING(ST_IN_ADC3),
	[ST_IN_ADC3_DG] = TO_STRING(ST_IN_ADC3_DG),
};

static const struct soc_enum
vbc_mux_st_enum[VBC_ST_MUX_ID_MAX] = {
	[VBC_ST_MUX] = SPRD_VBC_ENUM(VBC_ST_MUX,
				     ST_IN_VAL_MAX, mux_st_sel_txt),
};

static const char *vbc_mux_st_id2name(int id)
{
	const char * const vbc_mux_st_name[VBC_ST_MUX_ID_MAX] = {
		[VBC_ST_MUX] = TO_STRING(VBC_ST_MUX),
	};

	if (id >= VBC_ST_MUX_ID_MAX) {
		pr_err("invalid id %s %d\n", __func__, id);
		return "";
	}
	if (!vbc_mux_st_name[id]) {
		pr_err("null string =%d\n", id);
		return "";
	}

	return vbc_mux_st_name[id];
}

static int vbc_mux_st_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	ucontrol->value.integer.value[0] = vbc_codec->mux_st[id].val;

	return 0;
}

static int vbc_mux_st_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;
	int val;

	val = ucontrol->value.integer.value[0];
	if (val >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}
	sp_asoc_pr_dbg("%s, %s to %s\n",
		       __func__, vbc_mux_st_id2name(id), texts->texts[val]);
	vbc_codec->mux_st[id].id = id;
	vbc_codec->mux_st[id].val = val;
	dsp_vbc_mux_st_set(id, val);

	return 1;
}

/* MUX LOOP_DA0 */
static const char * const mux_loop_da0_sel_txt[DAC0_LOOP_OUT_MAX] = {
	[DAC0_SMTHDG_OUT] = TO_STRING(DAC0_SMTHDG_OUT),
	[DAC0_MIX1_OUT] = TO_STRING(DAC0_MIX1_OUT),
	[DAC0_EQ4_OUT] = TO_STRING(DAC0_EQ4_OUT),
	[DAC0_MBDRC_OUT] = TO_STRING(DAC0_MBDRC_OUT),
};

static const struct soc_enum
vbc_mux_loop_da0_enum[VBC_MUX_LOOP_DAC0_MAX] = {
	[VBC_MUX_LOOP_DAC0] = SPRD_VBC_ENUM(VBC_MUX_LOOP_DAC0,
		DAC0_LOOP_OUT_MAX, mux_loop_da0_sel_txt),
};

static const char *vbc_mux_loop_da0_id2name(int id)
{
	const char * const vbc_mux_loop_da0_name[VBC_MUX_LOOP_DAC0_MAX] = {
		[VBC_MUX_LOOP_DAC0] = TO_STRING(VBC_MUX_LOOP_DAC0),
	};

	if (id >= VBC_MUX_LOOP_DAC0_MAX) {
		pr_err("invalid id %s %d\n", __func__, id);
		return "";
	}
	if (!vbc_mux_loop_da0_name[id]) {
		pr_err("null string =%d\n", id);
		return "";
	}

	return vbc_mux_loop_da0_name[id];
}

static int vbc_mux_loop_da0_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	ucontrol->value.integer.value[0] = vbc_codec->mux_loop_dac0[id].val;

	return 0;
}

static int vbc_mux_loop_da0_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;
	int val;

	val = ucontrol->value.integer.value[0];
	if (val >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}
	sp_asoc_pr_dbg("%s, %s to %s\n",
		__func__, vbc_mux_loop_da0_id2name(id), texts->texts[val]);
	vbc_codec->mux_loop_dac0[id].id = id;
	vbc_codec->mux_loop_dac0[id].val = val;
	dsp_vbc_mux_loop_da0_set(id, val);

	return 1;
}

/* MUX LOOP_DA1 */
static const char * const mux_loop_da1_txt[DA1_LOOP_OUT_MAX] = {
	[DAC1_MIXER_OUT] = TO_STRING(DAC1_MIXER_OUT),
	[DAC1_MIXERDG_OUT] = TO_STRING(DAC1_MIXERDG_OUT),
};

static const struct soc_enum
vbc_mux_loop_da1_enum[VBC_MUX_LOOP_DAC1_MAX] = {
	[VBC_MUX_LOOP_DAC1] = SPRD_VBC_ENUM(VBC_MUX_LOOP_DAC1,
					    DA1_LOOP_OUT_MAX, mux_loop_da1_txt),
};

static const char *vbc_mux_loop_da1_id2name(int id)
{
	const char * const vbc_mux_loop_da1_name[VBC_MUX_LOOP_DAC1_MAX] = {
		[VBC_MUX_LOOP_DAC1] = TO_STRING(VBC_MUX_LOOP_DAC1),
	};

	if (id >= VBC_MUX_LOOP_DAC1_MAX) {
		pr_err("invalid id %s %d\n", __func__, id);
		return "";
	}
	if (!vbc_mux_loop_da1_name[id]) {
		pr_err("null string =%d\n", id);
		return "";
	}

	return vbc_mux_loop_da1_name[id];
}

static int vbc_mux_loop_da1_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	ucontrol->value.integer.value[0] = vbc_codec->mux_loop_dac1[id].val;

	return 0;
}

static int vbc_mux_loop_da1_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;
	int val;

	val = ucontrol->value.integer.value[0];
	if (val >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}
	sp_asoc_pr_dbg("%s, %s to %s\n",
		__func__, vbc_mux_loop_da1_id2name(id), texts->texts[val]);
	vbc_codec->mux_loop_dac1[id].id = id;
	vbc_codec->mux_loop_dac1[id].val = val;
	dsp_vbc_mux_loop_da1_set(id, val);

	return 1;
}

/* MUX LOOP_DA0_DA1 */
static const char * const mux_loop_da0_da1_txt[DAC0_DAC1_SEL_MAX] = {
	[DAC0_DAC1_SEL_DAC1] = TO_STRING(DAC0_DAC1_SEL_DAC1),
	[DAC0_DAC1_SEL_DAC0] = TO_STRING(DAC0_DAC1_SEL_DAC0),
};

static const struct soc_enum
vbc_mux_loop_da0_da1_enum[VBC_MUX_LOOP_DAC0_DAC1_MAX] = {
	[VBC_MUX_LOOP_DAC0_DAC1] = SPRD_VBC_ENUM(VBC_MUX_LOOP_DAC0_DAC1,
		DAC0_DAC1_SEL_MAX, mux_loop_da0_da1_txt),
};

static const char *vbc_mux_loop_da0_da1_id2name(int id)
{
	const char * const
		vbc_mux_loop_da0_da1_name[VBC_MUX_LOOP_DAC0_DAC1_MAX] = {
		[VBC_MUX_LOOP_DAC0_DAC1] = TO_STRING(VBC_MUX_LOOP_DAC0_DAC1),
	};

	if (id >= VBC_MUX_LOOP_DAC0_DAC1_MAX) {
		pr_err("invalid id %s %d\n", __func__, id);
		return "";
	}
	if (!vbc_mux_loop_da0_da1_name[id]) {
		pr_err("null string =%d\n", id);
		return "";
	}

	return vbc_mux_loop_da0_da1_name[id];
}

static int vbc_mux_loop_da0_da1_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	ucontrol->value.integer.value[0] =
		vbc_codec->mux_loop_dac0_dac1[id].val;

	return 0;
}

static int vbc_mux_loop_da0_da1_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;
	int val;

	val = ucontrol->value.integer.value[0];
	if (val >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}
	sp_asoc_pr_dbg("%s, %s to %s\n",
		__func__, vbc_mux_loop_da0_da1_id2name(id), texts->texts[val]);
	vbc_codec->mux_loop_dac0_dac1[id].id = id;
	vbc_codec->mux_loop_dac0_dac1[id].val = val;

	dsp_vbc_mux_loop_da0_da1_set(id, val);

	return 1;
}

/* MUX AUDRCD */
static const char * const mux_audrcd_txt[AUDRCD_ADC_IN_MAX] = {
	[AUDRCD_IN_ADC0] = TO_STRING(AUDRCD_IN_ADC0),
	[AUDRCD_IN_ADC1] = TO_STRING(AUDRCD_IN_ADC1),
	[AUDRCD_IN_ADC2] = TO_STRING(AUDRCD_IN_ADC2),
	[AUDRCD_IN_ADC3] = TO_STRING(AUDRCD_IN_ADC3),
};

static const struct soc_enum
vbc_mux_audrcd_enum[VBC_MUX_AUDRCD_ID_MAX] = {
	[VBC_MUX_AUDRCD01] = SPRD_VBC_ENUM(VBC_MUX_AUDRCD01,
					   AUDRCD_ADC_IN_MAX, mux_audrcd_txt),
	[VBC_MUX_AUDRCD23] = SPRD_VBC_ENUM(VBC_MUX_AUDRCD23,
					   AUDRCD_ADC_IN_MAX, mux_audrcd_txt),
};

static const char *vbc_mux_audrcd_id2name(int id)
{
	const char * const vbc_mux_audrcd_name[VBC_MUX_AUDRCD_ID_MAX] = {
		[VBC_MUX_AUDRCD01] = TO_STRING(VBC_MUX_AUDRCD01),
		[VBC_MUX_AUDRCD23] = TO_STRING(VBC_MUX_AUDRCD23),
	};

	if (id >= VBC_MUX_AUDRCD_ID_MAX) {
		pr_err("invalid id %s %d\n", __func__, id);
		return "";
	}
	if (!vbc_mux_audrcd_name[id]) {
		pr_err("null string =%d\n", id);
		return "";
	}

	return vbc_mux_audrcd_name[id];
}

static int vbc_mux_audrcd_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	ucontrol->value.integer.value[0] = vbc_codec->mux_audrcd_in[id].val;

	return 0;
}

static int vbc_mux_audrcd_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;
	int val;

	val = ucontrol->value.integer.value[0];
	if (val >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}
	sp_asoc_pr_dbg("%s, %s to %s\n",
		       __func__, vbc_mux_audrcd_id2name(id), texts->texts[val]);
	vbc_codec->mux_audrcd_in[id].id = id;
	vbc_codec->mux_audrcd_in[id].val = val;
	dsp_vbc_mux_audrcd_set(id, val);

	return 1;
}

/* MUX TDM_AUDRCD23 */
static const char * const mux_tdm_audrcd23_txt[AUDRCD23_TMD_SEL_MAX] = {
	[AUDRCD23_TDM_SEL_AUDRCD23] = TO_STRING(AUDRCD23_TDM_SEL_AUDRCD23),
	[AUDRCD23_TDM_SEL_TDM] = TO_STRING(AUDRCD23_TDM_SEL_TDM),
};

static const struct soc_enum
vbc_mux_tdm_audrcd23_enum[VBC_MUX_TDM_AUDRCD23_MAX] = {
	[VBC_MUX_TDM_AUDRCD23] = SPRD_VBC_ENUM(VBC_MUX_TDM_AUDRCD23,
		AUDRCD23_TMD_SEL_MAX, mux_tdm_audrcd23_txt),
};

static const char *vbc_mux_tdm_audrcd23_id2name(int id)
{
	const char * const
		vbc_mux_tdm_audrcd23_name[VBC_MUX_TDM_AUDRCD23_MAX] = {
		[VBC_MUX_TDM_AUDRCD23] = TO_STRING(VBC_MUX_TDM_AUDRCD23),
	};

	if (id >= VBC_MUX_TDM_AUDRCD23_MAX) {
		pr_err("invalid id %s %d\n", __func__, id);
		return "";
	}
	if (!vbc_mux_tdm_audrcd23_name[id]) {
		pr_err("null string =%d\n", id);
		return "";
	}

	return vbc_mux_tdm_audrcd23_name[id];
}

static int vbc_mux_tdm_audrcd23_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	ucontrol->value.integer.value[0] = vbc_codec->mux_tdm_audrcd23[id].val;

	return 0;
}

static int vbc_mux_tdm_audrcd23_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;
	int val;

	val = ucontrol->value.integer.value[0];
	if (val >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}
	sp_asoc_pr_dbg("%s, %s to %s\n",
		__func__, vbc_mux_tdm_audrcd23_id2name(id), texts->texts[val]);
	vbc_codec->mux_tdm_audrcd23[id].id = id;
	vbc_codec->mux_tdm_audrcd23[id].val = val;
	dsp_vbc_mux_tdm_audrcd23_set(id, val);

	return 1;
}

/* MUX AP01_DSP */
static const char * const mux_ap01_dsp_txt[AP01_TO_DSP_MAX] = {
	[AP01_TO_DSP_DISABLE] = TO_STRING(AP01_TO_DSP_DISABLE),
	[AP01_TO_DSP_ENABLE] = TO_STRING(AP01_TO_DSP_ENABLE),
};

static const struct soc_enum
vbc_mux_ap01_dsp_enum[VBC_MUX_AP01_DSP_ID_MAX] = {
	[VBC_MUX_AP01_DSP_PLY] = SPRD_VBC_ENUM(VBC_MUX_AP01_DSP_PLY,
		AP01_TO_DSP_MAX, mux_ap01_dsp_txt),
	[VBC_MUX_AP01_DSP_RCD] = SPRD_VBC_ENUM(VBC_MUX_AP01_DSP_RCD,
		AP01_TO_DSP_MAX, mux_ap01_dsp_txt),
};

static const char *vbc_mux_ap01_dsp_id2name(int id)
{
	const char * const vbc_mux_ap01_dsp_name[VBC_MUX_AP01_DSP_ID_MAX] = {
		[VBC_MUX_AP01_DSP_PLY] = TO_STRING(VBC_MUX_AP01_DSP_PLY),
		[VBC_MUX_AP01_DSP_RCD] = TO_STRING(VBC_MUX_AP01_DSP_RCD),
	};

	if (id >= VBC_MUX_AP01_DSP_ID_MAX) {
		pr_err("invalid id %s %d\n", __func__, id);
		return "";
	}
	if (!vbc_mux_ap01_dsp_name[id]) {
		pr_err("null string =%d\n", id);
		return "";
	}

	return vbc_mux_ap01_dsp_name[id];
}

static int vbc_mux_ap01_dsp_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	ucontrol->value.integer.value[0] = vbc_codec->mux_ap01_dsp[id].val;

	return 0;
}

static int vbc_mux_ap01_dsp_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;
	int val;

	val = ucontrol->value.integer.value[0];
	if (val >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}
	sp_asoc_pr_dbg("%s, %s to %s\n",
		__func__, vbc_mux_ap01_dsp_id2name(id), texts->texts[val]);
	vbc_codec->mux_ap01_dsp[id].id = id;
	vbc_codec->mux_ap01_dsp[id].val = val;
	dsp_vbc_mux_ap01_dsp_set(id, val);

	return 1;
}

/* MUX IIS_TX */
static const char * const mux_iis_tx_rx_txt[VBC_IIS_PORT_ID_MAX] = {
	[VBC_IIS_PORT_IIS0] = TO_STRING(VBC_IIS_PORT_IIS0),
	[VBC_IIS_PORT_IIS1] = TO_STRING(VBC_IIS_PORT_IIS1),
	[VBC_IIS_PORT_IIS2] = TO_STRING(VBC_IIS_PORT_IIS2),
	[VBC_IIS_PORT_IIS3] = TO_STRING(VBC_IIS_PORT_IIS3),
	[VBC_IIS_PORT_MST_IIS0] = TO_STRING(VBC_IIS_PORT_MST_IIS0),
};

static const struct soc_enum
vbc_mux_iis_tx_enum[VBC_MUX_IIS_TX_ID_MAX] = {
	[VBC_MUX_IIS_TX_DAC0] = SPRD_VBC_ENUM(VBC_MUX_IIS_TX_DAC0,
			VBC_IIS_PORT_ID_MAX, mux_iis_tx_rx_txt),
	[VBC_MUX_IIS_TX_DAC1] = SPRD_VBC_ENUM(VBC_MUX_IIS_TX_DAC1,
			VBC_IIS_PORT_ID_MAX, mux_iis_tx_rx_txt),
	[VBC_MUX_IIS_TX_DAC2] = SPRD_VBC_ENUM(VBC_MUX_IIS_TX_DAC2,
			VBC_IIS_PORT_ID_MAX, mux_iis_tx_rx_txt),
};

static const char *vbc_mux_iis_tx_id2name(int id)
{
	const char * const vbc_mux_iis_tx_name[VBC_MUX_IIS_TX_ID_MAX] = {
		[VBC_MUX_IIS_TX_DAC0] = TO_STRING(VBC_MUX_IIS_TX_DAC0),
		[VBC_MUX_IIS_TX_DAC1] = TO_STRING(VBC_MUX_IIS_TX_DAC1),
		[VBC_MUX_IIS_TX_DAC2] = TO_STRING(VBC_MUX_IIS_TX_DAC2),
	};

	if (id >= VBC_MUX_IIS_TX_ID_MAX) {
		pr_err("invalid id %s %d\n", __func__, id);
		return "";
	}
	if (!vbc_mux_iis_tx_name[id]) {
		pr_err("null string =%d\n", id);
		return "";
	}

	return vbc_mux_iis_tx_name[id];
}

static int vbc_mux_iis_tx_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	ucontrol->value.integer.value[0] = vbc_codec->mux_iis_tx[id].val;

	return 0;
}

static int vbc_mux_iis_tx_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;
	int val;

	val = ucontrol->value.integer.value[0];
	if (val >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}
	sp_asoc_pr_dbg("%s, %s to %s\n",
		       __func__, vbc_mux_iis_tx_id2name(id), texts->texts[val]);
	vbc_codec->mux_iis_tx[id].id = id;
	vbc_codec->mux_iis_tx[id].val = val;
	dsp_vbc_mux_iis_tx_set(id, val);

	return 1;
}

/* MUX IIS_RX */
static const struct soc_enum
vbc_mux_iis_rx_enum[VBC_MUX_IIS_RX_ID_MAX] = {
	[VBC_MUX_IIS_RX_ADC0] = SPRD_VBC_ENUM(VBC_MUX_IIS_RX_ADC0,
		VBC_IIS_PORT_ID_MAX, mux_iis_tx_rx_txt),
	[VBC_MUX_IIS_RX_ADC1] = SPRD_VBC_ENUM(VBC_MUX_IIS_RX_ADC1,
		VBC_IIS_PORT_ID_MAX, mux_iis_tx_rx_txt),
	[VBC_MUX_IIS_RX_ADC2] = SPRD_VBC_ENUM(VBC_MUX_IIS_RX_ADC2,
		VBC_IIS_PORT_ID_MAX, mux_iis_tx_rx_txt),
	[VBC_MUX_IIS_RX_ADC3] = SPRD_VBC_ENUM(VBC_MUX_IIS_RX_ADC3,
		VBC_IIS_PORT_ID_MAX, mux_iis_tx_rx_txt),
};

static const char *vbc_mux_iis_rx_id2name(int id)
{
	const char * const vbc_mux_iis_rx_name[VBC_MUX_IIS_RX_ID_MAX] = {
		[VBC_MUX_IIS_RX_ADC0] = TO_STRING(VBC_MUX_IIS_RX_ADC0),
		[VBC_MUX_IIS_RX_ADC1] = TO_STRING(VBC_MUX_IIS_RX_ADC1),
		[VBC_MUX_IIS_RX_ADC2] = TO_STRING(VBC_MUX_IIS_RX_ADC2),
		[VBC_MUX_IIS_RX_ADC3] = TO_STRING(VBC_MUX_IIS_RX_ADC3),
	};

	if (id >= VBC_MUX_IIS_RX_ID_MAX) {
		pr_err("invalid id %s %d\n", __func__, id);
		return "";
	}
	if (!vbc_mux_iis_rx_name[id]) {
		pr_err("null string =%d\n", id);
		return "";
	}

	return vbc_mux_iis_rx_name[id];
}

static int vbc_mux_iis_rx_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	ucontrol->value.integer.value[0] = vbc_codec->mux_iis_rx[id].val;

	return 0;
}

static int vbc_mux_iis_rx_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;
	int val;

	val = ucontrol->value.integer.value[0];
	if (val >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}
	sp_asoc_pr_dbg("%s, %s to %s\n",
		       __func__, vbc_mux_iis_rx_id2name(id), texts->texts[val]);
	vbc_codec->mux_iis_rx[id].id = id;
	vbc_codec->mux_iis_rx[id].val = val;
	dsp_vbc_mux_iis_rx_set(id, val);

	return 1;
}

/* MUX IIS_PORT_DO */
static const char * const mux_iis_port_do_txt[IIS_DO_VAL_MAX] = {
	[IIS_DO_VAL_DAC0] = TO_STRING(IIS_DO_VAL_DAC0),
	[IIS_DO_VAL_DAC1] = TO_STRING(IIS_DO_VAL_DAC1),
	[IIS_DO_VAL_DAC2] = TO_STRING(IIS_DO_VAL_DAC2),
};

static const struct soc_enum
vbc_mux_iis_port_do_enum[VBC_IIS_PORT_ID_MAX] = {
	[VBC_IIS_PORT_IIS0] = SPRD_VBC_ENUM(VBC_IIS_PORT_IIS0,
				IIS_DO_VAL_MAX, mux_iis_port_do_txt),
	[VBC_IIS_PORT_IIS1] = SPRD_VBC_ENUM(VBC_IIS_PORT_IIS1,
				IIS_DO_VAL_MAX, mux_iis_port_do_txt),
	[VBC_IIS_PORT_IIS2] = SPRD_VBC_ENUM(VBC_IIS_PORT_IIS2,
				IIS_DO_VAL_MAX, mux_iis_port_do_txt),
	[VBC_IIS_PORT_IIS3] = SPRD_VBC_ENUM(VBC_IIS_PORT_IIS3,
				IIS_DO_VAL_MAX, mux_iis_port_do_txt),
	[VBC_IIS_PORT_MST_IIS0] = SPRD_VBC_ENUM(VBC_IIS_PORT_MST_IIS0,
				IIS_DO_VAL_MAX, mux_iis_port_do_txt),
};

static const char *vbc_mux_iis_port_id2name(int id)
{
	const char * const vbc_mux_iis_port_name[VBC_IIS_PORT_ID_MAX] = {
		[VBC_IIS_PORT_IIS0] = TO_STRING(VBC_IIS_PORT_IIS0),
		[VBC_IIS_PORT_IIS1] = TO_STRING(VBC_IIS_PORT_IIS1),
		[VBC_IIS_PORT_IIS2] = TO_STRING(VBC_IIS_PORT_IIS2),
		[VBC_IIS_PORT_IIS3] = TO_STRING(VBC_IIS_PORT_IIS3),
		[VBC_IIS_PORT_MST_IIS0] = TO_STRING(VBC_IIS_PORT_MST_IIS0),
	};

	if (id >= VBC_IIS_PORT_ID_MAX) {
		pr_err("invalid id %s %d\n", __func__, id);
		return "";
	}
	if (!vbc_mux_iis_port_name[id]) {
		pr_err("null string =%d\n", id);
		return "";
	}

	return vbc_mux_iis_port_name[id];
}

static int vbc_mux_iis_port_do_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	ucontrol->value.integer.value[0] = vbc_codec->mux_iis_port_do[id].val;

	return 0;
}

static int vbc_mux_iis_port_do_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;
	int val;

	val = ucontrol->value.integer.value[0];
	if (val >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}
	sp_asoc_pr_dbg("%s, %s to %s\n",
		__func__, vbc_mux_iis_port_id2name(id), texts->texts[val]);
	vbc_codec->mux_iis_port_do[id].id = id;
	vbc_codec->mux_iis_port_do[id].val = val;
	dsp_vbc_mux_iis_port_do_set(id, val);

	return 1;
}

/* ADDER */
static const char *vbc_adder_id2name(int id)
{
	const char * const vbc_adder_name[VBC_ADDER_MAX] = {
		[VBC_ADDER_OFLD] = TO_STRING(VBC_ADDER_OFLD),
		[VBC_ADDER_FM_DAC0] = TO_STRING(VBC_ADDER_FM_DAC0),
		[VBC_ADDER_ST_DAC0] = TO_STRING(VBC_ADDER_ST_DAC0),
	};

	if (id >= VBC_ADDER_MAX) {
		pr_err("invalid id %s %d\n", __func__, id);
		return "";
	}
	if (!vbc_adder_name[id]) {
		pr_err("null string =%d\n", id);
		return "";
	}

	return vbc_adder_name[id];
}

static const char * const adder_mode_txt[ADDER_MOD_MAX] = {
	[ADDER_MOD_IGNORE] = TO_STRING(ADDER_MOD_IGNORE),
	[ADDER_MOD_ADD] = TO_STRING(ADDER_MOD_ADD),
	[ADDER_MOD_MINUS] = TO_STRING(ADDER_MOD_MINUS),
};

static const struct soc_enum
vbc_adder_enum[VBC_ADDER_MAX] = {
	[VBC_ADDER_OFLD] =
		SPRD_VBC_ENUM(VBC_ADDER_OFLD, ADDER_MOD_MAX, adder_mode_txt),
	[VBC_ADDER_FM_DAC0] =
		SPRD_VBC_ENUM(VBC_ADDER_FM_DAC0, ADDER_MOD_MAX, adder_mode_txt),
	[VBC_ADDER_ST_DAC0] =
		SPRD_VBC_ENUM(VBC_ADDER_ST_DAC0, ADDER_MOD_MAX, adder_mode_txt),
};

static int vbc_adder_get(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	ucontrol->value.integer.value[0] =
		vbc_codec->vbc_adder[id].adder_mode_l;

	return 0;
}

static int vbc_adder_put(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;
	int val;

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}
	val = ucontrol->value.integer.value[0];
	sp_asoc_pr_dbg("%s %s to %s\n",
		       __func__, vbc_adder_id2name(id), texts->texts[val]);
	vbc_codec->vbc_adder[id].adder_id = id;
	vbc_codec->vbc_adder[id].adder_mode_l = val;
	vbc_codec->vbc_adder[id].adder_mode_r = val;
	dsp_vbc_adder_set(id, val, val);

	return 1;

}

/* DATA_PATH  */
static const char *vbc_datapath_id2name(int id)
{
	const char * const vbc_datapath_name[VBC_DP_EN_MAX] = {
		[VBC_DAC0_DP_EN] = TO_STRING(VBC_DAC0_DP_EN),
		[VBC_DAC1_DP_EN] = TO_STRING(VBC_DAC1_DP_EN),
		[VBC_DAC2_DP_EN] = TO_STRING(VBC_DAC2_DP_EN),
		[VBC_ADC0_DP_EN] = TO_STRING(VBC_ADC0_DP_EN),
		[VBC_ADC1_DP_EN] = TO_STRING(VBC_ADC1_DP_EN),
		[VBC_ADC2_DP_EN] = TO_STRING(VBC_ADC2_DP_EN),
		[VBC_ADC3_DP_EN] = TO_STRING(VBC_ADC3_DP_EN),
		[VBC_OFLD_DP_EN] = TO_STRING(VBC_OFLD_DP_EN),
		[VBC_FM_DP_EN] = TO_STRING(VBC_FM_DP_EN),
		[VBC_ST_DP_EN] = TO_STRING(VBC_ST_DP_EN),
	};

	if (id >= VBC_DP_EN_MAX) {
		pr_err("invalid id %s %d\n", __func__, id);
		return "";
	}
	if (!vbc_datapath_name[id]) {
		pr_err("null string =%d\n", id);
		return "";
	}

	return vbc_datapath_name[id];
}

static const struct soc_enum
vbc_datapath_enum[VBC_DP_EN_MAX] = {
	[VBC_DAC0_DP_EN] = SPRD_VBC_ENUM(VBC_DAC0_DP_EN, 2, enable_disable_txt),
	[VBC_DAC1_DP_EN] = SPRD_VBC_ENUM(VBC_DAC1_DP_EN, 2, enable_disable_txt),
	[VBC_DAC2_DP_EN] = SPRD_VBC_ENUM(VBC_DAC2_DP_EN, 2, enable_disable_txt),
	[VBC_ADC0_DP_EN] = SPRD_VBC_ENUM(VBC_ADC0_DP_EN, 2, enable_disable_txt),
	[VBC_ADC1_DP_EN] = SPRD_VBC_ENUM(VBC_ADC1_DP_EN, 2, enable_disable_txt),
	[VBC_ADC2_DP_EN] = SPRD_VBC_ENUM(VBC_ADC2_DP_EN, 2, enable_disable_txt),
	[VBC_ADC3_DP_EN] = SPRD_VBC_ENUM(VBC_ADC3_DP_EN, 2, enable_disable_txt),
	[VBC_OFLD_DP_EN] = SPRD_VBC_ENUM(VBC_OFLD_DP_EN, 2, enable_disable_txt),
	[VBC_FM_DP_EN] = SPRD_VBC_ENUM(VBC_FM_DP_EN, 2, enable_disable_txt),
	[VBC_ST_DP_EN] = SPRD_VBC_ENUM(VBC_ST_DP_EN, 2, enable_disable_txt),
};

static int vbc_dp_en_get(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	ucontrol->value.integer.value[0] = vbc_codec->vbc_dp_en[id].enable;

	return 0;
}

static int vbc_dp_en_put(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	u16 value;
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}

	value = ucontrol->value.enumerated.item[0];
	sp_asoc_pr_dbg("%s, %s = %s\n",
		       __func__, vbc_datapath_id2name(id), texts->texts[value]);
	vbc_codec->vbc_dp_en[id].id = id;
	vbc_codec->vbc_dp_en[id].enable = value;
	dsp_vbc_dp_en_set(id, value);

	return 1;
}

/* CALL_MUTE */
static const char *vbc_callmute_id2name(int id)
{
	const char * const vbc_callmute_name[VBC_MUTE_MAX] = {
		[VBC_UL_MUTE] = TO_STRING(VBC_UL_MUTE),
		[VBC_DL_MUTE] = TO_STRING(VBC_DL_MUTE),
	};

	if (id >= VBC_MUTE_MAX) {
		pr_err("invalid id %s %d\n", __func__, id);
		return "";
	}
	if (!vbc_callmute_name[id]) {
		pr_err("null string =%d\n", id);
		return "";
	}

	return vbc_callmute_name[id];
}

static const struct soc_enum
vbc_call_mute_enum[VBC_MUTE_MAX] = {
	SPRD_VBC_ENUM(VBC_UL_MUTE, 2, enable_disable_txt),
	SPRD_VBC_ENUM(VBC_DL_MUTE, 2, enable_disable_txt),
};

static int vbc_call_mute_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	u16 value;
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}

	value = ucontrol->value.enumerated.item[0];
	sp_asoc_pr_dbg("%s, %s %s\n",
		       __func__, vbc_callmute_id2name(id), texts->texts[value]);
	vbc_codec->vbc_call_mute[id].id = id;
	vbc_codec->vbc_call_mute[id].mute = value;
	dsp_call_mute_set(id, value);

	return true;
}

/* FM_MUTE */
static const struct soc_enum vbc_fm_mute_enum =
	SPRD_VBC_ENUM(SND_SOC_NOPM, 2, mute_unmute_txt);

static int vbc_fm_mute_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->fm_mute.mute;

	return 0;
}

static int vbc_fm_mute_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	u16 value;
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}

	value = ucontrol->value.enumerated.item[0];
	sp_asoc_pr_dbg("%s, fm_mute %s, mute step %d\n", __func__,
				   texts->texts[value],
				   vbc_codec->fm_mute_step.step);
	vbc_codec->fm_mute.id = id;
	vbc_codec->fm_mute.mute = value;
	fm_mute_set(id, value, vbc_codec->fm_mute_step.step);

	return true;
}

static int vbc_fm_mdg_stp_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->fm_mute_step.step;

	return 0;
}

static int vbc_fm_mdg_stp_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	int value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	value = ucontrol->value.integer.value[0];
	sp_asoc_pr_dbg("%s vbc_fm_mdg_stp = %d\n",
			   __func__, value);
	vbc_codec->fm_mute_step.step = value;

	return value;
}

/* IIS_TX_WIDTH */
static const char * const vbc_iis_width_txt[IIS_WD_MAX] = {
	[WD_16BIT] = TO_STRING(WD_16BIT),
	[WD_24BIT] = TO_STRING(WD_24BIT),
};

static const struct soc_enum vbc_iis_tx_wd_enum[VBC_MUX_IIS_TX_ID_MAX] = {
	SPRD_VBC_ENUM(VBC_MUX_IIS_TX_DAC0, IIS_WD_MAX, vbc_iis_width_txt),
	SPRD_VBC_ENUM(VBC_MUX_IIS_TX_DAC1, IIS_WD_MAX, vbc_iis_width_txt),
	SPRD_VBC_ENUM(VBC_MUX_IIS_TX_DAC2, IIS_WD_MAX, vbc_iis_width_txt),
};

static int vbc_get_iis_tx_width_sel(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	ucontrol->value.integer.value[0] = vbc_codec->iis_tx_wd[id].value;

	return 0;
}

static int vbc_put_iis_tx_width_sel(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	u32 value;
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}

	value = ucontrol->value.enumerated.item[0];
	sp_asoc_pr_dbg("%s, %s=%s\n",
		__func__, vbc_mux_iis_tx_id2name(id), texts->texts[value]);
	vbc_codec->iis_tx_wd[id].id = id;
	vbc_codec->iis_tx_wd[id].value = value;
	dsp_vbc_iis_tx_width_set(id, value);

	return 1;
}

/* IIS_TX_LR_MOD */
static const char * const vbc_iis_lr_mod_txt[LR_MOD_MAX] = {
	[LEFT_HIGH] = TO_STRING(LEFT_HIGH),
	[RIGHT_HIGH] = TO_STRING(RIGHT_HIGH),
};

static const struct soc_enum vbc_iis_tx_lr_mod_enum[VBC_MUX_IIS_TX_ID_MAX] = {
	SPRD_VBC_ENUM(VBC_MUX_IIS_TX_DAC0, IIS_WD_MAX, vbc_iis_lr_mod_txt),
	SPRD_VBC_ENUM(VBC_MUX_IIS_TX_DAC1, IIS_WD_MAX, vbc_iis_lr_mod_txt),
	SPRD_VBC_ENUM(VBC_MUX_IIS_TX_DAC2, IIS_WD_MAX, vbc_iis_lr_mod_txt),
};

static int vbc_get_iis_tx_lr_mod_sel(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	ucontrol->value.integer.value[0] = vbc_codec->iis_tx_lr_mod[id].value;

	return 0;
}

static int vbc_put_iis_tx_lr_mod_sel(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	u32 value;
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}
	value = ucontrol->value.enumerated.item[0];
	sp_asoc_pr_dbg("%s, %s = %s\n",
		__func__, vbc_mux_iis_tx_id2name(id), texts->texts[value]);
	vbc_codec->iis_tx_lr_mod[id].id = id;
	vbc_codec->iis_tx_lr_mod[id].value = value;
	dsp_vbc_iis_tx_lr_mod_set(id, value);

	return 1;
}

/* IIS_RX_WIDTH */
static const struct soc_enum vbc_iis_rx_wd_enum[VBC_MUX_IIS_RX_ID_MAX] = {
	SPRD_VBC_ENUM(VBC_MUX_IIS_RX_ADC0, IIS_WD_MAX, vbc_iis_width_txt),
	SPRD_VBC_ENUM(VBC_MUX_IIS_RX_ADC1, IIS_WD_MAX, vbc_iis_width_txt),
	SPRD_VBC_ENUM(VBC_MUX_IIS_RX_ADC2, IIS_WD_MAX, vbc_iis_width_txt),
	SPRD_VBC_ENUM(VBC_MUX_IIS_RX_ADC3, IIS_WD_MAX, vbc_iis_width_txt),
};

static int vbc_get_iis_rx_width_sel(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	ucontrol->value.integer.value[0] = vbc_codec->iis_rx_wd[id].value;

	return 0;
}

static int vbc_put_iis_rx_width_sel(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	u32 value;
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}

	value = ucontrol->value.enumerated.item[0];
	sp_asoc_pr_dbg("%s, %s=%s\n",
		__func__, vbc_mux_iis_rx_id2name(id), texts->texts[value]);
	vbc_codec->iis_rx_wd[id].id = id;
	vbc_codec->iis_rx_wd[id].value = value;
	dsp_vbc_iis_rx_width_set(id, value);

	return 1;
}

static int dsp_aux_mic2_sel_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->aux_mic2_sel_enable;

	return 0;
}

static int dsp_aux_mic2_sel_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	u32 value;
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}
	value = ucontrol->value.enumerated.item[0];
	sp_asoc_pr_dbg("%s, %s = %s\n",
		       __func__, "dsp_aux_mic2_sel", texts->texts[value]);

	vbc_codec->aux_mic2_sel_enable = value;
	dsp_aux_mic2_sel_set(value);

	return 1;
}

static int dsp_smartamp_iv_exchange_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] =
					vbc_codec->smartamp_iv_exchange_enable;

	return 0;
}

static int dsp_smartamp_iv_exchange_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	u32 value;
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}
	value = ucontrol->value.enumerated.item[0];
	sp_asoc_pr_dbg("%s, %s = %s\n",
		    __func__, "dsp_smartamp_iv_exchange", texts->texts[value]);

	vbc_codec->smartamp_iv_exchange_enable = value;
	dsp_smartamp_iv_exchange_set(value);

	return 1;
}

static int dsp_dac0_lr_exchange_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] =
		vbc_codec->dsp_dac0_lr_exchange;

	return 0;
}

static int dsp_dac0_lr_exchange_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	u32 value;
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}
	value = ucontrol->value.enumerated.item[0];
	sp_asoc_pr_dbg("%s, %s = %s\n",
		    __func__, "dsp_dac0_lr_exchange", texts->texts[value]);

	vbc_codec->dsp_dac0_lr_exchange = value;
	dsp_dac0_lr_exchange_set(value);

	return 1;
}

static int vbc_custm_dev_change_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->cust_dev;

	return 0;
}

/* device changed by customer */
static int vbc_custm_dev_change_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	u32 value;
	int ret;
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}

	value = ucontrol->value.enumerated.item[0];
	sp_asoc_pr_dbg("%s, device change to %s %s\n",
		__func__, texts->texts[value], dsp_device_change_txt[value]);
	vbc_codec->cust_dev = value;
	ret = vbc_dsp_cust_dev_set(value, 0, 0);
	if (ret < 0) {
		pr_err("vbc_dsp_cust_dev Failed to set, ret: %d\n", ret);
		return ret;
	}
	return 1;
}

static int vbc_system_dev_change_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->sys_dev;

	return 0;
}

/* device changed by system */
static int vbc_system_dev_change_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	u32 value;
	int ret;
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}

	value = ucontrol->value.enumerated.item[0];
	sp_asoc_pr_dbg("%s, device change to %s %s\n",
		__func__, texts->texts[value], dsp_device_change_txt[value]);

	vbc_codec->sys_dev = value;
	ret = vbc_dsp_sys_dev_set(value, 0, 0);

	if (ret < 0) {
		pr_err("vbc_dsp_sys_dev Failed to set, ret: %d\n", ret);
		return ret;
	}
	return 1;
}

static int vbc_custm_vol_change_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->cust_vol;

	return 0;
}

/* volume changed by customer */
static int vbc_custm_vol_change_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	u32 value;
	int ret;
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}

	value = ucontrol->value.enumerated.item[0];
	sp_asoc_pr_dbg("%s, device change to %s %s\n",
		__func__, texts->texts[value], dsp_custm_vol_change_txt[value]);

	vbc_codec->cust_vol = value;
	ret = vbc_dsp_cust_vol_set(value, 0, 0);

	if (ret < 0) {
		pr_err("vbc_dsp_sys_dev Failed to set, ret: %d\n", ret);
		return ret;
	}
	return 1;
}

/* IIS_RX_LR_MOD */
static const struct soc_enum vbc_iis_rx_lr_mod_enum[VBC_MUX_IIS_RX_ID_MAX] = {
	SPRD_VBC_ENUM(VBC_MUX_IIS_RX_ADC0, IIS_WD_MAX, vbc_iis_lr_mod_txt),
	SPRD_VBC_ENUM(VBC_MUX_IIS_RX_ADC1, IIS_WD_MAX, vbc_iis_lr_mod_txt),
	SPRD_VBC_ENUM(VBC_MUX_IIS_RX_ADC2, IIS_WD_MAX, vbc_iis_lr_mod_txt),
	SPRD_VBC_ENUM(VBC_MUX_IIS_RX_ADC3, IIS_WD_MAX, vbc_iis_lr_mod_txt),
};

static int vbc_get_iis_rx_lr_mod_sel(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	ucontrol->value.integer.value[0] = vbc_codec->iis_rx_lr_mod[id].value;

	return 0;
}

static int vbc_put_iis_rx_lr_mod_sel(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	u32 value;
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}
	value = ucontrol->value.enumerated.item[0];
	sp_asoc_pr_dbg("%s, %s = %s\n",
		__func__, vbc_mux_iis_rx_id2name(id), texts->texts[value]);
	vbc_codec->iis_rx_lr_mod[id].id = id;
	vbc_codec->iis_rx_lr_mod[id].value = value;
	dsp_vbc_iis_rx_lr_mod_set(id, value);

	return 1;
}

static const char * const vbc_iis_mst_sel_txt[VBC_MASTER_TYPE_MAX] = {
	[VBC_MASTER_EXTERNAL] = TO_STRING(VBC_MASTER_EXTERNAL),
	[VBC_MASTER_INTERNAL] = TO_STRING(VBC_MASTER_INTERNAL),
};

static const struct soc_enum vbc_iis_mst_sel_enum[IIS_MST_SEL_ID_MAX] = {
	SPRD_VBC_ENUM(IIS_MST_SEL_0, VBC_MASTER_TYPE_MAX, vbc_iis_mst_sel_txt),
	SPRD_VBC_ENUM(IIS_MST_SEL_1, VBC_MASTER_TYPE_MAX, vbc_iis_mst_sel_txt),
	SPRD_VBC_ENUM(IIS_MST_SEL_2, VBC_MASTER_TYPE_MAX, vbc_iis_mst_sel_txt),
	SPRD_VBC_ENUM(IIS_MST_SEL_3, VBC_MASTER_TYPE_MAX, vbc_iis_mst_sel_txt),
};

static int vbc_get_mst_sel_type(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	ucontrol->value.enumerated.item[0] =
		vbc_codec->mst_sel_para[id].mst_type;

	return 0;
}

static int vbc_put_mst_sel_type(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	u32 value;
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	if (ucontrol->value.enumerated.item[0] >= texts->items) {
		pr_err("mst_sel_type, index outof bounds error\n");
		return -EINVAL;
	}

	value = ucontrol->value.enumerated.item[0];
	vbc_codec->mst_sel_para[id].id = id;
	vbc_codec->mst_sel_para[id].mst_type = value;
	sp_asoc_pr_dbg("mst_sel_type id %d, value %d\n", id, value);
	dsp_vbc_mst_sel_type_set(id, value);

	return 0;
}

/* IIS MASTER */
static const struct soc_enum
vbc_iis_master_enum = SPRD_VBC_ENUM(SND_SOC_NOPM, 2, enable_disable_txt);

static int vbc_get_iis_master_en(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->iis_master.enable;

	return 0;
}

static int vbc_put_iis_master_en(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	u32 value;
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}
	value = ucontrol->value.enumerated.item[0];
	sp_asoc_pr_dbg("%s, %s = %s\n",
		       __func__, "vbc_iis_master", texts->texts[value]);

	vbc_codec->iis_master.enable = value;
	dsp_vbc_iis_master_start(value);

	return 1;
}

static const struct soc_enum vbc_iis_master_wd_width_enum =
	SPRD_VBC_ENUM(SND_SOC_NOPM, 2, vbc_iis_width_txt);

static int vbc_get_iis_master_width(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->iis_mst_width;

	return 0;
}

static int vbc_put_iis_master_width(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	u32 value;
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}

	value = ucontrol->value.enumerated.item[0];
	sp_asoc_pr_dbg("set iis master wd width to %s\n", texts->texts[value]);

	vbc_codec->iis_mst_width = value;
	dsp_vbc_iis_master_width_set(value);

	return 0;
}

static int vbc_mainmic_path_sel_val_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	ucontrol->value.integer.value[0] =
		vbc_codec->mainmic_from[id].main_mic_from;

	return 0;
}

static int vbc_mainmic_path_sel_val_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;
	int val;

	val = ucontrol->value.integer.value[0];
	if (val >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}
	sp_asoc_pr_dbg("%s, type %d is %s\n",
		__func__, id, texts->texts[val]);

	vbc_codec->mainmic_from[id].type = id;
	vbc_codec->mainmic_from[id].main_mic_from = val;


	dsp_vbc_mux_adc_source_set(id, val);

	return 1;
}

static const struct soc_enum
ivsence_enum = SPRD_VBC_ENUM(SND_SOC_NOPM, 2, enable_disable_txt);

static int vbc_get_ivsence_func(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->is_use_ivs_smtpa;

	return 0;
}

static int vbc_put_ivsence_func(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	u32 enable;
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	int iv_adc_id;

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}
	enable = ucontrol->value.enumerated.item[0];
	pr_info("%s, %s = %s\n",
		__func__, "ivsence func dsp", texts->texts[enable]);
	vbc_codec->is_use_ivs_smtpa = enable;
	iv_adc_id = get_ivsense_adc_id();
	dsp_ivsence_func(enable, iv_adc_id);

	return 1;
}

static int vbc_profile_set(struct snd_soc_component *codec, void *data,
			   int profile_type, int mode)
{
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct vbc_profile *p_profile_setting = &vbc_codec->vbc_profile_setting;
	int ret;

	ret = aud_send_block_param(AMSG_CH_VBC_CTL, mode, -1,
		SND_VBC_DSP_IO_SHAREMEM_SET, profile_type, data,
		p_profile_setting->hdr[profile_type].len_mode,
				   AUDIO_SIPC_WAIT_FOREVER);
	if (ret < 0)
		return -EIO;

	return 0;
}

static int vbc_profile_try_apply(struct snd_soc_component *codec,
				 int profile_id, u32 selector)
{
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct vbc_profile *profile = &vbc_codec->vbc_profile_setting;
	u32 offset = selector >> 24;
	void *data;
	int ret, changed;
	if (profile_id < 0 || profile_id >= SND_VBC_PROFILE_MAX)
		return -EINVAL;
	mutex_lock(&vbc_codec->load_mutex);
	if (!profile->data[profile_id] || !profile->hdr[profile_id].len_mode ||
	    offset >= profile->hdr[profile_id].num_mode) {
		ret = -EINVAL;
		goto out;
	}
	data = (u8 *)profile->data[profile_id] + (size_t)profile->hdr[profile_id].len_mode * offset;
	changed = (u32)profile->now_mode[profile_id] != selector;
	ret = vbc_profile_set(codec, data, profile_id, selector);
	if (!ret) {
		profile->now_mode[profile_id] = selector;
		ret = changed;
	}
out:
	mutex_unlock(&vbc_codec->load_mutex);
	return ret;
}

#if 1
#include <linux/cdev.h>
#define AUDIO_TURNING_PATH_BASE "vbc_turning"
#define AUD_TURNING_PRO_CNTS (SND_VBC_PROFILE_MAX)
#define AUD_TURNING_MINOR_START (0)
struct vbc_codec_priv *g_vbc_codec;


static int vbc_profile_store(struct snd_soc_component *codec, int id,
			     const u8 *data, size_t size)
{
	struct vbc_codec_priv *vbc = snd_soc_component_get_drvdata(codec);
	struct vbc_profile *profile = &vbc->vbc_profile_setting;
	struct vbc_fw_header header;
	size_t length;
	void *replacement, *old;
	if (id < 0 || id >= SND_VBC_PROFILE_MAX || size < sizeof(header))
		return -EINVAL;
	memcpy(&header, data, sizeof(header));
	if (strncmp(header.magic, VBC_PROFILE_FIRMWARE_MAGIC_ID, sizeof(header.magic)) ||
	    !header.num_mode || header.num_mode > 128 || !header.len_mode ||
	    check_mul_overflow((size_t)header.num_mode, (size_t)header.len_mode, &length) ||
	    length > size - sizeof(header))
		return -EINVAL;
	replacement = kmemdup(data + sizeof(header), length, GFP_KERNEL);
	if (!replacement)
		return -ENOMEM;
	mutex_lock(&vbc->load_mutex);
	old = profile->data[id];
	profile->data[id] = replacement;
	profile->hdr[id] = header;
	profile->is_loading[id] = 0;
	mutex_unlock(&vbc->load_mutex);
	kfree(old);
	return 0;
}

static int vbc_turning_profile_loading(const u8 *profile_data, size_t profile_size,
				      struct snd_soc_component *codec, int profile_id)
{
	return vbc_profile_store(codec, profile_id, profile_data, profile_size);
}

static int vbc_turning_profile_get(u8 *data, size_t size,
				 struct snd_soc_component *codec, int id)
{
	struct vbc_codec_priv *vbc = snd_soc_component_get_drvdata(codec);
	struct vbc_profile *profile = &vbc->vbc_profile_setting;
	size_t header_size, bytes;
	int ret;
	if (id < 0 || id >= SND_VBC_PROFILE_MAX || size > INT_MAX)
		return -EINVAL;
	mutex_lock(&vbc->load_mutex);
	header_size = min(size, sizeof(profile->hdr[id]));
	memcpy(data, &profile->hdr[id], header_size);
	ret = header_size;
	if (size > header_size && profile->data[id]) {
		bytes = min(size - header_size,
			    (size_t)profile->hdr[id].num_mode * profile->hdr[id].len_mode);
		memcpy(data + header_size, profile->data[id], bytes);
		ret += bytes;
	}
	mutex_unlock(&vbc->load_mutex);
	return ret;
}

static int vbc_turning_ndp_open(struct inode *inode, struct file *file)
{
    pr_info("%s: opened\n", __func__);
    return 0;
}

static ssize_t vbc_turning_ndp_write(struct file *file, const char __user *data_p, size_t data_size, loff_t *ppos, int profile_id)
{
	void *buf_p = NULL;
	int ret = 0;

	if (NULL == data_p) {
		pr_err("%s, Error: data_p is NULL\n", __func__);
		return -EINVAL;
	}

	if (0 == data_size) {
		pr_err("%s, Error: size is 0\n", __func__);
		return -EINVAL;
	}

	if (NULL == g_vbc_codec) {
		pr_err("%s: Error: not find codec.\n", __func__);
		return -ENODEV;
	}

	pr_info("%s, eq turning data_p = %p, size =%ld, profile id = %d\n", __func__, data_p, data_size, profile_id);
	buf_p = vmalloc(data_size);
	if (NULL == buf_p) {
		pr_err("%s, Error: vmalloc eq data buffer failed, size is %ld\n", __func__, data_size);
		return -ENOMEM;
	}

	if (copy_from_user(buf_p, data_p, data_size)) {
		pr_err("%s: Error: arg protection error\n", __func__);
		vfree(buf_p);
		return -EACCES;
	}

	ret = vbc_turning_profile_loading((const u8 *)buf_p, data_size, g_vbc_codec->codec, profile_id);
	if (ret < 0) {
		vfree(buf_p);
		pr_err("%s: Error: load eq from turning failed, ret=%d.\n", __func__, ret);
		return ret;
	}

	vfree(buf_p);
	pr_info("%s: load eq from turning success.\n", __func__);
	return data_size;
}

static ssize_t vbc_turning_ndp_read(struct file *file, char __user *data_p, size_t data_size, loff_t *ppos, int profile_id)
{
	void *buf_p = NULL;
	int ret = 0;

	if (NULL == data_p) {
		pr_err("%s, Error: data_p is NULL\n", __func__);
		return -EINVAL;
	}

	if (0 == data_size) {
		pr_err("%s, Error: size is 0\n", __func__);
		return -EINVAL;
	}

	if (NULL == g_vbc_codec) {
		pr_err("%s: Error: not find codec.\n", __func__);
		return -ENODEV;
	}

	pr_info("%s, eq turning data_p = %p, size =%ld, profile id = %d\n", __func__, data_p, data_size, profile_id);
	buf_p = vmalloc(data_size);
	if (NULL == buf_p) {
		pr_err("%s, Error: vmalloc eq data buffer failed, size is %ld\n", __func__, data_size);
		return -ENOMEM;
	}

	ret = vbc_turning_profile_get((u8 *)buf_p, data_size, g_vbc_codec->codec, profile_id);
	if (ret < 0) {
		vfree(buf_p);
		pr_err("%s: Error: get eq to upper layer failed, ret=%d.\n", __func__, ret);
		return ret;
	}

	if (copy_to_user(data_p, buf_p, ret)) {
		pr_err("%s: Error: arg protection error\n", __func__);
		vfree(buf_p);
		return -EACCES;
	}

	vfree(buf_p);
	pr_info("%s: get eq to upper layer success, size %d.\n", __func__, ret);
	return ret;
}

static int vbc_turning_ndp_release(struct inode *inode, struct file *file)
{
    pr_info("%s: Enter\n", __func__);

    return 0;
}

static ssize_t vbc_turning_structure_write(struct file *file, const char __user *data_p, size_t data_size, loff_t *ppos)
{
	return vbc_turning_ndp_write(file, data_p, data_size, ppos, SND_VBC_PROFILE_AUDIO_STRUCTURE);
}

static ssize_t vbc_turning_DSP_write(struct file *file, const char __user *data_p, size_t data_size, loff_t *ppos)
{
	return vbc_turning_ndp_write(file, data_p, data_size, ppos, SND_VBC_PROFILE_DSP);
}

static ssize_t vbc_turning_NXP_write(struct file *file, const char __user *data_p, size_t data_size, loff_t *ppos)
{
	return vbc_turning_ndp_write(file, data_p, data_size, ppos, SND_VBC_PROFILE_NXP);
}

static ssize_t vbc_turning_SMARTPA_write(struct file *file, const char __user *data_p, size_t data_size, loff_t *ppos)
{
	return vbc_turning_ndp_write(file, data_p, data_size, ppos, SND_VBC_PROFILE_IVS_SMARTPA);
}

static ssize_t vbc_turning_structure_read(struct file *file, char __user *data_p, size_t data_size, loff_t *ppos)
{
	return vbc_turning_ndp_read(file, data_p, data_size, ppos, SND_VBC_PROFILE_AUDIO_STRUCTURE);
}

static ssize_t vbc_turning_DSP_read(struct file *file, char __user *data_p, size_t data_size, loff_t *ppos)
{
	return vbc_turning_ndp_read(file, data_p, data_size, ppos, SND_VBC_PROFILE_DSP);
}

static ssize_t vbc_turning_NXP_read(struct file *file, char __user *data_p, size_t data_size, loff_t *ppos)
{
	return vbc_turning_ndp_read(file, data_p, data_size, ppos, SND_VBC_PROFILE_NXP);
}

static ssize_t vbc_turning_SMARTPA_read(struct file *file, char __user *data_p, size_t data_size, loff_t *ppos)
{
	return vbc_turning_ndp_read(file, data_p, data_size, ppos, SND_VBC_PROFILE_IVS_SMARTPA);
}

static const struct file_operations audio_turning_fops[AUD_TURNING_PRO_CNTS] = {
    //SND_VBC_PROFILE_AUDIO_STRUCTURE
    {
    .owner          = THIS_MODULE,
    .open           = vbc_turning_ndp_open,
    .write          = vbc_turning_structure_write,
    .read           = vbc_turning_structure_read,
    .release        = vbc_turning_ndp_release,
    },
    //SND_VBC_PROFILE_DSP
    {
    .owner          = THIS_MODULE,
    .open           = vbc_turning_ndp_open,
    .write          = vbc_turning_DSP_write,
    .read           = vbc_turning_DSP_read,
    .release        = vbc_turning_ndp_release,
    },
    //SND_VBC_PROFILE_NXP
    {
    .owner          = THIS_MODULE,
    .open           = vbc_turning_ndp_open,
    .write          = vbc_turning_NXP_write,
    .read           = vbc_turning_NXP_read,
    .release        = vbc_turning_ndp_release,
    },
    //SND_VBC_PROFILE_IVS_SMARTPA
    {
    .owner          = THIS_MODULE,
    .open           = vbc_turning_ndp_open,
    .write          = vbc_turning_SMARTPA_write,
    .read           = vbc_turning_SMARTPA_read,
    .release        = vbc_turning_ndp_release,
    }
};

struct aud_turning_info {
    u8    ready;
    dev_t devid;
    struct cdev turing_cdev;
    struct class *turing_class;
    struct device *turing_device;
};

struct aud_turning_info aud_turning[AUD_TURNING_PRO_CNTS] = {0};

static void vbc_turning_ndp_exit(void)
{
	u8 index = 0;

	pr_info("%s: Enter.\n", __func__);
	for (index = 0; index < AUD_TURNING_PRO_CNTS; index++) {
		if (aud_turning[index].ready) {
			if (NULL != aud_turning[index].turing_class) {
				device_destroy(aud_turning[index].turing_class, aud_turning[index].devid);
				class_destroy(aud_turning[index].turing_class);
			}
			cdev_del(&aud_turning[index].turing_cdev);
			unregister_chrdev_region(aud_turning[index].devid, 1);
			aud_turning[index].turing_device = NULL;
			aud_turning[index].turing_class = NULL;
			aud_turning[index].ready = 0;
		}
	}
}

static int vbc_turning_ndp_init(void)
{
	int result = 0;
	dev_t devid = 0;
	unsigned int major = 0;
	unsigned int minor = 0;
	u8 index = 0;
	char ndp_name[32];

	result = alloc_chrdev_region(&devid, AUD_TURNING_MINOR_START, AUD_TURNING_PRO_CNTS, AUDIO_TURNING_PATH_BASE);
	if (result < 0) {
		pr_err("%s,alloc_chrdev_region failed! result: %d\n", __func__, result);
		goto INIT_FAILED;
	}
	major = MAJOR(devid);
	minor = MINOR(devid);
	pr_info("%s,alloc dev id 0x%x, major =%d, minor = %d!\n", __func__, devid, major, minor);

	for (index = 0; index < AUD_TURNING_PRO_CNTS; index++) {
		aud_turning[index].devid = MKDEV(major, minor+index);
		pr_info("%s,alloc dev id %d for NO.%d!\n", __func__, aud_turning[index].devid, index);
		cdev_init(&aud_turning[index].turing_cdev, &audio_turning_fops[index]);
		aud_turning[index].turing_cdev.owner = THIS_MODULE;

		result = cdev_add(&aud_turning[index].turing_cdev, aud_turning[index].devid, 1);
		if (result < 0) {
			pr_err("%s,cdev_add failed! result: %d\n", __func__, result);
			cdev_del(&aud_turning[index].turing_cdev);
			unregister_chrdev_region(aud_turning[index].devid, 1);
			goto INIT_FAILED;
		}

		memset(&ndp_name[0], 0, sizeof(char) * 32);
		sprintf(&ndp_name[0], "%s%d", AUDIO_TURNING_PATH_BASE, index);
		pr_info("%s,ndp_name = %s !\n", __func__, ndp_name);
		aud_turning[index].turing_class = class_create(ndp_name);
		if (IS_ERR(aud_turning[index].turing_class)) {
			pr_err("%s, class_create failed!\n", __func__);
			cdev_del(&aud_turning[index].turing_cdev);
			unregister_chrdev_region(aud_turning[index].devid, 1);
			goto INIT_FAILED;
		}

		aud_turning[index].turing_device = device_create(aud_turning[index].turing_class, NULL,
											aud_turning[index].devid, NULL, ndp_name);
		if (IS_ERR(aud_turning[index].turing_device)) {
			pr_err("%s,device_create failed!\n", __func__);
			class_destroy(aud_turning[index].turing_class);
			aud_turning[index].turing_class = NULL;
			cdev_del(&aud_turning[index].turing_cdev);
			unregister_chrdev_region(aud_turning[index].devid, 1);
			goto INIT_FAILED;
		}

		aud_turning[index].ready = 1;
	}

	pr_info("%s: success.\n", __func__);

	return 0;

INIT_FAILED:
	vbc_turning_ndp_exit();
	return result;
}

#endif



#if 0
static int audio_load_firmware_data(struct firmware *fw, char *firmware_path)
{
	int read_len, size, cnt = 0;
	char *buf;
	char *audio_image_buffer;
	int image_size;
	loff_t pos = 0;
	struct file *file;

	if (!firmware_path)
		return -EINVAL;
	pr_info("%s entry, path %s\n", __func__, firmware_path);
	file = filp_open(firmware_path, O_RDONLY, 0);
	if (IS_ERR(file)) {
		pr_err("%s open file %s error, file =%p\n",
		       firmware_path, __func__, file);
		return PTR_ERR(file);
	}
	pr_info("audio %s open image file %s  successfully\n",
		__func__, firmware_path);

	/* read file to buffer */
	image_size = i_size_read(file_inode(file));
	if (image_size <= 0) {
		filp_close(file, NULL);
		pr_err("read size failed");
		return -EINVAL;
	}
	audio_image_buffer = vmalloc(image_size);
	if (!audio_image_buffer) {
		filp_close(file, NULL);
		pr_err("%s no memory\n", __func__);
		return -ENOMEM;
	}
	memset(audio_image_buffer, 0, image_size);
	pr_info("audio_image_buffer=%px\n", audio_image_buffer);
	size = image_size;
	buf = audio_image_buffer;
	do {
		read_len = kernel_read(file, buf, size, &pos);
		if (read_len > 0) {
			size -= read_len;
			buf += read_len;
		} else if (read_len == -EINTR || read_len == -EAGAIN) {
			cnt++;
			pr_warn("%s, read failed,read_len=%d, cnt=%d\n",
				__func__, read_len, cnt);
			if (cnt < 3) {
				msleep(50);
				continue;
			}
		}
	} while (read_len > 0 && size > 0);
	filp_close(file, NULL);
	fw->data = audio_image_buffer;
	fw->size = image_size;
	pr_info("After read, audio_image_buffer=%px, size=%zd, pos:%zd, read_len:%d, finish.\n",
		fw->data, fw->size, (size_t)pos, read_len);

	return 0;
}

static void audio_release_firmware_data(struct firmware *fw)
{
	if (fw->data) {
		vfree(fw->data);
		memset(fw, 0, sizeof(*fw));
		pr_info("%s\n", __func__);
	}
}

#define AUDIO_FIRMWARE_PATH_BASE "/vendor/firmware/"
#endif
int vbc_profile_loading(struct snd_soc_component *codec, int profile_id)
{
	const struct firmware *fw;
	int ret;
	if (profile_id < 0 || profile_id >= SND_VBC_PROFILE_MAX)
		return -EINVAL;
	ret = request_firmware(&fw, vbc_get_profile_name(profile_id), codec->dev);
	if (ret)
		return ret;
	ret = vbc_profile_store(codec, profile_id, fw->data, fw->size);
	release_firmware(fw);
	return ret;
}

static int vbc_profile_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct vbc_profile *p_profile_setting =
		&vbc_codec->vbc_profile_setting;
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int profile_idx = mc->reg - SND_VBC_PROFILE_START;

	ucontrol->value.integer.value[0] =
		p_profile_setting->now_mode[profile_idx];

	return 0;
}

static int vbc_profile_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct soc_mixer_control *mc = (struct soc_mixer_control *)kcontrol->private_value;
	long value = ucontrol->value.integer.value[0];
	if (value < 0 || value > VBC_PROFILE_CNT_MAX)
		return -EINVAL;
	return vbc_profile_try_apply(codec, mc->reg - SND_VBC_PROFILE_START, value);
}

static int vbc_profile_load_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct vbc_profile *p_profile_setting = &vbc_codec->vbc_profile_setting;
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int profile_idx = mc->reg - SND_VBC_PROFILE_START;

	ucontrol->value.integer.value[0] =
		p_profile_setting->is_loading[profile_idx];

	return 0;
}

static int vbc_profile_load_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	int ret;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int profile_idx = mc->reg - SND_VBC_PROFILE_START;

	ret = ucontrol->value.integer.value[0];

	sp_asoc_pr_dbg("%s %s, %s\n",
		       __func__, vbc_get_profile_name(profile_idx),
		       (ret == 1) ? "load" : "idle");
	if (ret == 1)
		ret = vbc_profile_loading(codec, profile_idx);

	return ret;
}

static int vbc_volume_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->volume;

	return 0;
}

static int vbc_volume_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	int value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	value = ucontrol->value.integer.value[0];
	sp_asoc_pr_dbg("%s volume = %d\n",
		       __func__, value);
	vbc_codec->volume = value;
	dsp_vbc_set_volume(vbc_codec->volume);

	return value;
}

static int vbc_reg_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->dsp_reg;

	return 0;
}

static int vbc_reg_put(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	u32 reg;
	u32 value;
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	reg = mc->reg;
	value = ucontrol->value.integer.value[0];
	pr_debug("%s %#x(reg) = %#x(value)\n",
		 __func__, reg, value);
	vbc_codec->dsp_reg = value;

	dsp_vbc_reg_write(reg, value, 0xffffffff);

	return value;
}

static int vbc_get_aud_iis_clock(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->aud_iis0_master_setting;

	return 0;
}

static const char * const iis_master_setting_txt[] = {
	"disable_iis0", "disable_loop", "iis0", "loop",
};

static const struct soc_enum iis_master_setting_enum  =
SPRD_VBC_ENUM(SND_SOC_NOPM, 4, iis_master_setting_txt);

static int vbc_put_aud_iis_clock(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	u32 value;
	int ret;
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec->need_aud_top_clk) {
		pr_debug("%s No need audio top to provide da clock.\n",
			 __func__);
		return 0;
	}

	value = ucontrol->value.enumerated.item[0];
	sp_asoc_pr_dbg("%s, texts->texts[%d] = '%s', texts->items=%d\n",
		       __func__, value, texts->texts[value], texts->items);
	if (value >= texts->items) {
		pr_err("err: %s value(%u) >= items(%u)\n",
		       __func__, value, texts->items);
		return -1;
	}

	ret = aud_dig_iis_master(codec->card,
				 value);
	if (ret < 0) {
		pr_err("%s failed. value: %u ret = %d\n", __func__, value, ret);
		return -1;
	}
	vbc_codec->aud_iis0_master_setting = value;

	return value;
}

static int vbc_loopback_loop_mode_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->loopback.loop_mode;

	return 0;
}

static int vbc_loopback_loop_mode_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	vbc_codec->loopback.loop_mode = value;
	dsp_vbc_loopback_set(&vbc_codec->loopback);

	return value;
}

static int vbc_loopback_type_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->loopback.loopback_type;

	return 0;
}

static int vbc_loopback_type_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	int value;
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}

	value = ucontrol->value.enumerated.item[0];
	sp_asoc_pr_dbg("%s, texts->texts[%d] =%s\n",
		       __func__, value, texts->texts[value]);
	vbc_codec->loopback.loopback_type = value;
	dsp_vbc_loopback_set(&vbc_codec->loopback);

	return value;
}

static int vbc_loopback_voice_fmt_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->loopback.voice_fmt;

	return 0;
}

static int vbc_loopback_voice_fmt_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	int value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	value = ucontrol->value.integer.value[0];
	sp_asoc_pr_dbg("%s, value=%d\n", __func__, value);
	vbc_codec->loopback.voice_fmt = value;
	dsp_vbc_loopback_set(&vbc_codec->loopback);

	return value;
}

static int vbc_loopback_amr_rate_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->loopback.amr_rate;

	return 0;
}

static int vbc_loopback_amr_rate_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	int value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	value = ucontrol->value.enumerated.item[0];
	sp_asoc_pr_dbg("%s, value=%d\n", __func__, value);
	vbc_codec->loopback.amr_rate = value;
	dsp_vbc_loopback_set(&vbc_codec->loopback);

	return value;
}

static int vbc_call_mute_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	ucontrol->value.integer.value[0] = vbc_codec->vbc_call_mute[id].mute;

	return 0;
}

static int sys_iis_sel_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 id = e->reg;

	ucontrol->value.integer.value[0] = vbc_codec->sys_iis_sel[id];

	return 0;
}

static const char * const sys_iis_sel_txt[] = {
	"vbc_iis0", "vbc_iis1", "vbc_iis2", "vbc_iis3", "vbc_iism0", "ap_iis0",
	"audcp_iis0", "audcp_iis1", "audcp_tdm3", "audcp_tdm4"
};

static const struct soc_enum
vbc_sys_iis_enum[SYS_IIS_MAX] = {
	SPRD_VBC_ENUM(SYS_IIS0, 10, sys_iis_sel_txt),
	SPRD_VBC_ENUM(SYS_IIS1, 10, sys_iis_sel_txt),
	SPRD_VBC_ENUM(SYS_IIS2, 10, sys_iis_sel_txt),
	SPRD_VBC_ENUM(SYS_IIS3, 10, sys_iis_sel_txt),
	SPRD_VBC_ENUM(SYS_IIS4, 10, sys_iis_sel_txt),
	SPRD_VBC_ENUM(SYS_IIS5, 10, sys_iis_sel_txt),
	SPRD_VBC_ENUM(SYS_IIS6, 10, sys_iis_sel_txt),
};

static int sys_iis_sel_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	u16 value;
	int ret;
	struct pinctrl_state *state;
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	char buf[128] = {0};
	u32 id = e->reg;

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}

	value = ucontrol->value.enumerated.item[0];
	sp_asoc_pr_dbg("%s, id=%d,value=%d, texts->texts[] =%s\n",
		       __func__, id, value, texts->texts[value]);
	vbc_codec->sys_iis_sel[id] = value;
	sprintf(buf, "%s_%u", sys_iis_sel_txt[value], id);
	state = pinctrl_lookup_state(vbc_codec->pctrl, buf);
	if (IS_ERR(state)) {
		pr_err("%s line=%d failed\n", __func__, __LINE__);
		return -EINVAL;
	}
	ret =  pinctrl_select_state(vbc_codec->pctrl, state);
	if (ret != 0)
		pr_err("%s failed ret = %d\n", __func__, ret);

	sp_asoc_pr_dbg("%s,soc iis%d -> %s\n", __func__, id, buf);

	return true;
}

static int vbc_get_ag_iis_ext_sel(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 ag_iis_num = e->reg;

	ucontrol->value.integer.value[0] =
		vbc_codec->ag_iis_ext_sel[ag_iis_num];

	return 0;
}

static int vbc_put_ag_iis_ext_sel(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	u16 enable;
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 ag_iis_num = e->reg;

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}

	enable = ucontrol->value.enumerated.item[0];
	sp_asoc_pr_dbg("%s, ag_iis_num=%d,value=%d, texts->texts[] =%s\n",
		       __func__, ag_iis_num, enable, texts->texts[enable]);
	arch_audio_iis_to_audio_top_enable(ag_iis_num, enable);
	vbc_codec->ag_iis_ext_sel[ag_iis_num] = enable;

	return true;
}

static int vbc_get_ag_iis_ext_sel_v1(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 ag_iis_num = e->reg;

	ucontrol->value.integer.value[0] =
		vbc_codec->ag_iis_ext_sel_v1[ag_iis_num];

	return 0;
}

static int vbc_put_ag_iis_ext_sel_v1(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	u16 mode;
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 ag_iis_num = e->reg;

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}

	mode = ucontrol->value.enumerated.item[0];
	sp_asoc_pr_dbg("%s, ag_iis_num=%d,value=%d, texts->texts[] =%s\n",
		       __func__, ag_iis_num, mode, texts->texts[mode]);
	arch_audio_iis_to_audio_top_enable_v1(ag_iis_num, mode);
	vbc_codec->ag_iis_ext_sel_v1[ag_iis_num] = mode;

	return true;
}

static int vbc_get_ag_iis_ext_sel_v2(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 ag_iis_num = e->reg;

	ucontrol->value.integer.value[0] =
		vbc_codec->ag_iis_ext_sel_v2[ag_iis_num];

	return 0;
}

static int vbc_put_ag_iis_ext_sel_v2(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	u16 mode;
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u32 ag_iis_num = e->reg;

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}

	mode = ucontrol->value.enumerated.item[0];
	sp_asoc_pr_dbg("%s ag_iis_num %d, value %d, texts->texts[] %s\n",
		       __func__, ag_iis_num, mode, texts->texts[mode]);
	arch_audio_iis_to_audio_top_enable_v2(ag_iis_num, mode);
	vbc_codec->ag_iis_ext_sel_v2[ag_iis_num] = mode;

	return true;
}

static int vbc_get_agdsp_access(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	u16 enable;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	enable = ucontrol->value.enumerated.item[0];
	mutex_lock(&vbc_codec->agcp_access_mutex);
	ucontrol->value.integer.value[0] = vbc_codec->agcp_access_enable;
	mutex_unlock(&vbc_codec->agcp_access_mutex);

	return true;
}

static int vbc_put_agdsp_aud_access(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	u16 enable;
	int ret = true;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	enable = ucontrol->value.enumerated.item[0];

	pr_info("%s agcp_access_aud_cnt = %d, agcp_access_a2dp_cnt = %d, en = %s\n",
		__func__, vbc_codec->agcp_access_aud_cnt, vbc_codec->agcp_access_a2dp_cnt,
		enable ? "enable" : "disable");
	mutex_lock(&vbc_codec->agcp_access_mutex);
	if (enable) {
		if (vbc_codec->agcp_access_aud_cnt == 0 &&
		    vbc_codec->agcp_access_a2dp_cnt == 0) {
			vbc_codec->agcp_access_aud_cnt++;
			pr_info("audio hal agdsp_access_enable\n");
			ret = agdsp_access_enable();
			if (ret)
				pr_err("agdsp_access_enable error:%d\n", ret);
			else
				vbc_codec->agcp_access_enable = 1;
		}
	} else {
		if (vbc_codec->agcp_access_aud_cnt != 0) {
			vbc_codec->agcp_access_aud_cnt = 0;
			if (vbc_codec->agcp_access_a2dp_cnt == 0) {
				pr_info("audio hal agdsp_access_disable\n");
				agdsp_access_disable();
				vbc_codec->agcp_access_enable = 0;
			}
		}
	}
	mutex_unlock(&vbc_codec->agcp_access_mutex);

	return ret;
}

static int vbc_put_agdsp_a2dp_access(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	u16 enable;
	int ret = true;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	enable = ucontrol->value.enumerated.item[0];

	pr_info("%s agcp_access_aud_cnt = %d, agcp_access_a2dp_cnt = %d\n",
		__func__, vbc_codec->agcp_access_aud_cnt,
		vbc_codec->agcp_access_a2dp_cnt);
	mutex_lock(&vbc_codec->agcp_access_mutex);
	if (enable) {
		if (vbc_codec->agcp_access_a2dp_cnt == 0 &&
		    vbc_codec->agcp_access_aud_cnt == 0) {
			vbc_codec->agcp_access_a2dp_cnt++;
			ret = agdsp_access_enable();
			if (ret)
				pr_err("agdsp_access_enable error:%d\n", ret);
			else
				vbc_codec->agcp_access_enable = 1;
		}
	} else {
		if (vbc_codec->agcp_access_a2dp_cnt != 0) {
			vbc_codec->agcp_access_a2dp_cnt = 0;
			if (vbc_codec->agcp_access_aud_cnt == 0) {
				pr_info("audio hal agdsp_access_disable\n");
				agdsp_access_disable();
				vbc_codec->agcp_access_enable = 0;
			}
		}
	}
	mutex_unlock(&vbc_codec->agcp_access_mutex);

	return ret;
}

int sbc_paras_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_bytes_ext *params = (void *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	int len;
	int size;

	size = sizeof(struct sbcenc_param_t);
	len = params->max * sizeof(ucontrol->value.bytes.data[0]);
	if (size > len) {
		pr_err("%s size > len\n", __func__);
		return -EINVAL;
	}
	memcpy(ucontrol->value.bytes.data, &vbc_codec->sbcenc_para, size);

	return 0;
}

/* not send to dsp, dsp only use at startup */
int sbc_paras_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_bytes_ext *params = (void *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	int len;
	int size;

	size = sizeof(struct sbcenc_param_t);
	len = params->max * sizeof(ucontrol->value.bytes.data[0]);
	if (size > len) {
		pr_err("%s size > len\n", __func__);
		return -EINVAL;
	}
	memcpy(&vbc_codec->sbcenc_para, ucontrol->value.bytes.data, size);
	pr_info("%s sbc para %u, %u, %u, %u, %u, %u, %u\n",
		__func__, vbc_codec->sbcenc_para.SBCENC_Mode,
		vbc_codec->sbcenc_para.SBCENC_Blocks,
		vbc_codec->sbcenc_para.SBCENC_SubBands,
		vbc_codec->sbcenc_para.SBCENC_SamplingFreq,
		vbc_codec->sbcenc_para.SBCENC_AllocMethod,
		vbc_codec->sbcenc_para.SBCENC_min_Bitpool,
		vbc_codec->sbcenc_para.SBCENC_max_Bitpool);

	return 0;
}

static const char * const vbc_dump_pos_txt[DUMP_POS_MAX] = {
	[DUMP_POS_DAC0_E] = TO_STRING(DUMP_POS_DAC0_E),
	[DUMP_POS_DAC1_E] = TO_STRING(DUMP_POS_DAC1_E),
	[DUMP_POS_A4] = TO_STRING(DUMP_POS_A4),
	[DUMP_POS_A3] = TO_STRING(DUMP_POS_A3),
	[DUMP_POS_A2] = TO_STRING(DUMP_POS_A2),
	[DUMP_POS_A1] = TO_STRING(DUMP_POS_A1),
	[DUMP_POS_V2] = TO_STRING(DUMP_POS_V2),
	[DUMP_POS_V1] = TO_STRING(DUMP_POS_V1),
	[DUMP_POS_DAC0_TO_ADC1] = TO_STRING(DUMP_POS_DAC0_TO_ADC1),
	[DUMP_POS_DAC0_TO_ADC2] = TO_STRING(DUMP_POS_DAC0_TO_ADC2),
	[DUMP_POS_DAC0_TO_ADC3] = TO_STRING(DUMP_POS_DAC0_TO_ADC3),
};

static const struct soc_enum vbc_dump_pos_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0, DUMP_POS_MAX,
	vbc_dump_pos_txt);

static const char *vbc_dumppos2name(int pos)
{
	const char * const vbc_dumppos_name[DUMP_POS_MAX] = {
		[DUMP_POS_DAC0_E] = TO_STRING(DUMP_POS_DAC0_E),
		[DUMP_POS_DAC1_E] = TO_STRING(DUMP_POS_DAC1_E),
		[DUMP_POS_A4] = TO_STRING(DUMP_POS_A4),
		[DUMP_POS_A3] = TO_STRING(DUMP_POS_A3),
		[DUMP_POS_A2] = TO_STRING(DUMP_POS_A2),
		[DUMP_POS_A1] = TO_STRING(DUMP_POS_A1),
		[DUMP_POS_V2] = TO_STRING(DUMP_POS_V2),
		[DUMP_POS_V1] = TO_STRING(DUMP_POS_V1),
		[DUMP_POS_DAC0_TO_ADC1] = TO_STRING(DUMP_POS_DAC0_TO_ADC1),
		[DUMP_POS_DAC0_TO_ADC2] = TO_STRING(DUMP_POS_DAC0_TO_ADC2),
		[DUMP_POS_DAC0_TO_ADC3] = TO_STRING(DUMP_POS_DAC0_TO_ADC3),
	};

	if (pos >= DUMP_POS_MAX) {
		pr_err("invalid id %s %d\n", __func__, pos);
		return "";
	}
	if (!vbc_dumppos_name[pos]) {
		pr_err("null string =%d\n", pos);
		return "";
	}

	return vbc_dumppos_name[pos];
}

static int vbc_get_dump_pos(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->vbc_dump_position;

	return 0;
}

static int vbc_put_dump_pos(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	int val;

	val = ucontrol->value.integer.value[0];
	if (val >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}

	sp_asoc_pr_dbg("%s %s -> %s, vbc_dump_position %s\n", __func__,
		       vbc_dumppos2name(val), texts->texts[val],
		       vbc_dumppos2name(vbc_codec->vbc_dump_position_cmd));
	vbc_codec->vbc_dump_position = val;

	return 0;
}

static int vbc_get_dump_pos_cmd(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->vbc_dump_position_cmd;

	return 0;
}

static int vbc_put_dump_pos_cmd(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	int val = ucontrol->value.integer.value[0];

	if (val >= texts->items) {
		pr_err("put_dump_pos_cmd index outof bounds error\n");
		return -EINVAL;
	}

	vbc_codec->vbc_dump_position_cmd = val;
	sp_asoc_pr_dbg("%s -> %s, vbc_dump_position %s\n",
		       vbc_dumppos2name(val), texts->texts[val],
		       vbc_dumppos2name(vbc_codec->vbc_dump_position));
	scene_dump_set(vbc_codec->vbc_dump_position_cmd);

	return 0;
}

/* VBC IIS PINMUX FOR USB */
static const char * const vbc_iis_inf_sys_sel_txt[] = {
	"vbc_iis_to_pad", "vbc_iis_to_aon_usb",
};

static const struct soc_enum
vbc_iis_inf_sys_sel_enum = SPRD_VBC_ENUM(SND_SOC_NOPM, 2,
	vbc_iis_inf_sys_sel_txt);

static int vbc_iis_inf_sys_sel_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->vbc_iis_inf_sys_sel;

	return 0;
}

/*
 * vbc iis2 to inf sys iis2, you can extension it by soc_enum->reg.
 */
static int vbc_iis_inf_sys_sel_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	u16 value;
	int ret;
	struct pinctrl_state *state;
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	char buf[128] = {0};

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}

	value = ucontrol->value.enumerated.item[0];
	pr_info("%s, value=%d (%s)\n",
		__func__, value, texts->texts[value]);
	vbc_codec->vbc_iis_inf_sys_sel = value;
	sprintf(buf, "%s", vbc_iis_inf_sys_sel_txt[value]);
	state = pinctrl_lookup_state(vbc_codec->pctrl, buf);
	if (IS_ERR(state)) {
		pr_err("%s lookup pin control failed\n", __func__);
		return -EINVAL;
	}
	ret =  pinctrl_select_state(vbc_codec->pctrl, state);
	if (ret != 0)
		pr_err("%s pin contrl select failed %d\n", __func__, ret);


	return 0;
}

static int vbc_voice_capture_type_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->voice_capture_type;

	return 0;
}

static int vbc_voice_capture_type_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	int value;
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}

	value = ucontrol->value.enumerated.item[0];
	sp_asoc_pr_dbg("%s, texts->texts[%d] =%s\n",
		       __func__, value, texts->texts[value]);
	vbc_codec->voice_capture_type = value;

	return value;
}

static int vbc_voice_pcm_play_mode_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->voice_pcm_play_mode;

	return 0;
}

static int vbc_voice_pcm_play_mode_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	int value;
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("ERR: %s,index outof bounds error\n", __func__);
		return -EINVAL;
	}

	value = ucontrol->value.enumerated.item[0];
	sp_asoc_pr_dbg("%s, texts->texts[%d] =%s\n",
		       __func__, value, texts->texts[value]);
	vbc_codec->voice_pcm_play_mode = value;

	return value;
}

static const char * const vad_din_ad_sel_txt[] = {
	"vad_din_sel_add0", "vad_din_sel_add1",
};

static const struct soc_enum
	vad_din_ad_sel_enum = SPRD_VBC_ENUM(SND_SOC_NOPM, 2,
					    vad_din_ad_sel_txt);

static int vad_din_ad_sel_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->vad_din_ad_sel;

	return 0;
}

static int vad_din_ad_sel_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *texts = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct pinctrl_state *state;
	char buf[128] = {0};
	u16 value;
	int ret;

	if (ucontrol->value.integer.value[0] >= texts->items) {
		pr_err("%s, index outof bounds error\n", __func__);
		return -EINVAL;
	}

	value = ucontrol->value.enumerated.item[0];
	vbc_codec->vad_din_ad_sel = value;
	sprintf(buf, "%s", vad_din_ad_sel_txt[value]);
	pr_info("%s, (value %d, %s), %s\n", __func__, value,
		texts->texts[value], buf);
	state = pinctrl_lookup_state(vbc_codec->pctrl, buf);
	if (IS_ERR(state)) {
		pr_err("%s lookup pin control failed\n", __func__);
		return -EINVAL;
	}

	ret =  pinctrl_select_state(vbc_codec->pctrl, state);
	if (ret != 0)
		pr_err("%s pin contrl select failed %d\n", __func__, ret);

	return 0;
}


static int dsp_hp_crosstalk_en_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->hp_crosstalk_en;

	return 0;
}

static int dsp_hp_crosstalk_en_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	u32 enable;
	int ret;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	enable = ucontrol->value.enumerated.item[0];
	pr_info("%s enable = %d\n", __func__, enable);

	vbc_codec->hp_crosstalk_en = enable;
	ret = dsp_hp_crosstalk_en_set(enable);
	if (ret != 0)
		pr_err("%s set hp_crosstalk_en failed %d\n", __func__, ret);
	return 0;
}

static int dsp_hp_crosstalk_gain_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->hp_crosstalk_gain.gain0;
	ucontrol->value.integer.value[1] = vbc_codec->hp_crosstalk_gain.gain1;

	return 0;
}

static int dsp_hp_crosstalk_gain_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	int val0, val1, ret;

	val0 = ucontrol->value.integer.value[0];
	val1 = ucontrol->value.integer.value[1];
	sp_asoc_pr_dbg("%s, hp_crosstalk_gain, gain0=%d, gain1=%d\n",
		       __func__, val0, val1);
	vbc_codec->hp_crosstalk_gain.gain0 = val0;
	vbc_codec->hp_crosstalk_gain.gain1 = val1;

	ret = dsp_hp_crosstalk_gain(val0, val1);
	if (ret != 0)
		pr_err("%s set hp_crosstalk_gain failed %d\n", __func__, ret);
	return 0;
}

static int dsp_audio_zoom_st_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->audio_zoom_st;
	pr_info("%s value = %ld\n", __func__, ucontrol->value.integer.value[0]);

	return 0;
}

static int dsp_audio_zoom_st_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	u32 value;
	int ret;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	value = ucontrol->value.enumerated.item[0];
	pr_info("%s value = %d\n", __func__, value);

	vbc_codec->audio_zoom_st = value;
	ret = dsp_audio_zoom_st_set(value);
	if (ret != 0)
		pr_err("%s set audio_zoom_st failed %d\n", __func__, ret);
	return 0;
}

static int dsp_audio_zoom_ratio_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = vbc_codec->audio_zoom_ratio;
	pr_info("%s value = %ld\n", __func__, ucontrol->value.integer.value[0]);

	return 0;
}

static int dsp_audio_zoom_ratio_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	int ret, value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);

	value = ucontrol->value.integer.value[0];
	pr_info("%s value = %d\n", __func__, value);

	vbc_codec->audio_zoom_ratio = value;
	ret = dsp_audio_zoom_ratio_set(value);
	if (ret != 0)
		pr_err("%s set audio_zoom_ratio failed %d\n", __func__, ret);
	return 0;
}

static int dsp_audio_zoom_focus_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct soc_bytes_ext *params = (void *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	int len, size;

	size = sizeof(struct audio_zoom_focus_t);
	len = params->max * sizeof(ucontrol->value.bytes.data[0]);
	if (size > len) {
		pr_err("%s size > len\n", __func__);
		return -EINVAL;
	}
	memcpy(ucontrol->value.bytes.data, &vbc_codec->audio_zoom_focus, size);

	return 0;
}

static int dsp_audio_zoom_focus_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct soc_bytes_ext *params = (void *)kcontrol->private_value;
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	int type, x, y, width, height;
	int len, size;
	int ret;

	size = sizeof(struct audio_zoom_focus_t);
	len = params->max * sizeof(ucontrol->value.bytes.data[0]);
	if (size > len) {
		pr_err("%s size > len\n", __func__);
		return -EINVAL;
	}
	memcpy(&vbc_codec->audio_zoom_focus, ucontrol->value.bytes.data, size);

	type = vbc_codec->audio_zoom_focus.type;
	x = vbc_codec->audio_zoom_focus.x;
	y = vbc_codec->audio_zoom_focus.y;
	width = vbc_codec->audio_zoom_focus.width;
	height = vbc_codec->audio_zoom_focus.height;

	pr_info("%s, audio_zoom_focus, type=%d, x=%d, y=%d, width=%d, height=%d\n",
			__func__, type, x, y, width, height);

	ret = dsp_audio_zoom_focus_set(type, x, y, width, height);
	if (ret != 0)
		pr_err("%s set audio_zoom_focus failed %d\n", __func__, ret);

	return 0;
}

/* -9450dB to 0dB in 150dB steps ( mute instead of -9450dB) */
static const DECLARE_TLV_DB_SCALE(mdg_tlv, -9450, 150, 1);
static const DECLARE_TLV_DB_SCALE(dg_tlv, -9450, 150, 1);
static const DECLARE_TLV_DB_SCALE(smthdg_tlv, -9450, 150, 1);
static const DECLARE_TLV_DB_SCALE(smthdg_step_tlv, -9450, 150, 1);
static const DECLARE_TLV_DB_SCALE(mixerdg_tlv, -9450, 150, 1);
static const DECLARE_TLV_DB_SCALE(mixerdg_step_tlv, -9450, 150, 1);
static const DECLARE_TLV_DB_SCALE(offload_dg_tlv, 0, 150, 1);
static const DECLARE_TLV_DB_SCALE(hpg_tlv, -9450, 150, 0);

static const struct snd_kcontrol_new vbc_codec_snd_controls[] = {
	/* MDG */
	SOC_DOUBLE_R_EXT_TLV("VBC DAC0 DSP MDG Set",
			     0, 1, VBC_MDG_DAC0_DSP, MDG_STP_MAX_VAL, 0,
			     vbc_mdg_get,
			     vbc_mdg_put, mdg_tlv),
	SOC_DOUBLE_R_EXT_TLV("VBC DAC1 DSP MDG Set",
			     0, 1, VBC_MDG_DAC1_DSP, MDG_STP_MAX_VAL, 0,
			     vbc_mdg_get,
			     vbc_mdg_put, mdg_tlv),
	SOC_DOUBLE_R_EXT_TLV("VBC DAC0 AUD MDG Set",
			     0, 1, VBC_MDG_AP01, MDG_STP_MAX_VAL, 0,
			     vbc_mdg_get,
			     vbc_mdg_put, mdg_tlv),
	SOC_DOUBLE_R_EXT_TLV("VBC DAC0 AUD23 MDG Set",
			     0, 1, VBC_MDG_AP23, MDG_STP_MAX_VAL, 0,
			     vbc_mdg_get,
			     vbc_mdg_put, mdg_tlv),
	/* SRC */
	SOC_SINGLE_EXT("VBC_SRC_DAC0", SND_SOC_NOPM,
		       VBC_SRC_DAC0, SRC_MAX_VAL, 0,
		       vbc_src_get, vbc_src_put),
	SOC_SINGLE_EXT("VBC_SRC_DAC1", SND_SOC_NOPM,
		       VBC_SRC_DAC1, SRC_MAX_VAL, 0,
		       vbc_src_get, vbc_src_put),
	SOC_SINGLE_EXT("VBC_SRC_ADC0", SND_SOC_NOPM,
		       VBC_SRC_ADC0, SRC_MAX_VAL, 0,
		       vbc_src_get, vbc_src_put),
	SOC_SINGLE_EXT("VBC_SRC_ADC1", SND_SOC_NOPM,
		       VBC_SRC_ADC1, SRC_MAX_VAL, 0,
		       vbc_src_get, vbc_src_put),
	SOC_SINGLE_EXT("VBC_SRC_ADC2", SND_SOC_NOPM,
		       VBC_SRC_ADC2, SRC_MAX_VAL, 0,
		       vbc_src_get, vbc_src_put),
	SOC_SINGLE_EXT("VBC_SRC_ADC3", SND_SOC_NOPM,
		       VBC_SRC_ADC3, SRC_MAX_VAL, 0,
		       vbc_src_get, vbc_src_put),
	SOC_SINGLE_EXT("VBC_SRC_BT_DAC", SND_SOC_NOPM,
		       VBC_SRC_BT_DAC, SRC_MAX_VAL, 0,
		       vbc_src_get, vbc_src_put),
	SOC_SINGLE_EXT("VBC_SRC_BT_ADC", SND_SOC_NOPM,
		       VBC_SRC_BT_ADC, SRC_MAX_VAL, 0,
		       vbc_src_get, vbc_src_put),
	SOC_SINGLE_EXT("VBC_SRC_FM", SND_SOC_NOPM,
		       VBC_SRC_FM, SRC_MAX_VAL, 0,
		       vbc_src_get, vbc_src_put),
	/* DG */
	SOC_DOUBLE_R_EXT_TLV("VBC DAC0 DG Set",
			     0, 1, VBC_DG_DAC0, DG_MAX_VAL, 0,
			     vbc_dg_get,
			     vbc_dg_put, dg_tlv),
	SOC_DOUBLE_R_EXT_TLV("VBC DAC1 DG Set",
			     0, 1, VBC_DG_DAC1, DG_MAX_VAL, 0,
			     vbc_dg_get,
			     vbc_dg_put, dg_tlv),
	SOC_DOUBLE_R_EXT_TLV("VBC ADC0 DG Set",
			     0, 1, VBC_DG_ADC0, DG_MAX_VAL, 0,
			     vbc_dg_get,
			     vbc_dg_put, dg_tlv),
	SOC_DOUBLE_R_EXT_TLV("VBC ADC1 DG Set",
			     0, 1, VBC_DG_ADC1, DG_MAX_VAL, 0,
			     vbc_dg_get,
			     vbc_dg_put, dg_tlv),
	SOC_DOUBLE_R_EXT_TLV("VBC ADC2 DG Set",
			     0, 1, VBC_DG_ADC2, DG_MAX_VAL, 0,
			     vbc_dg_get,
			     vbc_dg_put, dg_tlv),
	SOC_DOUBLE_R_EXT_TLV("VBC ADC3 DG Set",
			     0, 1, VBC_DG_ADC3, DG_MAX_VAL, 0,
			     vbc_dg_get,
			     vbc_dg_put, dg_tlv),
	SOC_DOUBLE_R_EXT_TLV("VBC FM DG Set",
			     0, 1, VBC_DG_FM, DG_MAX_VAL, 0,
			     vbc_dg_get,
			     vbc_dg_put, dg_tlv),
	SOC_DOUBLE_R_EXT_TLV("VBC ST DG Set",
			     0, 1, VBC_DG_ST, DG_MAX_VAL, 0,
			     vbc_dg_get,
			     vbc_dg_put, dg_tlv),
	SOC_DOUBLE_R_EXT_TLV("OFFLOAD DG Set",
			     0, 1, OFFLOAD_DG, OFFLOAD_DG_MAX, 0,
			     vbc_dg_get,
			     vbc_dg_put, offload_dg_tlv),
	/* SMTHDG */
	SOC_DOUBLE_R_EXT_TLV("VBC_SMTHDG_DAC0",
			     0, 1, VBC_SMTHDG_DAC0, SMTHDG_MAX_VAL, 0,
			     vbc_smthdg_get,
			     vbc_smthdg_put, smthdg_tlv),
	SOC_SINGLE_EXT_TLV("VBC_SMTHDG_DAC0_STEP",
			   0, VBC_SMTHDG_DAC0, SMTHDG_STEP_MAX_VAL, 0,
			   vbc_smthdg_step_get,
			   vbc_smthdg_step_put, smthdg_step_tlv),
	/* MIXERDG */
	SOC_DOUBLE_R_EXT_TLV("VBC_MIXERDG_DAC0_MAIN",
			     0, 1, VBC_MIXERDG_DAC0, MIXERDG_MAX_VAL, 0,
			     vbc_mixerdg_mainpath_get,
			     vbc_mixerdg_mainpath_put, mixerdg_tlv),
	SOC_DOUBLE_R_EXT_TLV("VBC_MIXERDG_DAC0_MIX",
			     0, 1, VBC_MIXERDG_DAC0, MIXERDG_MAX_VAL, 0,
			     vbc_mixerdg_mixpath_get,
			     vbc_mixerdg_mixpath_put, mixerdg_tlv),
	SOC_DOUBLE_R_EXT_TLV("VBC_MIXERDG_DAC1_MAIN",
			     0, 1, VBC_MIXERDG_DAC1, MIXERDG_MAX_VAL, 0,
			     vbc_mixerdg_mainpath_get,
			     vbc_mixerdg_mainpath_put, mixerdg_tlv),
	SOC_DOUBLE_R_EXT_TLV("VBC_MIXERDG_DAC1_MIX",
			     0, 1, VBC_MIXERDG_DAC1, MIXERDG_MAX_VAL, 0,
			     vbc_mixerdg_mixpath_get,
			     vbc_mixerdg_mixpath_put, mixerdg_tlv),
	SOC_SINGLE_EXT_TLV("VBC_MIXERDG_STEP",
			   SND_SOC_NOPM,
			   0, MIXERDG_STP_MAX_VAL, 0,
			   vbc_mixerdg_step_get,
			   vbc_mixerdg_step_put, mixerdg_step_tlv),
	/* MIXER */
	SOC_ENUM_EXT("VBC_MIXER0_DAC0",
		     vbc_mixer_enum[VBC_MIXER0_DAC0],
		     vbc_get_mixer_ops,
		     vbc_put_mixer_ops),
	SOC_ENUM_EXT("VBC_MIXER1_DAC0",
		     vbc_mixer_enum[VBC_MIXER1_DAC0],
		     vbc_get_mixer_ops,
		     vbc_put_mixer_ops),
	SOC_ENUM_EXT("VBC_MIXER0_DAC1",
		     vbc_mixer_enum[VBC_MIXER0_DAC1],
		     vbc_get_mixer_ops,
		     vbc_put_mixer_ops),
	SOC_ENUM_EXT("VBC_MIXER_ST",
		     vbc_mixer_enum[VBC_MIXER_ST],
		     vbc_get_mixer_ops,
		     vbc_put_mixer_ops),
	SOC_ENUM_EXT("VBC_MIXER_FM",
		     vbc_mixer_enum[VBC_MIXER_FM],
		     vbc_get_mixer_ops,
		     vbc_put_mixer_ops),
	/* MUX */
	SOC_ENUM_EXT("VBC_MUX_ADC0_SOURCE",
		     vbc_mux_adc_source_enum[VBC_MUX_ADC0_SOURCE],
		     vbc_mux_adc_source_get, vbc_mux_adc_source_put),
	SOC_ENUM_EXT("VBC_MUX_ADC1_SOURCE",
		     vbc_mux_adc_source_enum[VBC_MUX_ADC1_SOURCE],
		     vbc_mux_adc_source_get, vbc_mux_adc_source_put),
	SOC_ENUM_EXT("VBC_MUX_ADC2_SOURCE",
		     vbc_mux_adc_source_enum[VBC_MUX_ADC2_SOURCE],
		     vbc_mux_adc_source_get, vbc_mux_adc_source_put),
	SOC_ENUM_EXT("VBC_MUX_ADC3_SOURCE",
		     vbc_mux_adc_source_enum[VBC_MUX_ADC3_SOURCE],
		     vbc_mux_adc_source_get, vbc_mux_adc_source_put),
	SOC_ENUM_EXT("VBC_MUX_DAC0_OUT_SEL",
		     vbc_mux_dac_out_enum[VBC_MUX_DAC0_OUT_SEL],
		     vbc_mux_dac_out_get, vbc_mux_dac_out_put),
	SOC_ENUM_EXT("VBC_MUX_DAC1_OUT_SEL",
		     vbc_mux_dac_out_enum[VBC_MUX_DAC1_OUT_SEL],
		     vbc_mux_dac_out_get, vbc_mux_dac_out_put),
	SOC_ENUM_EXT("VBC_MUX_ADC0",
		     vbc_mux_adc_enum[VBC_MUX_IN_ADC0],
		     vbc_mux_adc_get, vbc_mux_adc_put),
	SOC_ENUM_EXT("VBC_MUX_ADC1",
		     vbc_mux_adc_enum[VBC_MUX_IN_ADC1],
		     vbc_mux_adc_get, vbc_mux_adc_put),
	SOC_ENUM_EXT("VBC_MUX_ADC2",
		     vbc_mux_adc_enum[VBC_MUX_IN_ADC2],
		     vbc_mux_adc_get, vbc_mux_adc_put),
	SOC_ENUM_EXT("VBC_MUX_ADC3",
		     vbc_mux_adc_enum[VBC_MUX_IN_ADC3],
		     vbc_mux_adc_get, vbc_mux_adc_put),
	SOC_ENUM_EXT("VBC_MUX_FM",
		     vbc_mux_fm_enum[VBC_FM_MUX],
		     vbc_mux_fm_get, vbc_mux_fm_put),
	SOC_ENUM_EXT("VBC_MUX_ST",
		     vbc_mux_st_enum[VBC_ST_MUX],
		     vbc_mux_st_get, vbc_mux_st_put),
	SOC_ENUM_EXT("VBC_MUX_LOOP_DAC0",
		     vbc_mux_loop_da0_enum[VBC_MUX_LOOP_DAC0],
		     vbc_mux_loop_da0_get, vbc_mux_loop_da0_put),
	SOC_ENUM_EXT("VBC_MUX_LOOP_DAC1",
		     vbc_mux_loop_da1_enum[VBC_MUX_LOOP_DAC1],
		     vbc_mux_loop_da1_get, vbc_mux_loop_da1_put),
	SOC_ENUM_EXT("VBC_MUX_LOOP_DAC0_DAC1",
		     vbc_mux_loop_da0_da1_enum[VBC_MUX_LOOP_DAC0_DAC1],
		     vbc_mux_loop_da0_da1_get, vbc_mux_loop_da0_da1_put),
	SOC_ENUM_EXT("VBC_MUX_AUDRCD01",
		     vbc_mux_audrcd_enum[VBC_MUX_AUDRCD01],
		     vbc_mux_audrcd_get, vbc_mux_audrcd_put),
	SOC_ENUM_EXT("VBC_MUX_AUDRCD23",
		     vbc_mux_audrcd_enum[VBC_MUX_AUDRCD23],
		     vbc_mux_audrcd_get, vbc_mux_audrcd_put),
	SOC_ENUM_EXT("VBC_MUX_TDM_AUDRCD23",
		     vbc_mux_tdm_audrcd23_enum[VBC_MUX_TDM_AUDRCD23],
		     vbc_mux_tdm_audrcd23_get, vbc_mux_tdm_audrcd23_put),
	SOC_ENUM_EXT("VBC_MUX_AP01_DSP_PLY",
		     vbc_mux_ap01_dsp_enum[VBC_MUX_AP01_DSP_PLY],
		     vbc_mux_ap01_dsp_get, vbc_mux_ap01_dsp_put),
	SOC_ENUM_EXT("VBC_MUX_AP01_DSP_RCD",
		     vbc_mux_ap01_dsp_enum[VBC_MUX_AP01_DSP_RCD],
		     vbc_mux_ap01_dsp_get, vbc_mux_ap01_dsp_put),
	SOC_ENUM_EXT("VBC_MUX_DAC0_IIS_PORT_SEL",
		     vbc_mux_iis_tx_enum[VBC_MUX_IIS_TX_DAC0],
		     vbc_mux_iis_tx_get, vbc_mux_iis_tx_put),
	SOC_ENUM_EXT("VBC_MUX_DAC1_IIS_PORT_SEL",
		     vbc_mux_iis_tx_enum[VBC_MUX_IIS_TX_DAC1],
		     vbc_mux_iis_tx_get, vbc_mux_iis_tx_put),
	SOC_ENUM_EXT("VBC_MUX_DAC2_IIS_PORT_SEL",
		     vbc_mux_iis_tx_enum[VBC_MUX_IIS_TX_DAC2],
		     vbc_mux_iis_tx_get, vbc_mux_iis_tx_put),
	SOC_ENUM_EXT("VBC_MUX_ADC0_IIS_PORT_SEL",
		     vbc_mux_iis_rx_enum[VBC_MUX_IIS_RX_ADC0],
		     vbc_mux_iis_rx_get, vbc_mux_iis_rx_put),
	SOC_ENUM_EXT("VBC_MUX_ADC1_IIS_PORT_SEL",
		     vbc_mux_iis_rx_enum[VBC_MUX_IIS_RX_ADC1],
		     vbc_mux_iis_rx_get, vbc_mux_iis_rx_put),
	SOC_ENUM_EXT("VBC_MUX_ADC2_IIS_PORT_SEL",
		     vbc_mux_iis_rx_enum[VBC_MUX_IIS_RX_ADC2],
		     vbc_mux_iis_rx_get, vbc_mux_iis_rx_put),
	SOC_ENUM_EXT("VBC_MUX_ADC3_IIS_PORT_SEL",
		     vbc_mux_iis_rx_enum[VBC_MUX_IIS_RX_ADC3],
		     vbc_mux_iis_rx_get, vbc_mux_iis_rx_put),
	SOC_ENUM_EXT("VBC_MUX_IIS0_PORT_DO_SEL",
		     vbc_mux_iis_port_do_enum[VBC_IIS_PORT_IIS0],
		     vbc_mux_iis_port_do_get, vbc_mux_iis_port_do_put),
	SOC_ENUM_EXT("VBC_MUX_IIS1_PORT_DO_SEL",
		     vbc_mux_iis_port_do_enum[VBC_IIS_PORT_IIS1],
		     vbc_mux_iis_port_do_get, vbc_mux_iis_port_do_put),
	SOC_ENUM_EXT("VBC_MUX_IIS2_PORT_DO_SEL",
		     vbc_mux_iis_port_do_enum[VBC_IIS_PORT_IIS2],
		     vbc_mux_iis_port_do_get, vbc_mux_iis_port_do_put),
	SOC_ENUM_EXT("VBC_MUX_IIS3_PORT_DO_SEL",
		     vbc_mux_iis_port_do_enum[VBC_IIS_PORT_IIS3],
		     vbc_mux_iis_port_do_get, vbc_mux_iis_port_do_put),
	SOC_ENUM_EXT("VBC_MUX_MST_IIS0_PORT_DO_SEL",
		     vbc_mux_iis_port_do_enum[VBC_IIS_PORT_MST_IIS0],
		     vbc_mux_iis_port_do_get, vbc_mux_iis_port_do_put),
	/* ADDER */
	SOC_ENUM_EXT("VBC_ADDER_OFLD",
		     vbc_adder_enum[VBC_ADDER_OFLD],
		     vbc_adder_get, vbc_adder_put),
	SOC_ENUM_EXT("VBC_ADDER_FM_DAC0",
		     vbc_adder_enum[VBC_ADDER_FM_DAC0],
		     vbc_adder_get, vbc_adder_put),
	SOC_ENUM_EXT("VBC_ADDER_ST_DAC0",
		     vbc_adder_enum[VBC_ADDER_ST_DAC0],
		     vbc_adder_get, vbc_adder_put),
	/* LOOPBACK */
	SOC_SINGLE_EXT("VBC_DSP_LOOPBACK_ARM_RATE",
		       SND_SOC_NOPM, 0,
		       MAX_32_BIT, 0,
		       vbc_loopback_amr_rate_get,
		       vbc_loopback_amr_rate_put),
	SOC_SINGLE_EXT("VBC_DSP_LOOPBACK_VOICE_FMT",
		       SND_SOC_NOPM, 0,
		       MAX_32_BIT, 0,
		       vbc_loopback_voice_fmt_get,
		       vbc_loopback_voice_fmt_put),
	SOC_SINGLE_EXT("VBC_DSP_LOOPBACK_LOOP_MODE", SND_SOC_NOPM, 0,
		      MAX_32_BIT, 0,
		      vbc_loopback_loop_mode_get,
		      vbc_loopback_loop_mode_put),
	SOC_ENUM_EXT("VBC_DSP_LOOPBACK_TYPE",
		     dsp_loopback_enum,
		     vbc_loopback_type_get, vbc_loopback_type_put),
	/* DATAPATH */
	SOC_ENUM_EXT("VBC_DAC0_DP_EN",
		     vbc_datapath_enum[VBC_DAC0_DP_EN],
		     vbc_dp_en_get, vbc_dp_en_put),
	SOC_ENUM_EXT("VBC_DAC1_DP_EN",
		     vbc_datapath_enum[VBC_DAC1_DP_EN],
		     vbc_dp_en_get, vbc_dp_en_put),
	SOC_ENUM_EXT("VBC_DAC2_DP_EN",
		     vbc_datapath_enum[VBC_DAC2_DP_EN],
		     vbc_dp_en_get, vbc_dp_en_put),
	SOC_ENUM_EXT("VBC_ADC0_DP_EN",
		     vbc_datapath_enum[VBC_ADC0_DP_EN],
		     vbc_dp_en_get, vbc_dp_en_put),
	SOC_ENUM_EXT("VBC_ADC1_DP_EN",
		     vbc_datapath_enum[VBC_ADC1_DP_EN],
		     vbc_dp_en_get, vbc_dp_en_put),
	SOC_ENUM_EXT("VBC_ADC2_DP_EN",
		     vbc_datapath_enum[VBC_ADC2_DP_EN],
		     vbc_dp_en_get, vbc_dp_en_put),
	SOC_ENUM_EXT("VBC_ADC3_DP_EN",
		     vbc_datapath_enum[VBC_ADC3_DP_EN],
		     vbc_dp_en_get, vbc_dp_en_put),
	SOC_ENUM_EXT("VBC_OFLD_DP_EN",
		     vbc_datapath_enum[VBC_OFLD_DP_EN],
		     vbc_dp_en_get, vbc_dp_en_put),
	SOC_ENUM_EXT("VBC_FM_DP_EN",
		     vbc_datapath_enum[VBC_FM_DP_EN],
		     vbc_dp_en_get, vbc_dp_en_put),
	SOC_ENUM_EXT("VBC_ST_DP_EN",
		     vbc_datapath_enum[VBC_ST_DP_EN],
		     vbc_dp_en_get, vbc_dp_en_put),
	/*CALL MUTE*/
	SOC_ENUM_EXT("VBC_UL_MUTE",
		     vbc_call_mute_enum[VBC_UL_MUTE],
		     vbc_call_mute_get, vbc_call_mute_put),
	SOC_ENUM_EXT("VBC_DL_MUTE",
		     vbc_call_mute_enum[VBC_DL_MUTE],
		     vbc_call_mute_get, vbc_call_mute_put),
	SOC_ENUM_EXT("VBC IIS Master Setting", iis_master_setting_enum,
		     vbc_get_aud_iis_clock, vbc_put_aud_iis_clock),
	SOC_SINGLE_BOOL_EXT("agdsp_access_en", 0,
			    vbc_get_agdsp_access, vbc_put_agdsp_aud_access),
	SOC_SINGLE_BOOL_EXT("agdsp_access_a2dp_en", 0,
			    vbc_get_agdsp_access, vbc_put_agdsp_a2dp_access),
	/*FM MUTE*/
	SOC_SINGLE_EXT("VBC FM_MUTE_SMOOTHDG STEP", SND_SOC_NOPM, 0,
		       MAX_12_BIT, 0,
		       vbc_fm_mdg_stp_get, vbc_fm_mdg_stp_put),
	SOC_ENUM_EXT("VBC_FM_UNMUTE_SMOOTH", vbc_fm_mute_enum,
		     vbc_fm_mute_get, vbc_fm_mute_put),
	/* VBC VOLUME */
	SOC_SINGLE_EXT("VBC_VOLUME", SND_SOC_NOPM, 0,
		       MAX_32_BIT, 0,
		       vbc_volume_get, vbc_volume_put),
	/* PROFILE */
	SOC_SINGLE_EXT("Audio Structure Profile Update",
		       SND_VBC_PROFILE_AUDIO_STRUCTURE, 0,
		       2, 0,
		       vbc_profile_load_get, vbc_profile_load_put),
	SOC_SINGLE_EXT("DSP VBC Profile Update",
		       SND_VBC_PROFILE_DSP, 0,
		       2, 0,
		       vbc_profile_load_get, vbc_profile_load_put),
	SOC_SINGLE_EXT("CVS Profile Update",
		       SND_VBC_PROFILE_NXP, 0,
		       2, 0,
		       vbc_profile_load_get, vbc_profile_load_put),
	SOC_SINGLE_EXT("DSP SMARTAMP Update",
				SND_VBC_PROFILE_IVS_SMARTPA, 0,
				2, 0,
				vbc_profile_load_get, vbc_profile_load_put),
	SOC_SINGLE_EXT("Audio Structure Profile Select",
		       SND_VBC_PROFILE_AUDIO_STRUCTURE, 0,
		       VBC_PROFILE_CNT_MAX, 0,
		       vbc_profile_get, vbc_profile_put),
	SOC_SINGLE_EXT("DSP VBC Profile Select",
		       SND_VBC_PROFILE_DSP, 0,
		       VBC_PROFILE_CNT_MAX, 0,
		       vbc_profile_get, vbc_profile_put),
	SOC_SINGLE_EXT("NXP Profile Select",
		       SND_VBC_PROFILE_NXP, 0,
		       VBC_PROFILE_CNT_MAX, 0,
		       vbc_profile_get, vbc_profile_put),
	SOC_SINGLE_EXT("DSP FFSMARTAMP Select",
			SND_VBC_PROFILE_IVS_SMARTPA, 0,
			VBC_PROFILE_CNT_MAX, 0,
			vbc_profile_get, vbc_profile_put),

	/* IIS RX/TX WD */
	SOC_ENUM_EXT("VBC_IIS_TX0_WD_SEL", vbc_iis_tx_wd_enum[
		     VBC_MUX_IIS_TX_DAC0],
		     vbc_get_iis_tx_width_sel, vbc_put_iis_tx_width_sel),
	SOC_ENUM_EXT("VBC_IIS_TX1_WD_SEL", vbc_iis_tx_wd_enum[
		     VBC_MUX_IIS_TX_DAC1],
		     vbc_get_iis_tx_width_sel, vbc_put_iis_tx_width_sel),
	SOC_ENUM_EXT("VBC_IIS_TX2_WD_SEL", vbc_iis_tx_wd_enum[
		     VBC_MUX_IIS_TX_DAC2],
		     vbc_get_iis_tx_width_sel, vbc_put_iis_tx_width_sel),
	SOC_ENUM_EXT("VBC_IIS_RX0_WD_SEL", vbc_iis_rx_wd_enum[
		     VBC_MUX_IIS_RX_ADC0],
		     vbc_get_iis_rx_width_sel, vbc_put_iis_rx_width_sel),
	SOC_ENUM_EXT("VBC_IIS_RX1_WD_SEL", vbc_iis_rx_wd_enum[
		     VBC_MUX_IIS_RX_ADC1],
		     vbc_get_iis_rx_width_sel, vbc_put_iis_rx_width_sel),
	SOC_ENUM_EXT("VBC_IIS_RX2_WD_SEL", vbc_iis_rx_wd_enum[
		     VBC_MUX_IIS_RX_ADC2],
		     vbc_get_iis_rx_width_sel, vbc_put_iis_rx_width_sel),
	SOC_ENUM_EXT("VBC_IIS_RX3_WD_SEL", vbc_iis_rx_wd_enum[
		     VBC_MUX_IIS_RX_ADC3],
		     vbc_get_iis_rx_width_sel, vbc_put_iis_rx_width_sel),
	/* IIS RX/TX LR_MOD */
	SOC_ENUM_EXT("VBC_IIS_TX0_LRMOD_SEL", vbc_iis_tx_lr_mod_enum[
		     VBC_MUX_IIS_TX_DAC0],
		     vbc_get_iis_tx_lr_mod_sel, vbc_put_iis_tx_lr_mod_sel),
	SOC_ENUM_EXT("VBC_IIS_TX1_LRMOD_SEL", vbc_iis_tx_lr_mod_enum[
		     VBC_MUX_IIS_TX_DAC1],
		     vbc_get_iis_tx_lr_mod_sel, vbc_put_iis_tx_lr_mod_sel),
	SOC_ENUM_EXT("VBC_IIS_TX2_LRMOD_SEL", vbc_iis_tx_lr_mod_enum[
		     VBC_MUX_IIS_TX_DAC2],
		     vbc_get_iis_tx_lr_mod_sel, vbc_put_iis_tx_lr_mod_sel),
	SOC_ENUM_EXT("VBC_IIS_RX0_LRMOD_SEL", vbc_iis_rx_lr_mod_enum[
		     VBC_MUX_IIS_RX_ADC0],
		     vbc_get_iis_rx_lr_mod_sel, vbc_put_iis_rx_lr_mod_sel),
	SOC_ENUM_EXT("VBC_IIS_RX1_LRMOD_SEL", vbc_iis_rx_lr_mod_enum[
		     VBC_MUX_IIS_RX_ADC1],
		     vbc_get_iis_rx_lr_mod_sel, vbc_put_iis_rx_lr_mod_sel),
	SOC_ENUM_EXT("VBC_IIS_RX2_LRMOD_SEL", vbc_iis_rx_lr_mod_enum[
		     VBC_MUX_IIS_RX_ADC2],
		     vbc_get_iis_rx_lr_mod_sel, vbc_put_iis_rx_lr_mod_sel),
	SOC_ENUM_EXT("VBC_IIS_RX3_LRMOD_SEL", vbc_iis_rx_lr_mod_enum[
		     VBC_MUX_IIS_RX_ADC3],
		     vbc_get_iis_rx_lr_mod_sel, vbc_put_iis_rx_lr_mod_sel),
	SOC_ENUM_EXT("VBC_IIS_MASTER_ENALBE", vbc_iis_master_enum,
		     vbc_get_iis_master_en, vbc_put_iis_master_en),
	SOC_ENUM_EXT("VBC_IIS_MST_SEL_0_TYPE",
		     vbc_iis_mst_sel_enum[IIS_MST_SEL_0], vbc_get_mst_sel_type,
		     vbc_put_mst_sel_type),
	SOC_ENUM_EXT("VBC_IIS_MST_SEL_1_TYPE",
		     vbc_iis_mst_sel_enum[IIS_MST_SEL_1], vbc_get_mst_sel_type,
		     vbc_put_mst_sel_type),
	SOC_ENUM_EXT("VBC_IIS_MST_SEL_2_TYPE",
		     vbc_iis_mst_sel_enum[IIS_MST_SEL_2], vbc_get_mst_sel_type,
		     vbc_put_mst_sel_type),
	SOC_ENUM_EXT("VBC_IIS_MST_SEL_3_TYPE",
		     vbc_iis_mst_sel_enum[IIS_MST_SEL_3], vbc_get_mst_sel_type,
		     vbc_put_mst_sel_type),

	SOC_ENUM_EXT("VBC_IIS_MST_WIDTH_SET", vbc_iis_master_wd_width_enum,
		     vbc_get_iis_master_width, vbc_put_iis_master_width),

	SOC_ENUM_EXT("VBC_DSP_MAINMIC_PATH_SEL",
		     vbc_mainmic_path_enum[MAINMIC_USED_DSP_NORMAL_ADC],
		     vbc_mainmic_path_sel_val_get,
		     vbc_mainmic_path_sel_val_put),
	SOC_ENUM_EXT("VBC_DSP_MAINMIC_REF_PATH_SEL",
		     vbc_mainmic_path_enum[MAINMIC_USED_DSP_REF_ADC],
		     vbc_mainmic_path_sel_val_get,
		     vbc_mainmic_path_sel_val_put),
	SOC_ENUM_EXT("IVSENCE_FUNC_DSP", ivsence_enum,
		     vbc_get_ivsence_func, vbc_put_ivsence_func),
	/* PIN MUX */
	SOC_ENUM_EXT("ag_iis0_ext_sel", vbc_ag_iis_ext_sel_enum[0],
		     vbc_get_ag_iis_ext_sel, vbc_put_ag_iis_ext_sel),
	SOC_ENUM_EXT("ag_iis1_ext_sel", vbc_ag_iis_ext_sel_enum[1],
		     vbc_get_ag_iis_ext_sel, vbc_put_ag_iis_ext_sel),
	SOC_ENUM_EXT("ag_iis2_ext_sel", vbc_ag_iis_ext_sel_enum[2],
		     vbc_get_ag_iis_ext_sel, vbc_put_ag_iis_ext_sel),

	/* used for ums9230 */
	SOC_ENUM_EXT("ag_iis0_ext_sel_v1", vbc_ag_iis_ext_sel_enum_v1[0],
		     vbc_get_ag_iis_ext_sel_v1, vbc_put_ag_iis_ext_sel_v1),
	SOC_ENUM_EXT("ag_iis1_ext_sel_v1", vbc_ag_iis_ext_sel_enum_v1[1],
		     vbc_get_ag_iis_ext_sel_v1, vbc_put_ag_iis_ext_sel_v1),
	SOC_ENUM_EXT("ag_iis2_ext_sel_v1", vbc_ag_iis_ext_sel_enum_v1[2],
		     vbc_get_ag_iis_ext_sel_v1, vbc_put_ag_iis_ext_sel_v1),
	SOC_ENUM_EXT("ag_iis4_ext_sel_v1", vbc_ag_iis_ext_sel_enum_v1[3],
		     vbc_get_ag_iis_ext_sel_v1, vbc_put_ag_iis_ext_sel_v1),

	/* used for ums9620 */
	SOC_ENUM_EXT("ag_iis0_ext_sel_v2", vbc_ag_iis_ext_sel_enum_v2[0],
		     vbc_get_ag_iis_ext_sel_v2, vbc_put_ag_iis_ext_sel_v2),
	SOC_ENUM_EXT("ag_iis1_ext_sel_v2", vbc_ag_iis_ext_sel_enum_v2[1],
		     vbc_get_ag_iis_ext_sel_v2, vbc_put_ag_iis_ext_sel_v2),
	SOC_ENUM_EXT("ag_iis2_ext_sel_v2", vbc_ag_iis_ext_sel_enum_v2[2],
		     vbc_get_ag_iis_ext_sel_v2, vbc_put_ag_iis_ext_sel_v2),
	SOC_ENUM_EXT("ag_iis4_ext_sel_v2", vbc_ag_iis_ext_sel_enum_v2[3],
		     vbc_get_ag_iis_ext_sel_v2, vbc_put_ag_iis_ext_sel_v2),
	SOC_ENUM_EXT("ag_iis3_ext_sel_v2", vbc_ag_iis_ext_sel_enum_v2[4],
			 vbc_get_ag_iis_ext_sel_v2, vbc_put_ag_iis_ext_sel_v2),

	SOC_ENUM_EXT("SYS_IIS0", vbc_sys_iis_enum[SYS_IIS0],
		     sys_iis_sel_get, sys_iis_sel_put),
	SOC_ENUM_EXT("SYS_IIS1", vbc_sys_iis_enum[SYS_IIS1],
		     sys_iis_sel_get, sys_iis_sel_put),
	SOC_ENUM_EXT("SYS_IIS2", vbc_sys_iis_enum[SYS_IIS2],
		     sys_iis_sel_get, sys_iis_sel_put),
	SOC_ENUM_EXT("SYS_IIS3", vbc_sys_iis_enum[SYS_IIS3],
		     sys_iis_sel_get, sys_iis_sel_put),

	SOC_ENUM_EXT("SYS_IIS4", vbc_sys_iis_enum[SYS_IIS4],
		     sys_iis_sel_get, sys_iis_sel_put),
	SOC_ENUM_EXT("SYS_IIS5", vbc_sys_iis_enum[SYS_IIS5],
		     sys_iis_sel_get, sys_iis_sel_put),
	SOC_ENUM_EXT("SYS_IIS6", vbc_sys_iis_enum[SYS_IIS6],
		     sys_iis_sel_get, sys_iis_sel_put),
	SOC_SINGLE_EXT("VBC_BAK_REG_SET", REG_VBC_BAK_REG, 1, 2, 0,
		       vbc_reg_get, vbc_reg_put),
	SOC_ENUM_EXT("VBC_DUMP_POS", vbc_dump_pos_enum,
		vbc_get_dump_pos, vbc_put_dump_pos),
	SOC_ENUM_EXT("VBC_DUMP_POS_CMD", vbc_dump_pos_enum,
		     vbc_get_dump_pos_cmd, vbc_put_dump_pos_cmd),
	SND_SOC_BYTES_EXT("SBC_PARAS", SBC_PARA_BYTES,
		sbc_paras_get, sbc_paras_put),

	SOC_ENUM_EXT("VBC_IIS_INF_SYS_SEL", vbc_iis_inf_sys_sel_enum,
		vbc_iis_inf_sys_sel_get, vbc_iis_inf_sys_sel_put),
	SOC_ENUM_EXT("DMIC_CHN_SEL", dmic_chn_sel_enum,
		     vbc_dmic_chn_sel_get, vbc_dmic_chn_sel_put),

	/* VOICE CAPTURE */
	SOC_ENUM_EXT("VBC_DSP_VOICE_CAPTURE_TYPE",
		     dsp_voice_capture_enum,
		     vbc_voice_capture_type_get, vbc_voice_capture_type_put),
	/* VOICE PCM PLAYBACK */
	SOC_ENUM_EXT("VBC_DSP_VOICE_PCM_PLAY_MODE",
		     dsp_voice_pcm_play_enum,
		     vbc_voice_pcm_play_mode_get, vbc_voice_pcm_play_mode_put),
	SOC_ENUM_EXT("VAD_DIN_AD_SEL", vad_din_ad_sel_enum,
		     vad_din_ad_sel_get, vad_din_ad_sel_put),

	SOC_SINGLE_BOOL_EXT("HP_CROSSTALK_EN", 0,
			    dsp_hp_crosstalk_en_get, dsp_hp_crosstalk_en_put),
	SOC_DOUBLE_R_EXT_TLV("HP_CROSSTALK_GAIN",
			     0, 1, 0, MIXERDG_MAX_VAL, 0,
			     dsp_hp_crosstalk_gain_get,
			     dsp_hp_crosstalk_gain_put, hpg_tlv),
	SOC_ENUM_EXT("DSP_AUX_MIC2_SEL",
		     dsp_aux_mic2_enum,
		     dsp_aux_mic2_sel_get, dsp_aux_mic2_sel_put),
	SOC_ENUM_EXT("DSP_SMARTAMP_IV_EXCHANGE",
		     dsp_smartamp_iv_exchange_enum,
		     dsp_smartamp_iv_exchange_get,
		     dsp_smartamp_iv_exchange_put),
	SOC_ENUM_EXT("DSP_DAC0_LR_EXCHANGE",
		     dsp_dac0_lr_exchange_enum,
		     dsp_dac0_lr_exchange_get,
		     dsp_dac0_lr_exchange_put),
	SOC_ENUM_EXT("VBC_CUSTM_DEV_CHANGE",
		     dsp_custm_dev_change_enum,
		     vbc_custm_dev_change_get, vbc_custm_dev_change_put),
	SOC_ENUM_EXT("VBC_SYSTEM_DEV_CHANGE",
		     dsp_system_dev_change_enum,
		     vbc_system_dev_change_get, vbc_system_dev_change_put),
	SOC_ENUM_EXT("VBC_CUSTM_VOL_CHANGE",
		     dsp_custm_vol_change_enum,
		     vbc_custm_vol_change_get, vbc_custm_vol_change_put),

	SOC_SINGLE_BOOL_EXT("AUDIO_ZOOM_ST", 0,
		dsp_audio_zoom_st_get, dsp_audio_zoom_st_put),
	SOC_SINGLE_EXT("AUDIO_ZOOM_RATIO",
		SND_SOC_NOPM, 0, MAX_32_BIT, 0,
		dsp_audio_zoom_ratio_get, dsp_audio_zoom_ratio_put),
	SND_SOC_BYTES_EXT("AUDIO_ZOOM_FOCUS", 20,
		dsp_audio_zoom_focus_get, dsp_audio_zoom_focus_put),
};

static u32 vbc_codec_read(struct snd_soc_component *codec,
			  u32 reg)
{
	if (IS_AP_VBC_RANG(reg))
		return ap_vbc_reg_read(reg);
	else if (IS_DSP_VBC_RANG(reg))
		return dsp_vbc_reg_read(reg);

	return 0;
}

static int vbc_codec_write(struct snd_soc_component *codec, u32 reg,
			   u32 val)
{
	if (IS_AP_VBC_RANG(reg))
		return ap_vbc_reg_write(reg, val);
	else if (IS_DSP_VBC_RANG(reg))
		return dsp_vbc_reg_write(reg, val, 0xffffffff);

	return 0;
}

#ifdef CONFIG_PROC_FS
static int dsp_vbc_reg_shm_proc_read(struct snd_info_buffer *buffer)
{
	int ret;
	int reg;
	u32 size = 8*2048;
	u32 *addr = kzalloc(size, GFP_KERNEL);

	if (!addr)
		return -ENOMEM;

	ret = aud_recv_block_param(AMSG_CH_VBC_CTL, -1, -1,
		SND_VBC_DSP_IO_SHAREMEM_GET, SND_VBC_SHM_VBC_REG, addr, size,
		AUDIO_SIPC_WAIT_FOREVER);
	if (ret < 0) {
		kfree(addr);
		return -1;
	}

	snd_iprintf(buffer, "dsp-vbc register dump:\n");
	for (reg = REG_VBC_MODULE_CLR0;
	     reg <= REG_VBC_IIS_IN_STS; reg += 0x10, addr += 4) {
		snd_iprintf(buffer,
			    "0x%04x | 0x%04x 0x%04x 0x%04x 0x%04x\n",
			    reg - VBC_DSP_ADDR_BASE, (*addr),
			    *(addr + 1), *(addr + 2), *(addr + 3));
	}
	kfree(addr);

	return 0;
}

static int ap_vbc_reg_proc_read(struct snd_info_buffer *buffer)
{
	int reg, ret;
	bool active = false;
	struct aud_pm_vbc *pm_vbc;
	int scene_idx, stream;

	ret = agdsp_access_enable();
	if (ret) {
		pr_err("agdsp_access_enable:error:%d", ret);
		return ret;
	}
	snd_iprintf(buffer, "ap-vbc register dump\n");

	pm_vbc = aud_pm_vbc_get();
	if (pm_vbc == NULL) {
		agdsp_access_disable();
		return -EPERM;
	}
	mutex_lock(&pm_vbc->lock_scene_flag);

	for (scene_idx = 0; scene_idx < VBC_DAI_ID_MAX; scene_idx++) {
		for (stream = 0; stream < STREAM_CNT; stream++) {
			if (pm_vbc->scene_flag[scene_idx][stream] > 0) {
				active = true;
				break;
			}
		}
		if (active == true)
			break;
	}

	if (active == false) {
		mutex_unlock(&pm_vbc->lock_scene_flag);
		agdsp_access_disable();
		snd_iprintf(buffer,
			"vbc is inactive, can't dump ap-vbc register\n");
		return -EPERM;
	}

	for (reg = REG_VBC_AUDPLY_FIFO_CTRL;
	     reg <= VBC_AP_ADDR_END; reg += 0x10) {
		snd_iprintf(buffer, "0x%04x | 0x%04x 0x%04x 0x%04x 0x%04x\n",
			    reg, ap_vbc_reg_read(reg + 0x00)
			    , ap_vbc_reg_read(reg + 0x04)
			    , ap_vbc_reg_read(reg + 0x08)
			    , ap_vbc_reg_read(reg + 0x0C));
	}
	mutex_unlock(&pm_vbc->lock_scene_flag);
	agdsp_access_disable();

	return 0;
}

static void vbc_proc_write(struct snd_info_entry *entry,
			   struct snd_info_buffer *buffer)
{
	char line[64];
	u32 reg, val;
	int ret;
	struct snd_soc_component *codec = entry->private_data;

	ret = agdsp_access_enable();
	if (ret) {
		pr_err(":%s:agdsp_access_enable error:%d", __func__, ret);
		return;
	}
	while (!snd_info_get_line(buffer, line, sizeof(line))) {
		if (sscanf(line, "%x %x", &reg, &val) != 2)
			continue;
		pr_err("%s, reg:0x%x, val:0x%x\n", __func__, reg, val);
		if (val <= 0xfffffff)
			snd_soc_component_write(codec, reg, val);
	}
	agdsp_access_disable();
}

static void vbc_audcp_ahb_proc_read(struct snd_info_buffer *buffer)
{
	u32 val, reg;
	int ret;

	ret = agdsp_access_enable();
	if (ret) {
		pr_err("%s:agdsp_access_enable:error:%d", __func__, ret);
		return;
	}
	reg = REG_AGCP_AHB_EXT_ACC_AG_SEL;
	agcp_ahb_reg_read(reg, &val);
	snd_iprintf(buffer, "audcp ahb register dump\n");
	snd_iprintf(buffer, "0x%04x | 0x%04x\n", reg, val);
	agdsp_access_disable();
}

static void vbc_proc_read(struct snd_info_entry *entry,
			  struct snd_info_buffer *buffer)
{
	int ret;

	ret = dsp_vbc_reg_shm_proc_read(buffer);
	if (ret < 0)
		snd_iprintf(buffer, "dsp-vbc register dump error\n");

	ret = ap_vbc_reg_proc_read(buffer);
	if (ret < 0)
		snd_iprintf(buffer, "ap-vbc register dump error\n");
	vbc_audcp_ahb_proc_read(buffer);
}

static void vbc_proc_init(struct snd_soc_component *codec)
{
	struct snd_info_entry *entry;
	struct snd_card *card = codec->card->snd_card;

	if (!snd_card_proc_new(card, "vbc", &entry))
		snd_info_set_text_ops(entry, codec, vbc_proc_read);
	entry->c.text.write = vbc_proc_write;
	entry->mode |= 0200;
}
#else
/* !CONFIG_PROC_FS */
static inline void vbc_proc_init(struct snd_soc_component *codec)
{
}
#endif

/*
 * Re-assert BIT_AG_IIS0_EXT_SEL (bit 0 of the audcp-domain
 * REG_AGCP_AHB_EXT_ACC_AG_SEL, 0x335e003c), which routes AG IIS0 to the
 * audio-top and is what clocks the codec on the speaker path.
 *
 * This bit does NOT survive a suspend that had a PCM open: vbc_normal_suspend()
 * calls disable_access_force() to slam AGCP access to zero, the audcp-domain
 * register loses its contents across the sleep window, and restore_access()
 * brings access back without restoring any of the state that rode on it. With
 * the mux clear the DSP's DAC output stage has no clock, so the DSP stops
 * draining the vbc play fifo -- which presents as a total audio wedge across
 * every scene (AP01 and AP23 alike) that used to need a reboot to clear.
 *
 * Must be written UNCONDITIONALLY: vbc_get_ag_iis_ext_sel() reports
 * vbc_codec->ag_iis_ext_sel[], a driver-side cache, so a test-then-set would
 * read back "already enabled" on precisely the broken case and skip the write.
 * The write is idempotent, and arch_audio_iis_to_audio_top_enable() does its own
 * agdsp access wake, so callers need no extra wrapping.
 */
static void vbc_ag_iis0_restore_route(struct vbc_codec_priv *vbc_codec)
{
	bool wrote = false;
	u32 before = ~0u;

	if (agdsp_access_enable() != 0) {
		pr_err("%s: agdsp_access_enable failed, AG IIS0 route not restored\n",
		       __func__);
		return;
	}
	if (agdsp_can_access()) {
		/*
		 * Log the pre-write value so it is possible to tell "we restored
		 * a bit that had been lost" from "it was never cleared this
		 * time" -- the two are indistinguishable from a post-hoc devmem
		 * read, which cost us a misattribution once already.
		 */
		agcp_ahb_reg_read(REG_AGCP_AHB_EXT_ACC_AG_SEL, &before);
		arch_audio_iis_to_audio_top_enable(AG_IIS0, 1);
		wrote = true;
		pr_info("%s: AG IIS0 route asserted (EXT_ACC_AG_SEL was %#x)\n",
			__func__, before);
	} else {
		/*
		 * Legitimately transient: AGCP can still be asleep at probe, and
		 * on the resume path until the startup ipc wakes it. Callers that
		 * matter run again once it is awake, so this is not an error.
		 */
		pr_info("%s: agdsp asleep, AG IIS0 route not restored yet\n",
			__func__);
	}
	agdsp_access_disable();

	/*
	 * Only claim the cached value once the register write really happened --
	 * the old probe-time code assigned this outside both guards, so a failed
	 * write left the cache (and therefore the ag_iis0_ext_sel control)
	 * reporting "enable" over silent hardware.
	 */
	if (wrote && vbc_codec)
		vbc_codec->ag_iis_ext_sel[AG_IIS0] = 1;
}

static int vbc_codec_soc_probe(struct snd_soc_component *codec)
{
	struct vbc_codec_priv *vbc_codec = snd_soc_component_get_drvdata(codec);
	struct vbc_profile *vbc_profile_setting =
		&vbc_codec->vbc_profile_setting;
	struct snd_soc_dapm_context *dapm = snd_soc_component_to_dapm(codec);

	sp_asoc_pr_dbg("%s\n", __func__);

	/* idle_bias_off is the default now (driver->idle_bias_on = 0) */

	vbc_codec->codec = codec;
	vbc_profile_setting->codec = codec;

	vbc_proc_init(codec);

	/*
	 * Route AG IIS0 to the audio-top (BIT_AG_IIS0_EXT_SEL in the
	 * audcp-domain REG_AGCP_AHB_EXT_ACC_AG_SEL): the speaker path clocks
	 * the codec from AG IIS0 and it defaults to disable at boot, so
	 * without this the speaker is silent (only noise floor).
	 *
	 * This was previously dropped when i2s0 owned the mux, but the all-i2s
	 * card is a separate/optional card -- when it's disabled nobody sets
	 * the route, so re-own it here.
	 *
	 * A one-shot assert here is NOT sufficient, and the claim this comment
	 * used to make -- that "the permanent agdsp vote keeps the core awake,
	 * so the bit no longer gets reset across power cycles" -- is false. The
	 * bit is lost on every suspend that had a PCM open. It is therefore
	 * re-asserted on the paths that can lose it (see
	 * vbc_ag_iis0_restore_route() and its callers in dsp_startup() and
	 * vbc_normal_resume()); probe only needs to establish it for the boot
	 * case.
	 *
	 * Note this call is also why the old code needed an "amixer sset
	 * ag_iis0_ext_sel enable" from userspace despite setting the bit here:
	 * agdsp access is not always available this early, and the cache was
	 * assigned outside the guards, so the control claimed enable over
	 * hardware that had never been written.
	 */
	vbc_ag_iis0_restore_route(vbc_codec);

	snd_soc_dapm_ignore_suspend(dapm, "BE_DAI_OFFLOAD_CODEC_P");
	snd_soc_dapm_ignore_suspend(dapm, "BE_DAI_FM_CODEC_P");
	snd_soc_dapm_ignore_suspend(dapm, "BE_DAI_VOICE_CODEC_P");
	snd_soc_dapm_ignore_suspend(dapm, "BE_DAI_VOICE_CODEC_C");
	snd_soc_dapm_ignore_suspend(dapm, "BE_DAI_CAP_RECOGNISE_CODEC_C");

	return 0;
}

static void vbc_codec_soc_remove(struct snd_soc_component *codec)
{
	return;
}

static int vbc_component_suspend(struct snd_soc_component *component)
{
	struct snd_soc_dai *dai;

	for_each_component_dais(component, dai) {
		switch (dai->driver->id) {
		case BE_DAI_ID_NORMAL_AP01_CODEC:
		case BE_DAI_ID_NORMAL_AP23_CODEC:
		case BE_DAI_ID_NORMAL_AP01_USB:
		case BE_DAI_ID_NORMAL_AP23_USB:
			normal_suspend(dai);
			break;
		default:
			break;
		}
	}

	return 0;
}

static int vbc_component_resume(struct snd_soc_component *component)
{
	struct snd_soc_dai *dai;

	for_each_component_dais(component, dai) {
		switch (dai->driver->id) {
		case BE_DAI_ID_NORMAL_AP01_CODEC:
		case BE_DAI_ID_NORMAL_AP23_CODEC:
		case BE_DAI_ID_NORMAL_AP01_USB:
		case BE_DAI_ID_NORMAL_AP23_USB:
			normal_resume(dai);
			break;
		default:
			break;
		}
	}

	return 0;
}

struct snd_soc_component_driver sprd_vbc_codec = {
	.suspend = vbc_component_suspend,
	.resume = vbc_component_resume,
	.probe = vbc_codec_soc_probe,
	.remove = vbc_codec_soc_remove,
	.read = vbc_codec_read,
	.write = vbc_codec_write,
	.controls = vbc_codec_snd_controls,
	.num_controls = ARRAY_SIZE(vbc_codec_snd_controls),
};
EXPORT_SYMBOL(sprd_vbc_codec);

static void init_vbc_codec_data(struct vbc_codec_priv *vbc_codec)
{
	unsigned char i;
	/* vbc dac */
	vbc_codec->mux_iis_tx[VBC_MUX_IIS_TX_DAC0].id = VBC_MUX_IIS_TX_DAC0;
	vbc_codec->mux_iis_tx[VBC_MUX_IIS_TX_DAC0].val = VBC_IIS_PORT_IIS0;
	vbc_codec->mux_iis_tx[VBC_MUX_IIS_TX_DAC1].id = VBC_MUX_IIS_TX_DAC1;
	vbc_codec->mux_iis_tx[VBC_MUX_IIS_TX_DAC1].val = VBC_IIS_PORT_IIS1;
	vbc_codec->mux_iis_tx[VBC_MUX_IIS_TX_DAC2].id = VBC_MUX_IIS_TX_DAC2;
	vbc_codec->mux_iis_tx[VBC_MUX_IIS_TX_DAC2].val = VBC_IIS_PORT_IIS2;

	vbc_codec->mux_iis_port_do[VBC_IIS_PORT_IIS0].id = VBC_IIS_PORT_IIS0;
	vbc_codec->mux_iis_port_do[VBC_IIS_PORT_IIS0].val = IIS_DO_VAL_DAC0;
	vbc_codec->mux_iis_port_do[VBC_IIS_PORT_IIS1].id = VBC_IIS_PORT_IIS1;
	vbc_codec->mux_iis_port_do[VBC_IIS_PORT_IIS1].val = IIS_DO_VAL_DAC1;
	vbc_codec->mux_iis_port_do[VBC_IIS_PORT_IIS2].id = VBC_IIS_PORT_IIS2;
	vbc_codec->mux_iis_port_do[VBC_IIS_PORT_IIS2].val = IIS_DO_VAL_DAC2;
	vbc_codec->mux_iis_port_do[VBC_IIS_PORT_IIS3].id = VBC_IIS_PORT_IIS3;
	vbc_codec->mux_iis_port_do[VBC_IIS_PORT_IIS3].val = IIS_DO_VAL_DAC0;
	vbc_codec->mux_iis_port_do[VBC_IIS_PORT_MST_IIS0].id =
		VBC_IIS_PORT_MST_IIS0;
	vbc_codec->mux_iis_port_do[VBC_IIS_PORT_MST_IIS0].val = IIS_DO_VAL_DAC0;

	/* vbc adc */
	vbc_codec->mux_iis_rx[VBC_MUX_IIS_RX_ADC0].id = VBC_MUX_IIS_RX_ADC0;
	vbc_codec->mux_iis_rx[VBC_MUX_IIS_RX_ADC0].val = VBC_IIS_PORT_IIS1;
	vbc_codec->mux_iis_rx[VBC_MUX_IIS_RX_ADC1].id = VBC_MUX_IIS_RX_ADC1;
	vbc_codec->mux_iis_rx[VBC_MUX_IIS_RX_ADC1].val = VBC_IIS_PORT_IIS1;
	vbc_codec->mux_iis_rx[VBC_MUX_IIS_RX_ADC2].id = VBC_MUX_IIS_RX_ADC2;
	vbc_codec->mux_iis_rx[VBC_MUX_IIS_RX_ADC2].val = VBC_IIS_PORT_IIS1;
	vbc_codec->mux_iis_rx[VBC_MUX_IIS_RX_ADC3].id = VBC_MUX_IIS_RX_ADC3;
	vbc_codec->mux_iis_rx[VBC_MUX_IIS_RX_ADC3].val = VBC_IIS_PORT_IIS3;

	/* iis width */
	vbc_codec->iis_tx_wd[VBC_MUX_IIS_TX_DAC0].id = VBC_MUX_IIS_TX_DAC0;
	vbc_codec->iis_tx_wd[VBC_MUX_IIS_TX_DAC0].value = WD_24BIT;
	vbc_codec->iis_tx_wd[VBC_MUX_IIS_TX_DAC1].id = VBC_MUX_IIS_TX_DAC1;
	vbc_codec->iis_tx_wd[VBC_MUX_IIS_TX_DAC1].value = WD_16BIT;
	vbc_codec->iis_tx_wd[VBC_MUX_IIS_TX_DAC2].id = VBC_MUX_IIS_TX_DAC2;
	vbc_codec->iis_tx_wd[VBC_MUX_IIS_TX_DAC2].value = WD_16BIT;
	vbc_codec->iis_rx_wd[VBC_MUX_IIS_RX_ADC0].id = VBC_MUX_IIS_RX_ADC0;
	vbc_codec->iis_rx_wd[VBC_MUX_IIS_RX_ADC0].value = WD_24BIT;
	vbc_codec->iis_rx_wd[VBC_MUX_IIS_RX_ADC1].id = VBC_MUX_IIS_RX_ADC1;
	vbc_codec->iis_rx_wd[VBC_MUX_IIS_RX_ADC1].value = WD_24BIT;
	vbc_codec->iis_rx_wd[VBC_MUX_IIS_RX_ADC2].id = VBC_MUX_IIS_RX_ADC2;
	vbc_codec->iis_rx_wd[VBC_MUX_IIS_RX_ADC2].value = WD_16BIT;
	vbc_codec->iis_rx_wd[VBC_MUX_IIS_RX_ADC3].id = VBC_MUX_IIS_RX_ADC3;
	vbc_codec->iis_rx_wd[VBC_MUX_IIS_RX_ADC3].value = WD_16BIT;
	/* iis lr mod */
	vbc_codec->iis_tx_lr_mod[VBC_MUX_IIS_TX_DAC0].id = VBC_MUX_IIS_TX_DAC0;
	vbc_codec->iis_tx_lr_mod[VBC_MUX_IIS_TX_DAC0].value = LEFT_HIGH;
	vbc_codec->iis_tx_lr_mod[VBC_MUX_IIS_TX_DAC1].id = VBC_MUX_IIS_TX_DAC1;
	vbc_codec->iis_tx_lr_mod[VBC_MUX_IIS_TX_DAC1].value = LEFT_HIGH;
	vbc_codec->iis_tx_lr_mod[VBC_MUX_IIS_TX_DAC2].id = VBC_MUX_IIS_TX_DAC2;
	vbc_codec->iis_tx_lr_mod[VBC_MUX_IIS_TX_DAC2].value = LEFT_HIGH;
	vbc_codec->iis_rx_lr_mod[VBC_MUX_IIS_RX_ADC0].id = VBC_MUX_IIS_RX_ADC0;
	vbc_codec->iis_rx_lr_mod[VBC_MUX_IIS_RX_ADC0].value = LEFT_HIGH;
	vbc_codec->iis_rx_lr_mod[VBC_MUX_IIS_RX_ADC1].id = VBC_MUX_IIS_RX_ADC1;
	vbc_codec->iis_rx_lr_mod[VBC_MUX_IIS_RX_ADC1].value = LEFT_HIGH;
	vbc_codec->iis_rx_lr_mod[VBC_MUX_IIS_RX_ADC2].id = VBC_MUX_IIS_RX_ADC2;
	vbc_codec->iis_rx_lr_mod[VBC_MUX_IIS_RX_ADC2].value = LEFT_HIGH;
	vbc_codec->iis_rx_lr_mod[VBC_MUX_IIS_RX_ADC3].id = VBC_MUX_IIS_RX_ADC3;
	vbc_codec->iis_rx_lr_mod[VBC_MUX_IIS_RX_ADC3].value = LEFT_HIGH;
	/* default vbc iis2 connect to aon usb audio */
	vbc_codec->vbc_iis_inf_sys_sel = 1;

	/* iis master control */
	vbc_codec->mst_sel_para[IIS_MST_SEL_0].id = IIS_MST_SEL_0;
	vbc_codec->mst_sel_para[IIS_MST_SEL_0].mst_type = VBC_MASTER_EXTERNAL;
	vbc_codec->mst_sel_para[IIS_MST_SEL_1].id = IIS_MST_SEL_1;
	vbc_codec->mst_sel_para[IIS_MST_SEL_1].mst_type = VBC_MASTER_EXTERNAL;
	vbc_codec->mst_sel_para[IIS_MST_SEL_2].id = IIS_MST_SEL_2;
	vbc_codec->mst_sel_para[IIS_MST_SEL_2].mst_type = VBC_MASTER_EXTERNAL;
	vbc_codec->mst_sel_para[IIS_MST_SEL_3].id = IIS_MST_SEL_3;
	vbc_codec->mst_sel_para[IIS_MST_SEL_3].mst_type = VBC_MASTER_EXTERNAL;

	/* mixer */
	for (i = VBC_MIXER0_DAC0; i < VBC_MIXER_MAX; i++) {
		vbc_codec->mixer[i].mixer_id = i;
		vbc_codec->mixer[i].type = NOT_MIX;
	}

	/* vbc_if or iis */
	for (i = VBC_MUX_ADC0_SOURCE; i < VBC_MUX_ADC_SOURCE_MAX; i++) {
		vbc_codec->mux_adc_source[i].id = i;
		vbc_codec->mux_adc_source[i].val = ADC_SOURCE_IIS;
	}
	for (i = VBC_MUX_DAC0_OUT_SEL; i < VBC_MUX_DAC_OUT_MAX ; i++) {
		vbc_codec->mux_dac_out[i].id = i;
		vbc_codec->mux_dac_out[i].val = DAC_OUT_FROM_IIS;
	}
}

int sprd_vbc_codec_probe(struct platform_device *pdev)
{
	struct vbc_codec_priv *vbc_codec;
	struct device_node *np = pdev->dev.of_node;
	struct regmap *agcp_ahb_gpr;

	sp_asoc_pr_dbg("%s\n", __func__);

	vbc_codec = devm_kzalloc(&pdev->dev, sizeof(struct vbc_codec_priv),
				 GFP_KERNEL);
	if (vbc_codec == NULL)
		return -ENOMEM;
	init_vbc_codec_data(vbc_codec);
	platform_set_drvdata(pdev, vbc_codec);

	/* Prepare for global registers accessing. */
	agcp_ahb_gpr = syscon_regmap_lookup_by_phandle(
		np, "sprd,syscon-agcp-ahb");
	if (IS_ERR(agcp_ahb_gpr)) {
		pr_err("ERR: [%s] Get the agcp ahb syscon failed!\n",
		       __func__);
		agcp_ahb_gpr = NULL;
		return -EPROBE_DEFER;
	}
	arch_audio_set_agcp_ahb_gpr(agcp_ahb_gpr);

	/* If need internal codec(audio top) to provide clock for
	 * vbc da path.
	 */
	vbc_codec->need_aud_top_clk =
		of_property_read_bool(np, "sprd,need-aud-top-clk");
	mutex_init(&vbc_codec->load_mutex);
	mutex_init(&vbc_codec->agcp_access_mutex);

	return 0;
}

int sprd_vbc_codec_remove(struct platform_device *pdev)
{
	struct vbc_codec_priv *vbc_codec = platform_get_drvdata(pdev);

	vbc_codec->vbc_profile_setting.dev = 0;

	snd_soc_unregister_component(&pdev->dev);

	return 0;
}

//endof "vbc-codec.c"

static int to_vbc_chan(int channels)
{
	int chan;

	switch (channels) {
	case 1:
		chan = VBC_LEFT;
		break;
	case 2:
		chan = VBC_ALL_CHAN;
		break;
	default:
		chan = VBC_ALL_CHAN;
		pr_err("channels is %d, default vbc channel =%d\n",
			channels, chan);
		break;
	}

	return chan;
}

static int check_be_dai_id(int be_dai_id)
{
	int scene_id;

	switch (be_dai_id) {
	case BE_DAI_ID_NORMAL_AP01_CODEC:
	case BE_DAI_ID_NORMAL_AP01_USB:
	case BE_DAI_ID_NORMAL_AP01_P_BTSCO:
	case BE_DAI_ID_NORMAL_AP01_P_HIFI:
	case BE_DAI_ID_NORMAL_AP01_P_SMTPA:
	case BE_DAI_ID_DUMP:
		scene_id = VBC_DAI_ID_NORMAL_AP01;
		break;
	case BE_DAI_ID_NORMAL_AP23_CODEC:
	case BE_DAI_ID_NORMAL_AP23_USB:
	case BE_DAI_ID_NORMAL_AP23_HIFI:
	case BE_DAI_ID_NORMAL_AP23_SMTPA:
		scene_id = VBC_DAI_ID_NORMAL_AP23;
		break;
	case BE_DAI_ID_CAPTURE_DSP_CODEC:
	case BE_DAI_ID_CAPTURE_DSP_USB:
		scene_id = VBC_DAI_ID_CAPTURE_DSP;
		break;
	case BE_DAI_ID_FAST_P_CODEC:
	case BE_DAI_ID_FAST_P_USB:
	case BE_DAI_ID_FAST_P_BTSCO:
	case BE_DAI_ID_FAST_P_HIFI:
	case BE_DAI_ID_FAST_P_SMTPA:
		scene_id = VBC_DAI_ID_FAST_P;
		break;
	case BE_DAI_ID_FAST_P_SMART_AMP:
	case BE_DAI_ID_FAST_P_CODEC_MM:
		scene_id = VBC_DAI_ID_FAST_P_SMART_AMP;
		break;
	case BE_DAI_ID_OFFLOAD_CODEC:
	case BE_DAI_ID_OFFLOAD_USB:
	case BE_DAI_ID_OFFLOAD_HIFI:
	case BE_DAI_ID_OFFLOAD_SMTPA:
		scene_id = VBC_DAI_ID_OFFLOAD;
		break;
	case BE_DAI_ID_VOICE_CODEC:
	case BE_DAI_ID_VOICE_USB:
	case BE_DAI_ID_VOICE_BT:
	case BE_DAI_ID_VOICE_HIFI:
	case BE_DAI_ID_VOICE_SMTPA:
		scene_id = VBC_DAI_ID_VOICE;
		break;
	case BE_DAI_ID_VOIP_CODEC:
	case BE_DAI_ID_VOIP_USB:
	case BE_DAI_ID_VOIP_BT:
	case BE_DAI_ID_VOIP_HIFI:
	case BE_DAI_ID_VOIP_SMTPA:
		scene_id = VBC_DAI_ID_VOIP;
		break;
	case BE_DAI_ID_FM_CODEC:
	case BE_DAI_ID_FM_USB:
	case BE_DAI_ID_FM_HIFI:
	case BE_DAI_ID_FM_SMTPA:
		scene_id = VBC_DAI_ID_FM;
		break;
	case BE_DAI_ID_LOOP_CODEC:
	case BE_DAI_ID_LOOP_USB:
	case BE_DAI_ID_LOOP_BT:
	case BE_DAI_ID_LOOP_HIFI:
	case BE_DAI_ID_LOOP_SMTPA:
		scene_id = VBC_DAI_ID_LOOP;
		break;
	case BE_DAI_ID_PCM_A2DP:
		scene_id = VBC_DAI_ID_PCM_A2DP;
		break;
	case BE_DAI_ID_OFFLOAD_A2DP:
		scene_id = VBC_DAI_ID_OFFLOAD_A2DP;
		break;
	case BE_DAI_ID_CAPTURE_BT:
		scene_id = VBC_DAI_ID_BT_CAPTURE_AP;
		break;
	case BE_DAI_ID_VOICE_CAPTURE:
		scene_id = VBC_DAI_ID_VOICE_CAPTURE;
		break;
	case BE_DAI_ID_FM_CAPTURE:
		scene_id = VBC_DAI_ID_FM_CAPTURE_AP;
		break;
	case BE_DAI_ID_FM_CAPTURE_DSP:
		scene_id = VBC_DAI_ID_FM_CAPTURE_DSP;
		break;
	case BE_DAI_ID_CAPTURE_DSP_BTSCO:
		scene_id = VBC_DAI_ID_BT_SCO_CAPTURE_DSP;
		break;
	case BE_DAI_ID_FM_DSP_CODEC:
	case BE_DAI_ID_FM_DSP_USB:
	case BE_DAI_ID_FM_DSP_HIFI:
	case BE_DAI_ID_FM_DSP_SMTPA:
		scene_id = VBC_DAI_ID_FM_DSP;
		break;
	case BE_DAI_ID_HFP:
		scene_id = VBC_DAI_ID_HFP;
		break;
	case BE_DAI_ID_RECOGNISE_CAPTURE:
		scene_id = VBC_DAI_ID_RECOGNISE_CAPTURE;
		break;
	case BE_DAI_ID_VOICE_PCM_P:
		scene_id = VBC_DAI_ID_VOICE_PCM_P;
		break;
	case BE_DAI_ID_HIFI_P:
		scene_id = AUDCP_DAI_ID_HIFI;
		break;
	case BE_DAI_ID_HIFI_FAST_P:
		scene_id = AUDCP_DAI_ID_FAST;
		break;
	default:
		scene_id = VBC_DAI_ID_MAX;
		pr_err("unknown be dai id %d use default dsp id=%d\n",
		       be_dai_id, scene_id);
		break;
	}

	return scene_id;
}

#define SPRD_VBC_DAI_PCM_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
				  SNDRV_PCM_FMTBIT_S24_LE)

static int sprd_dai_vbc_probe(struct snd_soc_dai *dai)
{
	struct snd_soc_dapm_route intercon;

	if (!dai || !dai->driver) {
		pr_err("%s Invalid params\n", __func__);
		return -EINVAL;
	}
	memset(&intercon, 0, sizeof(intercon));
	if (dai->driver->playback.stream_name) {
		dev_dbg(dai->dev, "%s: add route for widget %s",
			__func__, dai->driver->playback.stream_name);
		intercon.source = sprd_stream_aif_name(dai->dev,
			dai->driver->playback.stream_name);
		intercon.sink = dai->driver->playback.stream_name;
		dev_dbg(dai->dev, "%s: src %s sink %s\n",
			__func__, intercon.source, intercon.sink);
		snd_soc_dapm_add_routes(snd_soc_component_to_dapm(dai->component), &intercon, 1);
	}
	if (dai->driver->capture.stream_name) {
		dev_dbg(dai->dev, "%s: add route for widget %s",
			__func__, dai->driver->capture.stream_name);
		intercon.sink = sprd_stream_aif_name(dai->dev,
			dai->driver->capture.stream_name);
		intercon.source = dai->driver->capture.stream_name;
		dev_dbg(dai->dev, "%s: src %s sink %s\n",
			__func__, intercon.source, intercon.sink);
		snd_soc_dapm_add_routes(snd_soc_component_to_dapm(dai->component), &intercon, 1);
	}

	return 0;
}

/* normal scene */
static void normal_vbc_protect_spin_lock(int stream)
{
	int is_playback = stream == SNDRV_PCM_STREAM_PLAYBACK ? 1 : 0;
	struct aud_pm_vbc *pm_vbc;

	if (!is_playback)
		return;

	pm_vbc = aud_pm_vbc_get();
	spin_lock(&pm_vbc->pm_spin_cmd_prot);
}

static void normal_vbc_protect_spin_unlock(int stream)
{
	int is_playback = stream == SNDRV_PCM_STREAM_PLAYBACK ? 1 : 0;
	struct aud_pm_vbc *pm_vbc;

	if (!is_playback)
		return;

	pm_vbc = aud_pm_vbc_get();
	spin_unlock(&pm_vbc->pm_spin_cmd_prot);
}

static void normal_vbc_protect_mutex_lock(int stream)
{
	int is_playback = stream == SNDRV_PCM_STREAM_PLAYBACK ? 1 : 0;
	struct aud_pm_vbc *pm_vbc;

	if (!is_playback)
		return;

	pm_vbc = aud_pm_vbc_get();
	mutex_lock(&pm_vbc->pm_mtx_cmd_prot);
}

static void normal_vbc_protect_mutex_unlock(int stream)
{
	int is_playback = stream == SNDRV_PCM_STREAM_PLAYBACK ? 1 : 0;
	struct aud_pm_vbc *pm_vbc;

	if (!is_playback)
		return;

	pm_vbc = aud_pm_vbc_get();
	mutex_unlock(&pm_vbc->pm_mtx_cmd_prot);
}

static void normal_p_suspend_resume_mtx_lock(void)
{
	struct aud_pm_vbc *pm_vbc;

	pm_vbc = aud_pm_vbc_get();

	mutex_lock(&pm_vbc->lock_mtx_suspend_resume);
}

static void normal_p_suspend_resume_mtx_unlock(void)
{
	struct aud_pm_vbc *pm_vbc;

	pm_vbc = aud_pm_vbc_get();

	mutex_unlock(&pm_vbc->lock_mtx_suspend_resume);
}

static void normal_p_suspend_resume_add_ref(void)
{
	struct aud_pm_vbc *pm_vbc;

	pm_vbc = aud_pm_vbc_get();
	pm_vbc->ref_suspend_resume++;
	pr_info("%s ref=%d\n", __func__, pm_vbc->ref_suspend_resume);
}

static void normal_p_suspend_resume_dec_ref(void)
{
	struct aud_pm_vbc *pm_vbc;

	pm_vbc = aud_pm_vbc_get();
	pm_vbc->ref_suspend_resume--;
	pr_info("%s ref=%d\n", __func__, pm_vbc->ref_suspend_resume);
}

static int normal_p_suspend_resume_get_ref(void)
{
	int ref;
	struct aud_pm_vbc *pm_vbc;

	pm_vbc = aud_pm_vbc_get();
	ref = pm_vbc->ref_suspend_resume;
	pr_info("%s ref=%d\n", __func__, ref);

	return ref;
}

static void set_normal_p_running_status(int stream, bool status)
{
	int is_playback = stream == SNDRV_PCM_STREAM_PLAYBACK ? 1 : 0;
	struct aud_pm_vbc *pm_vbc;

	pm_vbc = aud_pm_vbc_get();
	if (!is_playback)
		return;
	pm_vbc->is_startup = status;
	pr_info("%s is_startup=%s\n", __func__,
		pm_vbc->is_startup ? "true" : "false");
}

static bool get_normal_p_running_status(int stream)
{
	int is_playback = stream == SNDRV_PCM_STREAM_PLAYBACK ? 1 : 0;
	struct aud_pm_vbc *pm_vbc;
	bool status;

	pm_vbc = aud_pm_vbc_get();
	if (!is_playback)
		return false;

	status = pm_vbc->is_startup;
	pr_info("%s is_startup=%s\n", __func__,
		status ? "true" : "false");

	return status;
}

static int vbc_normal_resume(void)
{
	struct aud_pm_vbc *pm_vbc;

	pm_vbc = aud_pm_vbc_get();
	restore_access();
	/*
	 * NOTE: do not try to re-assert the AG IIS0 route here. restore_access()
	 * reopens the access bridge but AGCP is still asleep at this point
	 * (agdsp_can_access() is false, "BIT_PMU_APB_AGCP_SYS_SLP_STATUS not
	 * enable"), so the write is skipped and the call is pure log noise.
	 * Measured on hardware 2026-07-29. It is instead done at the end of
	 * dsp_startup(), after the startup ipc has woken the DSP -- which the
	 * resume replay in vbc_normal_restore_scene() reaches a few microseconds
	 * later on this same path.
	 */
	pr_info("%s resumed\n", __func__);

	return 0;
}
/*
 * suspend
 * 1. only vbc normal scene exists.
 * 2. send shutdown command to dsp.
 * resume
 * user space will excute close flow:
 * 1. if shutdown has been done in suspend,
 * do not send to dsp again.
 */
static int vbc_normal_suspend(void)
{
	struct aud_pm_vbc *pm_vbc;
	int stream = SNDRV_PCM_STREAM_PLAYBACK;
	int is_startup;

	pm_vbc = aud_pm_vbc_get();
	/*
	 * vbc clock clear and agcp access disable must after codec
	 * suspened because codec needed agcp access enable.
	 * Puting these function here and set bus_control to be true
	 * will meet requirements. Do not put them in PM_SUSPEND_PREPARE
	 * it is too early.
	 */
	pr_info("%s enter suspend\n", __func__);
	normal_vbc_protect_mutex_lock(stream);
	is_startup = get_normal_p_running_status(SNDRV_PCM_STREAM_PLAYBACK);
	if (is_startup == false) {
		pr_info("%s startup not called just return\n", __func__);
		normal_vbc_protect_mutex_unlock(stream);
		return 0;
	}
	pr_info("%s send shutdown\n", __func__);
	normal_vbc_protect_spin_lock(stream);
	set_normal_p_running_status(SNDRV_PCM_STREAM_PLAYBACK, false);
	normal_vbc_protect_spin_unlock(stream);
	pm_shutdown();
	normal_vbc_protect_mutex_unlock(stream);
	disable_access_force();
	pr_info("%s suspeded\n", __func__);

	return 0;
}

static int get_startup_scene_dac_id(int scene_id)
{
	int dac_id;

	switch (scene_id) {
	case VBC_DAI_ID_NORMAL_AP01:
		dac_id = VBC_DA0;
		break;
	case VBC_DAI_ID_NORMAL_AP23:
		dac_id = VBC_DA0;
		break;
	case VBC_DAI_ID_CAPTURE_DSP:
		/* not used */
		dac_id = 0;
		break;
	case VBC_DAI_ID_FAST_P:
	case VBC_DAI_ID_FAST_P_SMART_AMP:
		dac_id = VBC_DA0;
		break;
	case VBC_DAI_ID_OFFLOAD:
		dac_id = VBC_DA0;
		break;
	case VBC_DAI_ID_VOICE:
		dac_id = VBC_DA1;
		break;
	case VBC_DAI_ID_VOIP:
		dac_id = VBC_DA1;
		break;
	case VBC_DAI_ID_FM:
		dac_id = VBC_DA0;
		break;
	case VBC_DAI_ID_LOOP:
		dac_id = VBC_DA1;
		break;
	case VBC_DAI_ID_PCM_A2DP:
		dac_id = VBC_DA2;
		break;
	case VBC_DAI_ID_OFFLOAD_A2DP:
		dac_id = VBC_DA2;
		break;
	case VBC_DAI_ID_BT_CAPTURE_AP:
		/* not used */
		dac_id = 0;
		break;
	case VBC_DAI_ID_FM_CAPTURE_AP:
		/* not used */
		dac_id = 0;
		break;
	case VBC_DAI_ID_BT_SCO_CAPTURE_DSP:
		/* not used */
		dac_id = 0;
		break;
	case VBC_DAI_ID_FM_CAPTURE_DSP:
		/* not used */
		dac_id = 0;
		break;
	case VBC_DAI_ID_VOICE_CAPTURE:
		/* not used */
		dac_id = 0;
		break;
	case VBC_DAI_ID_FM_DSP:
		dac_id = VBC_DA0;
		break;
	case VBC_DAI_ID_HFP:
		dac_id = VBC_DA1;
		break;
	case VBC_DAI_ID_VOICE_PCM_P:
		/* not used */
		dac_id = 0;
		break;
	default:
		pr_err("invalid scene_id = %d\n", scene_id);
		dac_id = 0;
		break;
	}

	pr_info("%s scene is %s(id %d) dac_id = %d\n",
		__func__, scene_id_to_str(scene_id), scene_id, dac_id);

	return dac_id;
}

static int get_startup_scene_adc_id(int scene_id)
{
	int adc_id;

	switch (scene_id) {
	case VBC_DAI_ID_NORMAL_AP01:
		adc_id = VBC_AD0;
		break;
	case VBC_DAI_ID_NORMAL_AP23:
		adc_id = VBC_AD0;
		break;
	case VBC_DAI_ID_CAPTURE_DSP:
		adc_id = VBC_AD0;
		break;
	case VBC_DAI_ID_FAST_P:
	case VBC_DAI_ID_FAST_P_SMART_AMP:
		/* not used */
		adc_id = 0;
		break;
	case VBC_DAI_ID_OFFLOAD:
		/* not used */
		adc_id = 0;
		break;
	case VBC_DAI_ID_VOICE:
		adc_id = VBC_AD2;
		break;
	case VBC_DAI_ID_VOIP:
		adc_id = VBC_AD2;
		break;
	case VBC_DAI_ID_FM:
		adc_id = VBC_AD3;
		break;
	case VBC_DAI_ID_LOOP:
		adc_id = VBC_AD2;
		break;
	case VBC_DAI_ID_PCM_A2DP:
		/* not used */
		adc_id = 0;
		break;
	case VBC_DAI_ID_OFFLOAD_A2DP:
		/* not used */
		adc_id = 0;
		break;
	case VBC_DAI_ID_BT_CAPTURE_AP:
		/* not used */
		adc_id = 0;
		break;
	case VBC_DAI_ID_FM_CAPTURE_AP:
		/* not used */
		adc_id = 0;
		break;
	case VBC_DAI_ID_VOICE_CAPTURE:
		/* not used */
		adc_id = 0;
		break;
	case VBC_DAI_ID_BT_SCO_CAPTURE_DSP:
		adc_id = VBC_AD2;
		break;
	case VBC_DAI_ID_FM_CAPTURE_DSP:
		adc_id = VBC_AD3;
		break;
	case VBC_DAI_ID_FM_DSP:
		adc_id = VBC_AD3;
		break;
	case VBC_DAI_ID_HFP:
		adc_id = VBC_AD2;
		break;
	case VBC_DAI_ID_RECOGNISE_CAPTURE:
		adc_id = VBC_AD0;
		break;
	case VBC_DAI_ID_VOICE_PCM_P:
		/* not used */
		adc_id = 0;
		break;
	default:
		pr_err("invalid scene_id = %d\n", scene_id);
		adc_id = VBC_AD0;
		break;
	}
	pr_info("%s scene is %s(id %d) adc_id = %d\n",
		__func__, scene_id_to_str(scene_id), scene_id, adc_id);

	return adc_id;
}

static int16_t get_startup_mdg_reload(int mdg_id)
{
	int16_t reload;

	switch (mdg_id) {
	case VBC_MDG_DAC0_DSP:
	case VBC_MDG_DAC1_DSP:
	case VBC_MDG_AP01:
	case VBC_MDG_AP23:
		reload = 0;
		break;
	default:
		pr_err("%s invalid id=%d\n", __func__, mdg_id);
		reload = 0;
	}

	return reload;
}

static int16_t get_startup_smthdg_reload(int smthdg_id)
{
	int16_t reload;

	switch (smthdg_id) {
	case VBC_SMTHDG_DAC0:
		reload = 0;
		break;
	default:
		pr_err("%s invalid id = %d\n", __func__, smthdg_id);
		reload = 0;
	}

	return reload;
}

static int16_t get_startup_mixerdg_reload(int mixerdg_id)
{
	int16_t reload;

	switch (mixerdg_id) {
	case VBC_MIXERDG_DAC0:
	case VBC_MIXERDG_DAC1:
		reload = 0;
		break;
	default:
		pr_err("%s invalid id=%d\n", __func__, mixerdg_id);
		reload = 0;
	}

	return reload;
}

static int16_t get_startup_mixer_reload(int mixer_id)
{
	int16_t reload;

	switch (mixer_id) {
	case VBC_MIXER0_DAC0:
	case VBC_MIXER1_DAC0:
	case VBC_MIXER0_DAC1:
	case VBC_MIXER_ST:
	case VBC_MIXER_FM:
		reload = 0;
		break;
	default:
		pr_err("%s invalid id=%d\n", __func__, mixer_id);
		reload = 0;
	}

	return reload;
}

static int16_t get_startup_master_reload(void)
{
	return 1;
}

static void fill_dsp_startup_data(struct vbc_codec_priv *vbc_codec,
	int scene_id, int stream,
	struct sprd_vbc_stream_startup_shutdown *startup_info)
{
	int i;
	struct snd_pcm_startup_paras *para = &startup_info->startup_para;
	struct snd_pcm_stream_info *info = &startup_info->stream_info;

	if (!vbc_codec)
		return;
	info->id = scene_id;
	info->stream = stream;
	para->dac_id = get_startup_scene_dac_id(scene_id);
	para->adc_id = get_startup_scene_adc_id(scene_id);

	pr_debug("adc_id %d, dmic_chn_sel %d\n", para->adc_id,
		 vbc_codec->dmic_chn_sel);
	if (vbc_codec->dmic_chn_sel)
		para->adc_id = (unsigned int)vbc_codec->dmic_chn_sel;

	/* vbc_if or iis */
	for (i = VBC_MUX_ADC0_SOURCE; i < VBC_MUX_ADC_SOURCE_MAX; i++) {
		para->adc_source[i].id = vbc_codec->mux_adc_source[i].id;
		para->adc_source[i].val = vbc_codec->mux_adc_source[i].val;
	}
	for (i = VBC_MUX_DAC0_OUT_SEL; i < VBC_MUX_DAC_OUT_MAX ; i++) {
		para->dac_out[i].id = vbc_codec->mux_dac_out[i].id;
		para->dac_out[i].val = vbc_codec->mux_dac_out[i].val;
	}
	/* iis tx port select */
	for (i = VBC_MUX_IIS_TX_DAC0; i < VBC_MUX_IIS_TX_ID_MAX; i++) {
		para->mux_tx[i].id = vbc_codec->mux_iis_tx[i].id;
		para->mux_tx[i].val = vbc_codec->mux_iis_tx[i].val;
	}
	/* iis port do for tx */
	for (i = VBC_IIS_PORT_IIS0; i < VBC_IIS_PORT_ID_MAX; i++) {
		para->iis_do[i].id = vbc_codec->mux_iis_port_do[i].id;
		para->iis_do[i].val = vbc_codec->mux_iis_port_do[i].val;
	}
	/* iis rx port select */
	for (i = VBC_MUX_IIS_RX_ADC0; i < VBC_MUX_IIS_RX_ID_MAX; i++) {
		para->mux_rx[i].id = vbc_codec->mux_iis_rx[i].id;
		para->mux_rx[i].val = vbc_codec->mux_iis_rx[i].val;
	}
	/* iis tx width, tx lr_mode */
	for (i = VBC_MUX_IIS_TX_DAC0; i < VBC_MUX_IIS_TX_ID_MAX; i++) {
		para->tx_wd[i].id = vbc_codec->iis_tx_wd[i].id;
		para->tx_wd[i].value = vbc_codec->iis_tx_wd[i].value;
		para->tx_lr_mod[i].id = vbc_codec->iis_tx_lr_mod[i].id;
		para->tx_lr_mod[i].value = vbc_codec->iis_tx_lr_mod[i].value;
	}
	/* iis rx width, rx lr_mode */
	for (i = VBC_MUX_IIS_RX_ADC0; i < VBC_MUX_IIS_RX_ID_MAX; i++) {
		para->rx_wd[i].id = vbc_codec->iis_rx_wd[i].id;
		para->rx_wd[i].value = vbc_codec->iis_rx_wd[i].value;
		para->rx_lr_mod[i].id = vbc_codec->iis_rx_lr_mod[i].id;
		para->rx_lr_mod[i].value = vbc_codec->iis_rx_lr_mod[i].value;
	}

	/* iis master external or internal */
	for (i = IIS_MST_SEL_0; i < IIS_MST_SEL_ID_MAX; i++) {
		para->mst_sel_para[i].id = vbc_codec->mst_sel_para[i].id;
		para->mst_sel_para[i].mst_type =
			vbc_codec->mst_sel_para[i].mst_type;
	}

	/* vbc iis master */
	para->iis_master_para.vbc_startup_reload = get_startup_master_reload();
	para->iis_master_para.enable = vbc_codec->iis_master.enable;
	/* mute dg */
	for (i = VBC_MDG_DAC0_DSP; i < VBC_MDG_MAX; i++) {
		para->mdg_para[i].vbc_startup_reload =
			get_startup_mdg_reload(i);
		para->mdg_para[i].mdg_id = vbc_codec->mdg[i].mdg_id;
		para->mdg_para[i].mdg_mute = vbc_codec->mdg[i].mdg_mute;
		para->mdg_para[i].mdg_step = vbc_codec->mdg[i].mdg_step;
	}
	/* smthdg */
	for (i = VBC_SMTHDG_DAC0; i < VBC_SMTHDG_MAX; i++) {
		para->smthdg_modle[i].vbc_startup_reload =
			get_startup_smthdg_reload(i);
		para->smthdg_modle[i].smthdg_dg.smthdg_id =
			vbc_codec->smthdg[i].smthdg_id;
		para->smthdg_modle[i].smthdg_dg.smthdg_left =
			vbc_codec->smthdg[i].smthdg_left;
		para->smthdg_modle[i].smthdg_dg.smthdg_right =
			vbc_codec->smthdg[i].smthdg_right;
		para->smthdg_modle[i].smthdg_step.smthdg_id =
			vbc_codec->smthdg_step[i].smthdg_id;
		para->smthdg_modle[i].smthdg_step.step =
			vbc_codec->smthdg_step[i].step;
	}
	/* mixerdg */
	for (i = VBC_MIXERDG_DAC0; i < VBC_MIXERDG_MAX; i++) {
		para->mixerdg_para[i].vbc_startup_reload =
			get_startup_mixerdg_reload(i);
		para->mixerdg_para[i].mixerdg_id =
			vbc_codec->mixerdg[i].mixerdg_id;
		para->mixerdg_para[i].main_path.mixerdg_id =
			vbc_codec->mixerdg[i].main_path.mixerdg_id;
		para->mixerdg_para[i].main_path.mixerdg_main_left =
			vbc_codec->mixerdg[i].main_path.mixerdg_main_left;
		para->mixerdg_para[i].main_path.mixerdg_main_right =
			vbc_codec->mixerdg[i].main_path.mixerdg_main_right;
		para->mixerdg_para[i].mix_path.mixerdg_id =
			vbc_codec->mixerdg[i].mix_path.mixerdg_id;
		para->mixerdg_para[i].mix_path.mixerdg_mix_left =
			vbc_codec->mixerdg[i].mix_path.mixerdg_mix_left;
		para->mixerdg_para[i].mix_path.mixerdg_mix_right =
			vbc_codec->mixerdg[i].mix_path.mixerdg_mix_right;
	}
	para->mixerdg_step = vbc_codec->mixerdg_step;
	/* mixer */
	for (i = VBC_MIXER0_DAC0; i < VBC_MIXER_MAX; i++) {
		para->mixer_para[i].vbc_startup_reload =
			get_startup_mixer_reload(i);
		para->mixer_para[i].mixer_id = vbc_codec->mixer[i].mixer_id;
		para->mixer_para[i].type = vbc_codec->mixer[i].type;
	}
	/* loopback */
	para->loopback_para.loopback_type = vbc_codec->loopback.loopback_type;
	para->loopback_para.amr_rate = vbc_codec->loopback.amr_rate;
	para->loopback_para.voice_fmt = vbc_codec->loopback.voice_fmt;
	/* sbc para */
	para->sbcenc_para = vbc_codec->sbcenc_para;
	/* ivsense smartpa */
	para->ivs_smtpa.enable = check_enable_ivs_smtpa(scene_id,
		stream, vbc_codec);
	para->ivs_smtpa.iv_adc_id = get_ivsense_adc_id();
	/* voice capture type: 1-downlink, 2-uplink, 3-mix down/up link */
	para->voice_record_type = vbc_codec->voice_capture_type + 1;
}

static int dsp_startup(struct vbc_codec_priv *vbc_codec,
		       int scene_id, int stream)
{
	int ret = 0;
	struct sprd_vbc_stream_startup_shutdown startup_info;

	if (!vbc_codec)
		return 0;
	ret = agdsp_access_enable();
	if (ret) {
		pr_err("%s:agdsp_access_enable:error:%d", __func__, ret);
		return ret;
	}
	memset(&startup_info, 0,
	       sizeof(struct sprd_vbc_stream_startup_shutdown));
	fill_dsp_startup_data(vbc_codec, scene_id, stream, &startup_info);
	ret = vbc_dsp_func_startup(scene_id, stream, &startup_info);
	if (ret < 0) {
		pr_err("vbc_dsp_func_startup return error");
		agdsp_access_disable();
		return ret;
	}
	/*
	 * Re-assert the AG IIS0 route, which does not survive a suspend that had
	 * a PCM open -- see vbc_ag_iis0_restore_route(). This is the common
	 * choke point for every scene (normal AP01, AP23, fast, offload, voice,
	 * loop, fm, ...) rather than each scene's own .startup, so all of them
	 * are covered; the wedge was global across scenes, so the fix has to be
	 * too. It also means a plain close/re-open recovers, which covers
	 * triggers with no suspend involved at all, such as the pipewire node
	 * pause/resume on an ES<->emulator switch.
	 *
	 * Deliberately placed AFTER vbc_dsp_func_startup() rather than before
	 * it. On the resume path restore_access() has reopened the access bridge
	 * but AGCP is still asleep, so agdsp_can_access() is false and the write
	 * would be skipped ("BIT_PMU_APB_AGCP_SYS_SLP_STATUS not enable").
	 * Completing the startup ipc is what wakes the DSP, so by this point it
	 * has answered and AGCP is reachable. Still early enough: the DAC only
	 * needs its clock once data flows at trigger, not at startup.
	 */
	vbc_ag_iis0_restore_route(vbc_codec);
	/*
	 * Same story, bigger blast radius: the digital codec at
	 * audio-codec@33750000 is in the same audcp domain and loses its whole
	 * register window across a suspend that had a PCM open. AUD_TOP_CTL comes
	 * back as 0 -- DAC/ADC enables gone -- so there is no DAC and no I2S
	 * clock, the DSP's IIS output afifo backs up, and the DSP stops draining
	 * the vbc play fifo. DAPM cannot notice (its widget state still says the
	 * DACs are on) and the codec component has no .suspend/.resume, so
	 * without this nothing reprograms it and audio stays wedged until a full
	 * close/re-open. See OPEN-ITEMS.md section 5.
	 *
	 * Must be here rather than in a component .resume for the same reason as
	 * the mux above: AGCP is still asleep right after restore_access(), and
	 * DP register writes are silently dropped then.
	 */
	sprd_codec_restore_digital_regs();
	agdsp_access_disable();

	return 0;
}

static void fill_dsp_shutdown_data(struct vbc_codec_priv *vbc_codec,
	int scene_id, int stream,
	struct sprd_vbc_stream_startup_shutdown *shutdown_info)
{
	int dac_id, adc_id;

	if (!vbc_codec)
		return;
	shutdown_info->stream_info.id = scene_id;
	shutdown_info->stream_info.stream = stream;
	dac_id = get_startup_scene_dac_id(scene_id);
	adc_id = get_startup_scene_adc_id(scene_id);
	shutdown_info->startup_para.dac_id = dac_id;
	shutdown_info->startup_para.adc_id = adc_id;
}

static void dsp_shutdown(struct vbc_codec_priv *vbc_codec,
			 int scene_id, int stream)
{
	int ret;
	struct sprd_vbc_stream_startup_shutdown shutdown_info;

	if (!vbc_codec)
		return;
	ret = agdsp_access_enable();
	if (ret) {
		pr_err("%s, agdsp_access_enable failed!\n", __func__);
		return;
	}
	memset(&shutdown_info, 0,
	       sizeof(struct sprd_vbc_stream_startup_shutdown));
	fill_dsp_shutdown_data(vbc_codec, scene_id, stream, &shutdown_info);
	ret = vbc_dsp_func_shutdown(scene_id, stream, &shutdown_info);
	if (ret < 0) {
		agdsp_access_disable();
		return;
	}
	agdsp_access_disable();
}

static int rate_to_src_mode(unsigned int rate)
{
	int mode = -1;
	int i;
	struct sprd_codec_src_tbl {
		unsigned int rate;
		int src_mode;
	} src_tbl[] = {
		{48000, SRC_MODE_48000},
		{44100, SRC_MODE_44100},
		{32000, SRC_MODE_32000},
		{24000, SRC_MODE_24000},
		{22050, SRC_MODE_22050},
		{16000, SRC_MODE_16000},
		{12000, SRC_MODE_12000},
		{11025, SRC_MODE_11025},
		{9600, SRC_MODE_NA},
		{8000, SRC_MODE_8000},
		{96000, SRC_MODE_96000},
		{192000, SRC_MODE_192000},
	};

	for (i = 0; i < ARRAY_SIZE(src_tbl); i++) {
		if (src_tbl[i].rate == rate)
			mode = src_tbl[i].src_mode;
	}

	if (mode == -1) {
		pr_info("%s, not supported samplerate (%d)\n",
				__func__, rate);
		mode = SRC_MODE_48000;
	}

	return mode;
}

void fill_dsp_hw_data(struct vbc_codec_priv *vbc_codec,
		      int scene_id, int stream, int chan_cnt, int rate, int fmt,
		      struct sprd_vbc_stream_hw_paras *hw_data)
{
	if (!vbc_codec)
		return;
	hw_data->stream_info.id = scene_id;
	hw_data->stream_info.stream = stream;
	/* channels not use by dsp */
	hw_data->hw_params_info.channels = chan_cnt;
	hw_data->hw_params_info.format = fmt;
	/* dsp use transformed rate */
	hw_data->hw_params_info.rate = rate_to_src_mode(rate);
}

static void dsp_hw_params(struct vbc_codec_priv *vbc_codec,
	int scene_id, int stream, int chan_cnt, u32 rate, int data_fmt)
{
	struct sprd_vbc_stream_hw_paras hw_data;
	int ret;

	if (!vbc_codec)
		return;
	memset(&hw_data, 0, sizeof(struct sprd_vbc_stream_hw_paras));
	fill_dsp_hw_data(vbc_codec, scene_id, stream, chan_cnt, rate, data_fmt,
		&hw_data);
	ret = vbc_dsp_func_hwparam(scene_id, stream, &hw_data);
	if (ret < 0) {
		pr_err("vbc_dsp_func_hwparam return error\n");
		return;
	}
}

static int dsp_trigger(struct vbc_codec_priv *vbc_codec,
		       int scene_id, int stream, int up_down)
{
	int ret;

	if (!vbc_codec)
		return 0;
	ret = vbc_dsp_func_trigger(scene_id, stream, up_down);
	if (ret < 0) {
		pr_err("vbc_dsp_func_trigger return error\n");
		return ret;
	}

	return 0;
}

void set_kctrl_vbc_dac_iis_wd(struct vbc_codec_priv *vbc_codec, int dac_id,
			      int data_fmt)
{
	struct snd_ctl_elem_id id = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	};
	struct snd_kcontrol *kctrl;
	struct snd_ctl_elem_value ucontrol;
	int val;
	struct snd_soc_card *card;

	if (!vbc_codec || !vbc_codec->codec)
		return;
	card = vbc_codec->codec->card;
	if (!card)
		return;
	switch (dac_id) {
	case VBC_DA0:
		strcpy(id.name, "VBC_IIS_TX0_WD_SEL");
		break;
	case VBC_DA1:
		strcpy(id.name, "VBC_IIS_TX1_WD_SEL");
		break;
	default:
		pr_err("%s invalid dac_id=%d\n", __func__, dac_id);
		return;
	}

	down_read(&card->snd_card->controls_rwsem);
	kctrl = snd_ctl_find_id(card->snd_card, &id);
	if (!kctrl) {
		pr_err("%s can't find kctrl '%s'\n", __func__, id.name);
		up_read(&card->snd_card->controls_rwsem);
		return;
	}

	switch (data_fmt) {
	case VBC_DAT_L16:
	case VBC_DAT_H16:
		val = 0;
		break;
	case VBC_DAT_L24:
	case VBC_DAT_H24:
		val = 1;
		break;
	default:
		pr_err("unknown data fmt %d\n", data_fmt);
		val = 0;
	}

	ucontrol.value.enumerated.item[0] = val;
	vbc_put_iis_tx_width_sel(kctrl, &ucontrol);
	up_read(&card->snd_card->controls_rwsem);
}

int scene_id_to_ap_fifo_id(int scene_id, int stream)
{
	int ap_fifo_id;
	int is_playback = stream == SNDRV_PCM_STREAM_PLAYBACK ? 1 : 0;

	switch (scene_id) {
	case VBC_DAI_ID_NORMAL_AP01:
		ap_fifo_id = is_playback ? AP01_PLY_FIFO : AP01_REC_FIFO;
		break;
	case VBC_DAI_ID_FM_CAPTURE_AP:
		ap_fifo_id = AP01_REC_FIFO;
		break;
	case VBC_DAI_ID_BT_CAPTURE_AP:
		ap_fifo_id = AP01_REC_FIFO;
		break;
	case VBC_DAI_ID_NORMAL_AP23:
		ap_fifo_id = is_playback ? AP23_PLY_FIFO : AP23_REC_FIFO;
		break;
	default:
		ap_fifo_id = AP_FIFO_MAX;
		pr_err("not mapped scene_id and ap_fifo_id\n");
		break;
	}

	return ap_fifo_id;
}

int stream_to_watermark_type(int stream)
{
	int watermark_type;

	switch (stream) {
	case SNDRV_PCM_STREAM_CAPTURE:
		watermark_type = FULL_WATERMARK;
		break;
	case SNDRV_PCM_STREAM_PLAYBACK:
		watermark_type = EMPTY_WATERMARK;
		break;
	default:
		watermark_type = WATERMARK_TYPE_MAX;
		break;
	}

	return watermark_type;
}

u32 get_watermark(int fifo_id, int watermark_type)
{
	u32 watermark;

	switch (fifo_id) {
	case AP01_PLY_FIFO:
		if (watermark_type == FULL_WATERMARK)
			watermark = VBC_AUDPLY01_FULL_WATERMARK;
		else
			watermark = VBC_AUDPLY01_EMPTY_WATERMARK;
		break;
	case AP01_REC_FIFO:
		if (watermark_type == FULL_WATERMARK)
			watermark = VBC_AUDREC01_FULL_WATERMARK;
		else
			watermark = VBC_AUDREC01_EMPTY_WATERMARK;
		break;
	case AP23_PLY_FIFO:
		if (watermark_type == FULL_WATERMARK)
			watermark = VBC_AUDPLY23_FULL_WATERMARK;
		else
			watermark = VBC_AUDPLY23_EMPTY_WATERMARK;
		break;
	case AP23_REC_FIFO:
		if (watermark_type == FULL_WATERMARK)
			watermark = VBC_AUDREC23_FULL_WATERMARK;
		else
			watermark = VBC_AUDREC23_EMPTY_WATERMARK;
		break;
	default:
		watermark = 0;
		pr_err("%s invalid fifo_id =%d\n", __func__, fifo_id);
		break;
	}

	return watermark;
}

static void ap_vbc_ad_src_set(int en, unsigned int rate)
{
	int mode = rate_to_src_mode(rate);

	vbc_phy_audply_set_src_mode(en, mode);
}

static bool ap_ad_src_check(int scene_id, int stream)
{
	if (stream == SNDRV_PCM_STREAM_CAPTURE &&
		scene_id == VBC_DAI_ID_NORMAL_AP01)
		return true;

	return false;
}

/* ap_startup, ap_shutdown ignore */
static int ap_hw_params(struct vbc_codec_priv *vbc_codec,
	int scene_id, int stream, int vbc_chan, u32 rate, int data_fmt)
{
	int fifo_id;
	int watermark_type;
	int watermark;
	bool use_ad_src;
	int ret;

	if (!vbc_codec)
		return 0;

	ret = agdsp_access_enable();
	if (ret) {
		pr_err("%s:agdsp_access_enable:error:%d", __func__, ret);
		return ret;
	}
	fifo_id = scene_id_to_ap_fifo_id(scene_id, stream);
	watermark_type = stream_to_watermark_type(stream);
	watermark = get_watermark(fifo_id, watermark_type);
	if (fifo_id == AP01_REC_FIFO && watermark_type == FULL_WATERMARK)
		watermark = ((rate * VBC_AUDREC01_FULL_WATERMARK) /
			     DEFAULT_RATE) & ~BIT(0);
	ap_vbc_set_watermark(fifo_id, watermark_type, watermark);
	ap_vbc_data_format_set(fifo_id, data_fmt);
	use_ad_src = ap_ad_src_check(scene_id, stream);
	if (use_ad_src) {
		ap_vbc_ad_src_set(1, rate);
		pr_info("%s vbc ap src set rate %u\n", __func__, rate);
	}

	/* vbc_dsp_hw_params need not call in normal scene*/
	agdsp_access_disable();

	return 0;
}

void ap_vbc_fifo_pre_fill(int fifo_id, int vbc_chan)
{
	switch (fifo_id) {
	case AP01_PLY_FIFO:
		switch (vbc_chan) {
		case VBC_LEFT:
			ap_vbc_reg_write(VBC_AUDPLY_FIFO_WR_0, 0);
			break;
		case VBC_RIGHT:
			ap_vbc_reg_write(VBC_AUDPLY_FIFO_WR_1, 0);
			break;
		case VBC_ALL_CHAN:
			ap_vbc_reg_write(VBC_AUDPLY_FIFO_WR_0, 0);
			ap_vbc_reg_write(VBC_AUDPLY_FIFO_WR_1, 0);
			break;
		default:
			pr_err("%s invalid chan=%d\n", __func__,
			       vbc_chan);
			return;
		}
		break;
	case AP23_PLY_FIFO:
		switch (vbc_chan) {
		case VBC_LEFT:
			ap_vbc_reg_write(VBC_AUDPLY_FIFO_WR_2, 0);
			break;
		case VBC_RIGHT:
			ap_vbc_reg_write(VBC_AUDPLY_FIFO_WR_3, 0);
			break;
		case VBC_ALL_CHAN:
			ap_vbc_reg_write(VBC_AUDPLY_FIFO_WR_2, 0);
			ap_vbc_reg_write(VBC_AUDPLY_FIFO_WR_3, 0);
			break;
		default:
			pr_err("%s invalid chan=%d\n", __func__,
			       vbc_chan);
			return;
		}
		break;
	case AP01_REC_FIFO:
	case AP23_REC_FIFO:
	default:
		pr_err("%s %s should not pre filled\n", __func__,
		       ap_vbc_fifo_id2name(fifo_id));
		return;
	}
}

static int ap_trigger(struct vbc_codec_priv *vbc_codec,
		      int scene_id, int stream, int vbc_chan, int up_down)
{
	int is_playback = stream == SNDRV_PCM_STREAM_PLAYBACK ? 1 : 0;
	int fifo_id, ret;

	fifo_id = scene_id_to_ap_fifo_id(scene_id, stream);
	if (!vbc_codec)
		return 0;
	ret = agdsp_access_enable();
	if (ret) {
		pr_err("%s:agdsp_access_enable:error:%d", __func__, ret);
		return ret;
	}
	if (up_down == 1) {
		ap_vbc_fifo_clear(fifo_id);
		if (is_playback)
			ap_vbc_fifo_pre_fill(fifo_id, vbc_chan);
		ap_vbc_fifo_enable(fifo_id, vbc_chan, 1);
		ap_vbc_aud_dma_chn_en(fifo_id, vbc_chan, 1);
	} else {
		ap_vbc_aud_dma_chn_en(fifo_id, vbc_chan, 0);
		ap_vbc_fifo_enable(fifo_id, vbc_chan, 0);
	}
	agdsp_access_disable();

	return 0;
}

struct scene_data_s {
	struct mutex lock_startup[VBC_DAI_ID_MAX][STREAM_CNT];
	int ref_startup[VBC_DAI_ID_MAX][STREAM_CNT];
	struct mutex lock_hw_param[VBC_DAI_ID_MAX][STREAM_CNT];
	int ref_hw_param[VBC_DAI_ID_MAX][STREAM_CNT];
	struct spinlock lock_trigger[VBC_DAI_ID_MAX][STREAM_CNT];
	int ref_trigger[VBC_DAI_ID_MAX][STREAM_CNT];
	int vbc_chan[VBC_DAI_ID_MAX][STREAM_CNT];
};

static struct scene_data_s scene_data;

struct scene_data_s *get_scene_data(void)
{
	return &scene_data;
}

void set_vbc_chan(int scene_id, int stream, int vbc_chan)
{
	struct scene_data_s *scene_data;

	scene_data = get_scene_data();
	scene_data->vbc_chan[scene_id][stream] = vbc_chan;

}

int get_vbc_chan(int scene_id, int stream)
{
	struct scene_data_s *scene_data;

	scene_data = get_scene_data();

	return scene_data->vbc_chan[scene_id][stream];
}

/* lock reference operation for eache scene and stream */
void startup_lock_mtx(int scene_id, int stream)
{
	struct scene_data_s *scene_data;

	scene_data = get_scene_data();
	mutex_lock(&scene_data->lock_startup[scene_id][stream]);
	pr_debug("%s %s %s\n", __func__,
		scene_id_to_str(scene_id), stream_to_str(stream));
}
void startup_unlock_mtx(int scene_id, int stream)
{
	struct scene_data_s *scene_data;

	scene_data = get_scene_data();
	mutex_unlock(&scene_data->lock_startup[scene_id][stream]);
	pr_debug("%s %s %s\n", __func__,
		scene_id_to_str(scene_id), stream_to_str(stream));
}

void hw_param_lock_mtx(int scene_id, int stream)
{
	struct scene_data_s *scene_data;

	scene_data = get_scene_data();
	mutex_lock(&scene_data->lock_hw_param[scene_id][stream]);
	pr_debug("%s %s %s\n", __func__,
		scene_id_to_str(scene_id), stream_to_str(stream));
}

void hw_param_unlock_mtx(int scene_id, int stream)
{
	struct scene_data_s *scene_data;

	scene_data = get_scene_data();
	mutex_unlock(&scene_data->lock_hw_param[scene_id][stream]);
	pr_debug("%s %s %s\n", __func__,
		scene_id_to_str(scene_id), stream_to_str(stream));
}

void trigger_lock_spin(int scene_id, int stream)
{
	struct scene_data_s *scene_data;

	scene_data = get_scene_data();
	spin_lock(&scene_data->lock_trigger[scene_id][stream]);
	pr_debug("%s %s %s\n", __func__,
		scene_id_to_str(scene_id), stream_to_str(stream));
}

void trigger_unlock_spin(int scene_id, int stream)
{
	struct scene_data_s *scene_data;

	scene_data = get_scene_data();
	spin_unlock(&scene_data->lock_trigger[scene_id][stream]);
	pr_debug("%s %s %s\n", __func__,
		scene_id_to_str(scene_id), stream_to_str(stream));
}

void startup_add_ref(int scene_id, int stream)
{
	struct scene_data_s *scene_data;
	int ref;

	scene_data = get_scene_data();
	ref = ++scene_data->ref_startup[scene_id][stream];
	pr_debug("%s %s %s ref=%d\n", __func__,
		scene_id_to_str(scene_id), stream_to_str(stream), ref);
}
void startup_dec_ref(int scene_id, int stream)
{
	struct scene_data_s *scene_data;
	int ref = 0;

	scene_data = get_scene_data();
	if (scene_data->ref_startup[scene_id][stream] > 0)
		ref = --scene_data->ref_startup[scene_id][stream];
	pr_debug("%s %s %s ref=%d\n", __func__,
		scene_id_to_str(scene_id), stream_to_str(stream), ref);
}

int startup_get_ref(int scene_id, int stream)
{
	int ref;
	struct scene_data_s *scene_data;

	scene_data = get_scene_data();
	ref = scene_data->ref_startup[scene_id][stream];
	pr_debug("%s %s %s ref=%d\n", __func__,
		scene_id_to_str(scene_id), stream_to_str(stream), ref);

	return ref;
}

void hw_param_add_ref(int scene_id, int stream)
{
	struct scene_data_s *scene_data;
	int ref;

	scene_data = get_scene_data();
	ref = ++scene_data->ref_hw_param[scene_id][stream];
	pr_debug("%s %s %s ref=%d\n", __func__,
		scene_id_to_str(scene_id), stream_to_str(stream), ref);
}

void hw_param_dec_ref(int scene_id, int stream)
{
	struct scene_data_s *scene_data;
	int ref;

	scene_data = get_scene_data();
	ref = --scene_data->ref_hw_param[scene_id][stream];
	pr_debug("%s %s %s ref=%d\n", __func__,
		scene_id_to_str(scene_id), stream_to_str(stream), ref);
}

int hw_param_get_ref(int scene_id, int stream)
{
	int ref;
	struct scene_data_s *scene_data;

	scene_data = get_scene_data();
	ref = scene_data->ref_hw_param[scene_id][stream];
	pr_debug("%s %s %s ref=%d\n", __func__,
		scene_id_to_str(scene_id), stream_to_str(stream), ref);

	return ref;
}

void trigger_add_ref(int scene_id, int stream)
{
	struct scene_data_s *scene_data;
	int ref;

	scene_data = get_scene_data();
	ref = ++scene_data->ref_trigger[scene_id][stream];
	pr_debug("%s %s %s ref=%d\n", __func__,
		scene_id_to_str(scene_id), stream_to_str(stream), ref);
}

void trigger_dec_ref(int scene_id, int stream)
{
	struct scene_data_s *scene_data;
	int ref;

	scene_data = get_scene_data();
	ref = --scene_data->ref_trigger[scene_id][stream];
	pr_debug("%s %s %s ref=%d\n", __func__,
		scene_id_to_str(scene_id), stream_to_str(stream), ref);
}

int trigger_get_ref(int scene_id, int stream)
{
	int ref;
	struct scene_data_s *scene_data;

	scene_data = get_scene_data();
	ref = scene_data->ref_trigger[scene_id][stream];
	pr_debug("%s %s %s ref=%d\n", __func__,
		scene_id_to_str(scene_id), stream_to_str(stream), ref);

	return ref;
}

static void check_vbc_ref(int scene_id, int stream)
{
	int startup_ref, hw_param_ref, trigger_ref;
	struct scene_data_s *scene_data = get_scene_data();

	startup_lock_mtx(scene_id, stream);
	hw_param_lock_mtx(scene_id, stream);
	trigger_lock_spin(scene_id, stream);
	scene_data = get_scene_data();

	startup_ref = scene_data->ref_startup[scene_id][stream];
	hw_param_ref = scene_data->ref_hw_param[scene_id][stream];
	trigger_ref = scene_data->ref_trigger[scene_id][stream];

	if (startup_ref != hw_param_ref ||
		hw_param_ref != trigger_ref ||
		startup_ref != trigger_ref) {
		pr_err("%s ref check failed, reset refs, startup_ref %d, hw_param_ref %d, trigger_ref %d\n",
			__func__, startup_ref, hw_param_ref, trigger_ref);
		scene_data->ref_startup[scene_id][stream] = 0;
		scene_data->ref_hw_param[scene_id][stream] = 0;
		scene_data->ref_trigger[scene_id][stream] = 0;
	}
	trigger_unlock_spin(scene_id, stream);
	hw_param_unlock_mtx(scene_id, stream);
	startup_unlock_mtx(scene_id, stream);
}

static void init_pcm_ops_lock(void)
{
	int i = 0;
	int j = 0;
	struct scene_data_s *scene_data;

	scene_data = get_scene_data();
	for (i = 0; i < VBC_DAI_ID_MAX; i++) {
		for (j = 0; j < STREAM_CNT; j++) {
			mutex_init(&scene_data->lock_startup[i][j]);
			mutex_init(&scene_data->lock_hw_param[i][j]);
			spin_lock_init(&scene_data->lock_trigger[i][j]);
		}
	}
}

static int triggered_flag(int cmd)
{
	int32_t trigger_flag = false;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		trigger_flag = true;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		trigger_flag = false;
		break;
	default:
		trigger_flag = false;
	}

	return trigger_flag;
}

static bool is_only_normal_p_scene(void)
{
	struct aud_pm_vbc *pm_vbc;
	int scene_idx, stream;
	int normal_p_cnt = 0;
	int other_cnt = 0;
	bool only_normal_p;

	pm_vbc = aud_pm_vbc_get();
	mutex_lock(&pm_vbc->lock_scene_flag);
	for (scene_idx = 0; scene_idx < VBC_DAI_ID_MAX; scene_idx++) {
		for (stream = 0; stream < STREAM_CNT; stream++) {
			if (pm_vbc->scene_flag[scene_idx][stream] == 1) {
				if (scene_idx == VBC_DAI_ID_NORMAL_AP01 &&
				    stream == SNDRV_PCM_STREAM_PLAYBACK)
					normal_p_cnt++;
				else
					other_cnt++;
			}
		}
	}
	only_normal_p = normal_p_cnt == 1 && other_cnt == 0;
	mutex_unlock(&pm_vbc->lock_scene_flag);
	pr_debug("%s normal_p_cnt=%d, other_cnt=%d, only normal=%s\n", __func__,
		normal_p_cnt, other_cnt, only_normal_p ? "true" : "false");

	return only_normal_p;
}

static void set_scene_flag(int scene_id, int stream)
{
	struct aud_pm_vbc *pm_vbc;
	int flag;

	pm_vbc = aud_pm_vbc_get();
	mutex_lock(&pm_vbc->lock_scene_flag);
	flag = ++pm_vbc->scene_flag[scene_id][stream];
	mutex_unlock(&pm_vbc->lock_scene_flag);
	pr_debug("%s %s %s flag = %d\n", __func__,
		scene_id_to_str(scene_id), stream_to_str(stream), flag);
}

static void clr_scene_flag(int scene_id, int stream)
{
	struct aud_pm_vbc *pm_vbc;
	int flag = 0;

	pm_vbc = aud_pm_vbc_get();
	mutex_lock(&pm_vbc->lock_scene_flag);
	if (pm_vbc->scene_flag[scene_id][stream])
		flag = --pm_vbc->scene_flag[scene_id][stream];
	mutex_unlock(&pm_vbc->lock_scene_flag);
	pr_debug("%s %s %s flag = %d\n", __func__,
		scene_id_to_str(scene_id), stream_to_str(stream), flag);
}

static int normal_suspend(struct snd_soc_dai *dai)
{
	bool only_play;
	struct aud_pm_vbc *pm_vbc;

	pm_vbc = aud_pm_vbc_get();
	if (!snd_soc_dai_stream_active(dai, SNDRV_PCM_STREAM_PLAYBACK))
		return 0;

	normal_p_suspend_resume_mtx_lock();
	normal_p_suspend_resume_add_ref();
	if (normal_p_suspend_resume_get_ref() == 1) {
		only_play = is_only_normal_p_scene();
		if (only_play) {
			vbc_normal_suspend();
			pm_vbc->suspend_resume = true;
		}
	}
	normal_p_suspend_resume_mtx_unlock();

	return 0;
}

/*
 * Undo what vbc_normal_suspend() did to the dsp.
 *
 * Suspend sends SND_VBC_DSP_FUNC_SHUTDOWN and clears is_startup. The vendor
 * code left the recovery to the Android HAL, which closes and re-opens the
 * pcm on resume. With an alsa/pipewire userspace the stream just keeps
 * running, and because is_startup is false every later trigger skips
 * ap_trigger()/dsp_trigger() - the dma feeds the vbc fifo but nothing drains
 * it, so the speaker stays silent. Replay startup, hw_params and, if the
 * scene was triggered when we suspended, the trigger.
 */
static void vbc_normal_restore_scene(struct vbc_codec_priv *vbc_codec)
{
	int stream = SNDRV_PCM_STREAM_PLAYBACK;
	int scene_id = VBC_DAI_ID_NORMAL_AP01;
	struct aud_pm_vbc *pm_vbc = aud_pm_vbc_get();
	int vbc_chan;
	int ret;

	if (!vbc_codec)
		return;

	normal_vbc_protect_mutex_lock(stream);
	if (get_normal_p_running_status(stream)) {
		/* suspend never got as far as the shutdown */
		normal_vbc_protect_mutex_unlock(stream);
		return;
	}

	pr_info("%s replaying startup for %s\n", __func__,
		scene_id_to_str(scene_id));
	ret = dsp_startup(vbc_codec, scene_id, stream);
	if (ret) {
		pr_err("%s dsp_startup failed: %d\n", __func__, ret);
		normal_vbc_protect_mutex_unlock(stream);
		return;
	}
	normal_vbc_protect_spin_lock(stream);
	set_normal_p_running_status(stream, true);
	normal_vbc_protect_spin_unlock(stream);

	if (pm_vbc->normal_p_params_valid)
		ap_hw_params(vbc_codec, scene_id, stream,
			     pm_vbc->normal_p_vbc_chan, pm_vbc->normal_p_rate,
			     pm_vbc->normal_p_data_fmt);
	normal_vbc_protect_mutex_unlock(stream);

	/*
	 * The be was never stopped - its suspend trigger failed along with
	 * the fe one - so dpcm will not trigger it again on resume and the
	 * fifo/dma enables have to be put back here.
	 */
	/*
	 * No locks here: this runs from the dai resume callback with user
	 * space still frozen, so nothing else can touch the scene, and both
	 * ap_trigger() and dsp_trigger() can sleep (agdsp access, dsp ipc).
	 */
	if (trigger_get_ref(scene_id, stream) > 0) {
		vbc_chan = get_vbc_chan(scene_id, stream);
		ap_trigger(vbc_codec, scene_id, stream, vbc_chan, 1);
		dsp_trigger(vbc_codec, scene_id, stream, 1);
	}
}

static int normal_resume(struct snd_soc_dai *dai)
{
	bool only_play;
	struct aud_pm_vbc *pm_vbc;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pm_vbc = aud_pm_vbc_get();
	if (!snd_soc_dai_stream_active(dai, SNDRV_PCM_STREAM_PLAYBACK))
		return 0;

	normal_p_suspend_resume_mtx_lock();
	normal_p_suspend_resume_dec_ref();
	if (normal_p_suspend_resume_get_ref() == 0) {
		only_play = is_only_normal_p_scene();
		if (only_play) {
			if (pm_vbc->suspend_resume) {
				vbc_normal_resume();
				vbc_normal_restore_scene(vbc_codec);
				pm_vbc->suspend_resume = false;
			}
		}
	}
	normal_p_suspend_resume_mtx_unlock();

	return 0;
}

static int scene_normal_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_NORMAL_AP01;
	int be_dai_id = dai->id;
	int ret = 0;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	startup_lock_mtx(scene_id, stream);
	startup_add_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 1) {
		normal_vbc_protect_mutex_lock(stream);
		ret = dsp_startup(vbc_codec, scene_id, stream);
		if (ret) {
			startup_dec_ref(scene_id, stream);
			normal_vbc_protect_mutex_unlock(stream);
			startup_unlock_mtx(scene_id, stream);
			return ret;
		}
		set_scene_flag(scene_id, stream);
		normal_vbc_protect_spin_lock(stream);
		set_normal_p_running_status(stream, true);
		normal_vbc_protect_spin_unlock(stream);
		normal_vbc_protect_mutex_unlock(stream);
	}
	startup_unlock_mtx(scene_id, stream);
	return ret;
}

static void scene_normal_shutdown(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_NORMAL_AP01;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int is_started;
	int is_playback = stream == SNDRV_PCM_STREAM_PLAYBACK ? 1 : 0;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return;
	}

	if (!vbc_codec)
		return;
	startup_lock_mtx(scene_id, stream);
	startup_dec_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 0) {
		clr_scene_flag(scene_id, stream);
		normal_vbc_protect_mutex_lock(stream);
		is_started = get_normal_p_running_status(stream);
		if ((is_playback && is_started) ||
				!is_playback) {
			dsp_shutdown(vbc_codec, scene_id, stream);
			normal_vbc_protect_spin_lock(stream);
			set_normal_p_running_status(stream, false);
			normal_vbc_protect_spin_unlock(stream);
		}
		normal_vbc_protect_mutex_unlock(stream);
	}
	startup_unlock_mtx(scene_id, stream);

	check_vbc_ref(scene_id, stream);
}

static int scene_normal_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	int vbc_chan = VBC_ALL_CHAN;
	int chan_cnt;
	unsigned int rate;
	int data_fmt = VBC_DAT_L16;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_NORMAL_AP01;
	int is_started;
	int is_playback = stream == SNDRV_PCM_STREAM_PLAYBACK ? 1 : 0;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		data_fmt = VBC_DAT_L16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data_fmt = VBC_DAT_L24;
		break;
	default:
		pr_err("%s, ERR:VBC not support data fmt =%d", __func__,
		       data_fmt);
		break;
	}
	chan_cnt = params_channels(params);
	rate = params_rate(params);
	pr_info("%s data_fmt=%s, chan=%u, rate =%u\n", __func__,
		vbc_data_fmt_to_str(data_fmt), chan_cnt, rate);
	if (chan_cnt > 2)
		pr_warn("%s channel count invalid\n", __func__);

	hw_param_lock_mtx(scene_id, stream);
	hw_param_add_ref(scene_id, stream);
	if (hw_param_get_ref(scene_id, stream) == 1) {
		vbc_chan = to_vbc_chan(chan_cnt);
		set_vbc_chan(scene_id, stream, vbc_chan);
		normal_vbc_protect_mutex_lock(stream);
		is_started = get_normal_p_running_status(stream);
		if ((is_playback && is_started) ||
				!is_playback)
			ap_hw_params(vbc_codec, scene_id, stream,
				     vbc_chan, rate, data_fmt);
		if (is_playback) {
			struct aud_pm_vbc *pm_vbc = aud_pm_vbc_get();

			/* replayed by vbc_normal_restore_scene() on resume */
			pm_vbc->normal_p_vbc_chan = vbc_chan;
			pm_vbc->normal_p_rate = rate;
			pm_vbc->normal_p_data_fmt = data_fmt;
			pm_vbc->normal_p_params_valid = true;
		}
		normal_vbc_protect_mutex_unlock(stream);
	}
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_normal_hw_free(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_NORMAL_AP01;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	hw_param_lock_mtx(scene_id, stream);
	hw_param_dec_ref(scene_id, stream);
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static void fill_hifi_shutdown_data(int scene_id, int stream,
	struct snd_pcm_hifi_stream *hifi_shutdown_info)
{
	hifi_shutdown_info->id = scene_id;
	hifi_shutdown_info->stream = stream;
	hifi_shutdown_info->enable = 0;
	pr_info("%s enable: %d, scene_id: %d, stream: %d", __func__,
			hifi_shutdown_info->enable, hifi_shutdown_info->id,
			hifi_shutdown_info->stream);
}

void fill_hifi_dsp_hw_data(int scene_id, int stream, int chan_cnt, int rate, int fmt,
	struct sprd_vbc_stream_hw_paras *hifi_data)
{
	hifi_data->stream_info.id = scene_id;
	hifi_data->stream_info.stream = stream;
	hifi_data->hw_params_info.channels = chan_cnt;
	hifi_data->hw_params_info.format = fmt;
	hifi_data->hw_params_info.rate = rate_to_src_mode(rate);
	pr_info("%s id %d, stream %d, channel %d, fmt %d, rate_src_mode %d",
			__func__, hifi_data->stream_info.id, hifi_data->stream_info.stream,
			hifi_data->hw_params_info.channels,
			hifi_data->hw_params_info.format,
			hifi_data->hw_params_info.rate);
}

static void fill_hifi_startup_data(int scene_id, int stream,
	struct snd_pcm_hifi_stream *hifi_startup_info)
{
	hifi_startup_info->id = scene_id;
	hifi_startup_info->stream = stream;
	hifi_startup_info->enable = 1;
	pr_info("%s startup enable: %d, scene_id %d, stream %d",
			__func__, hifi_startup_info->enable,
			hifi_startup_info->id, hifi_startup_info->stream);
}

static int hifi_dsp_trigger(int scene_id, int stream, int up_down)
{
	int ret;

	ret = hifi_func_trigger(scene_id, stream, up_down);
	if (ret < 0) {
		pr_err("vbc_dsp_func_trigger return error\n");
		return ret;
	}

	return 0;
}

static void hifi_hw_params(int scene_id, int stream, int chan_cnt, u32 rate, int data_fmt)
{
	struct sprd_vbc_stream_hw_paras hifi_data;
	int ret;

	memset(&hifi_data, 0, sizeof(struct sprd_vbc_stream_hw_paras));
	fill_hifi_dsp_hw_data(scene_id, stream, chan_cnt, rate, data_fmt,
		&hifi_data);
	ret = hifi_dsp_func_hwparam(scene_id, stream, &hifi_data);
	if (ret < 0) {
		pr_err("HIFI_func_hwparam return error, scene_id: %d\n", scene_id);
		return;
	}
}

static void hifi_shutdown(int scene_id, int stream)
{
	int ret;
	struct snd_pcm_hifi_stream hifi_shutdown_info;

	ret = agdsp_access_enable();
	if (ret) {
		pr_err("%s, agdsp_access_enable failed!\n", __func__);
		return;
	}
	memset(&hifi_shutdown_info, 0,
	       sizeof(struct snd_pcm_hifi_stream));
	fill_hifi_shutdown_data(scene_id, stream, &hifi_shutdown_info);
	ret = hifi_func_shutdown(scene_id, stream, &hifi_shutdown_info);
	if (ret < 0) {
		agdsp_access_disable();
		return;
	}
	agdsp_access_disable();
}

static int hifi_startup(int scene_id, int stream)
{
	int ret = 0;
	struct snd_pcm_hifi_stream hifi_startup_info;

	ret = agdsp_access_enable();
	if (ret) {
		pr_err("%s:agdsp_access_enable:error:%d", __func__, ret);
		return ret;
	}
	memset(&hifi_startup_info, 0, sizeof(struct snd_pcm_hifi_stream));
	fill_hifi_startup_data(scene_id, stream, &hifi_startup_info);
	ret = hifi_func_startup(scene_id, stream, &hifi_startup_info);
	if (ret < 0) {
		pr_err("vbc_dsp_func_startup return error");
		agdsp_access_disable();
		return ret;
	}
	agdsp_access_disable();

	return 0;
}

static int scene_normal_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	int up_down;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_NORMAL_AP01;
	int ret;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int vbc_chan = VBC_ALL_CHAN;
	int is_started;
	int is_playback = stream == SNDRV_PCM_STREAM_PLAYBACK ? 1 : 0;

	pr_info("%s dai:%s(%d) scene:%s %s, cmd=%d\n", __func__,
		dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream), cmd);
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	up_down = triggered_flag(cmd);
	/* default ret is 0 */
	ret = 0;
	if (up_down == 1) {
		trigger_lock_spin(scene_id, stream);
		trigger_add_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 1) {
			vbc_chan = get_vbc_chan(scene_id, stream);
			normal_vbc_protect_spin_lock(stream);
			is_started = get_normal_p_running_status(stream);
			if ((is_playback && is_started) ||
				!is_playback) {
				ap_trigger(vbc_codec, scene_id, stream,
					   vbc_chan, up_down);
				ret = dsp_trigger(vbc_codec, scene_id,
						  stream, up_down);
			}
			normal_vbc_protect_spin_unlock(stream);
		}
		trigger_unlock_spin(scene_id, stream);

	} else {
		trigger_lock_spin(scene_id, stream);
		trigger_dec_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 0) {
			vbc_chan = get_vbc_chan(scene_id, stream);
			normal_vbc_protect_spin_lock(stream);
			is_started = get_normal_p_running_status(stream);
			if ((is_playback && is_started) ||
				!is_playback)
				ap_trigger(vbc_codec, scene_id, stream,
					   vbc_chan, up_down);
			normal_vbc_protect_spin_unlock(stream);
		}
		trigger_unlock_spin(scene_id, stream);
	}

	return ret;
}


static struct snd_soc_dai_ops normal_ops = {
	.probe = sprd_dai_vbc_probe,
	.startup = scene_normal_startup,
	.shutdown = scene_normal_shutdown,
	.hw_params = scene_normal_hw_params,
	.trigger = scene_normal_trigger,
	.hw_free = scene_normal_hw_free,
};

/* normal ap23 */
static int scene_normal_ap23_startup(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_NORMAL_AP23;
	int be_dai_id = dai->id;
	int ret = 0;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	startup_lock_mtx(scene_id, stream);
	startup_add_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 1) {
		ret = dsp_startup(vbc_codec, scene_id, stream);
		if (ret)
			startup_dec_ref(scene_id, stream);
		else
			set_scene_flag(scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);

	return ret;
}

static void scene_normal_ap23_shutdown(struct snd_pcm_substream *substream,
				       struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_NORMAL_AP23;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return;
	}

	if (!vbc_codec)
		return;
	startup_lock_mtx(scene_id, stream);
	startup_dec_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 0) {
		clr_scene_flag(scene_id, stream);
		dsp_shutdown(vbc_codec, scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);
}

static int scene_normal_ap23_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	int vbc_chan = VBC_ALL_CHAN;
	int chan_cnt;
	unsigned int rate;
	int data_fmt = VBC_DAT_L16;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_NORMAL_AP23;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		data_fmt = VBC_DAT_L16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data_fmt = VBC_DAT_L24;
		break;
	default:
		pr_err("%s, ERR:VBC not support data fmt =%d", __func__,
		       data_fmt);
		break;
	}
	chan_cnt = params_channels(params);
	rate = params_rate(params);
	pr_info("%s data_fmt=%s, chan=%u, rate =%u\n", __func__,
		vbc_data_fmt_to_str(data_fmt), chan_cnt, rate);
	if (chan_cnt > 2)
		pr_warn("%s channel count invalid\n", __func__);

	hw_param_lock_mtx(scene_id, stream);
	hw_param_add_ref(scene_id, stream);
	if (hw_param_get_ref(scene_id, stream) == 1) {
		vbc_chan = to_vbc_chan(chan_cnt);
		set_vbc_chan(scene_id, stream, vbc_chan);
		ap_hw_params(vbc_codec, scene_id, stream,
			     vbc_chan, rate, data_fmt);
	}
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_normal_ap23_hw_free(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_NORMAL_AP23;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	hw_param_lock_mtx(scene_id, stream);
	hw_param_dec_ref(scene_id, stream);
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_normal_ap23_trigger(struct snd_pcm_substream *substream,
				     int cmd, struct snd_soc_dai *dai)
{
	int up_down;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_NORMAL_AP23;
	int ret;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int vbc_chan = VBC_ALL_CHAN;

	pr_info("%s dai:%s(%d) scene:%s %s, cmd=%d\n", __func__,
		dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream), cmd);
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	up_down = triggered_flag(cmd);
	/* default ret is 0 */
	ret = 0;
	if (up_down == 1) {
		trigger_lock_spin(scene_id, stream);
		trigger_add_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 1) {
			vbc_chan = get_vbc_chan(scene_id, stream);
			ap_trigger(vbc_codec, scene_id, stream, vbc_chan,
				   up_down);
			ret = dsp_trigger(vbc_codec, scene_id, stream, up_down);
		}
		trigger_unlock_spin(scene_id, stream);

	} else {
		trigger_lock_spin(scene_id, stream);
		trigger_dec_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 0) {
			vbc_chan = get_vbc_chan(scene_id, stream);
			ap_trigger(vbc_codec, scene_id, stream, vbc_chan,
				   up_down);
		}
		trigger_unlock_spin(scene_id, stream);
	}

	return ret;
}

static struct snd_soc_dai_ops normal_ap23_ops = {
	.probe = sprd_dai_vbc_probe,
	.startup = scene_normal_ap23_startup,
	.shutdown = scene_normal_ap23_shutdown,
	.hw_params = scene_normal_ap23_hw_params,
	.trigger = scene_normal_ap23_trigger,
	.hw_free = scene_normal_ap23_hw_free,
};

/* capture dsp */
static int scene_capture_dsp_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_CAPTURE_DSP;
	int be_dai_id = dai->id;
	int ret = 0;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	startup_lock_mtx(scene_id, stream);
	startup_add_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 1) {
		ret = dsp_startup(vbc_codec, scene_id, stream);
		if (ret)
			startup_dec_ref(scene_id, stream);
		else
			set_scene_flag(scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);

	return ret;
}

static void scene_capture_dsp_shutdown(struct snd_pcm_substream *substream,
				       struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_CAPTURE_DSP;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return;
	}

	if (!vbc_codec)
		return;
	startup_lock_mtx(scene_id, stream);
	startup_dec_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 0) {
		clr_scene_flag(scene_id, stream);
		dsp_shutdown(vbc_codec, scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);

}
static int scene_capture_dsp_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int rate;
	int data_fmt = VBC_DAT_L16;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_CAPTURE_DSP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int chan_cnt;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		data_fmt = VBC_DAT_L16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data_fmt = VBC_DAT_L24;
		break;
	default:
		pr_err("%s, ERR:VBC not support data fmt =%d", __func__,
			   data_fmt);
		break;
	}
	chan_cnt = params_channels(params);
	rate = params_rate(params);
	pr_info("%s data_fmt=%s, chan=%u, rate =%u\n", __func__,
		vbc_data_fmt_to_str(data_fmt), chan_cnt, rate);

	if (chan_cnt > 2)
		pr_warn("%s channel count invalid\n", __func__);

	hw_param_lock_mtx(scene_id, stream);
	hw_param_add_ref(scene_id, stream);
	if (hw_param_get_ref(scene_id, stream) == 1) {
		dsp_hw_params(vbc_codec, scene_id, stream,
				  chan_cnt, rate, data_fmt);
	}
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_capture_dsp_hw_free(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_CAPTURE_DSP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	hw_param_lock_mtx(scene_id, stream);
	hw_param_dec_ref(scene_id, stream);
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_capture_dsp_trigger(struct snd_pcm_substream *substream,
				     int cmd, struct snd_soc_dai *dai)
{
	int up_down;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_CAPTURE_DSP;
	int ret;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s, cmd=%d\n", __func__,
		dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream), cmd);
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	up_down = triggered_flag(cmd);
	/* default ret is 0 */
	ret = 0;
	if (up_down == 1) {
		trigger_lock_spin(scene_id, stream);
		trigger_add_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 1)
			ret = dsp_trigger(vbc_codec, scene_id, stream, up_down);

		trigger_unlock_spin(scene_id, stream);
	} else {
		trigger_lock_spin(scene_id, stream);
		trigger_dec_ref(scene_id, stream);
		trigger_unlock_spin(scene_id, stream);
	}

	return ret;
}

static struct snd_soc_dai_ops capture_dsp_ops = {
	.probe = sprd_dai_vbc_probe,
	.startup = scene_capture_dsp_startup,
	.shutdown = scene_capture_dsp_shutdown,
	.hw_params = scene_capture_dsp_hw_params,
	.trigger = scene_capture_dsp_trigger,
	.hw_free = scene_capture_dsp_hw_free,
};

/* recognise capture */
static int scene_recognise_capture_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_RECOGNISE_CAPTURE;
	int be_dai_id = dai->id;
	int ret = 0;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	startup_lock_mtx(scene_id, stream);
	startup_add_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 1) {
		ret = dsp_startup(vbc_codec, scene_id, stream);
		if (ret)
			startup_dec_ref(scene_id, stream);
		else
			set_scene_flag(scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);

	return ret;
}

static void
scene_recognise_capture_shutdown(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_RECOGNISE_CAPTURE;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_debug("%s dai:%s(%d) scene:%s %s\n", __func__,
		 dai_id_to_str(be_dai_id),
		 be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));

	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return;
	}

	if (!vbc_codec)
		return;

	startup_lock_mtx(scene_id, stream);
	startup_dec_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 0) {
		clr_scene_flag(scene_id, stream);
		dsp_shutdown(vbc_codec, scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);
}

static int
scene_recognise_capture_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	unsigned int rate;
	int data_fmt = VBC_DAT_L16;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_RECOGNISE_CAPTURE;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int chan_cnt;

	pr_debug("%s dai:%s(%d) scene:%s %s\n", __func__,
		 dai_id_to_str(dai->id),
		 dai->id, scene_id_to_str(scene_id), stream_to_str(stream));

	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		data_fmt = VBC_DAT_L16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data_fmt = VBC_DAT_L24;
		break;
	default:
		pr_err("%s, ERR:VBC not support data fmt =%d", __func__,
			   data_fmt);
		break;
	}
	chan_cnt = params_channels(params);
	rate = params_rate(params);
	pr_debug("%s data_fmt=%s, chan=%u, rate =%u\n", __func__,
		 vbc_data_fmt_to_str(data_fmt), chan_cnt, rate);

	if (chan_cnt > 2)
		pr_warn("%s channel count invalid\n", __func__);

	hw_param_lock_mtx(scene_id, stream);
	hw_param_add_ref(scene_id, stream);
	if (hw_param_get_ref(scene_id, stream) == 1) {
		dsp_hw_params(vbc_codec, scene_id, stream,
				  chan_cnt, rate, data_fmt);
	}
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_recognise_capture_hw_free(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_RECOGNISE_CAPTURE;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_debug("%s dai:%s(%d) scene:%s %s\n", __func__,
		 dai_id_to_str(dai->id),
		 dai->id, scene_id_to_str(scene_id), stream_to_str(stream));

	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	hw_param_lock_mtx(scene_id, stream);
	hw_param_dec_ref(scene_id, stream);
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_recognise_capture_trigger(struct snd_pcm_substream *substream,
				     int cmd, struct snd_soc_dai *dai)
{
	int up_down;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_RECOGNISE_CAPTURE;
	int ret = 0;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_debug("%s dai:%s(%d) scene:%s %s, cmd=%d\n", __func__,
		 dai_id_to_str(dai->id),
		 dai->id, scene_id_to_str(scene_id),
		 stream_to_str(stream), cmd);
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;

	up_down = triggered_flag(cmd);
	if (up_down == 1) {
		trigger_lock_spin(scene_id, stream);
		trigger_add_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 1)
			ret = dsp_trigger(vbc_codec, scene_id, stream, up_down);

		trigger_unlock_spin(scene_id, stream);
	} else {
		trigger_lock_spin(scene_id, stream);
		trigger_dec_ref(scene_id, stream);
		trigger_unlock_spin(scene_id, stream);
	}

	return ret;
}

static struct snd_soc_dai_ops recognise_capture_ops = {
	.probe = sprd_dai_vbc_probe,
	.startup = scene_recognise_capture_startup,
	.shutdown = scene_recognise_capture_shutdown,
	.hw_params = scene_recognise_capture_hw_params,
	.trigger = scene_recognise_capture_trigger,
	.hw_free = scene_recognise_capture_hw_free,
};

/* fast */
static int scene_fast_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FAST_P;
	int be_dai_id = dai->id;
	int ret = 0;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	startup_lock_mtx(scene_id, stream);
	startup_add_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 1) {
		ret = dsp_startup(vbc_codec, scene_id, stream);
		if (ret)
			startup_dec_ref(scene_id, stream);
		else
			set_scene_flag(scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);

	return ret;
}

static void scene_fast_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FAST_P;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return;
	}

	if (!vbc_codec)
		return;
	startup_lock_mtx(scene_id, stream);
	startup_dec_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 0) {
		clr_scene_flag(scene_id, stream);
		dsp_shutdown(vbc_codec, scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);
}

static int scene_fast_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int rate;
	int data_fmt = VBC_DAT_L16;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FAST_P;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int chan_cnt;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		data_fmt = VBC_DAT_L16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data_fmt = VBC_DAT_L24;
		break;
	default:
		pr_err("%s, ERR:VBC not support data fmt =%d", __func__,
		       data_fmt);
		break;
	}
	chan_cnt = params_channels(params);
	rate = params_rate(params);
	pr_info("%s data_fmt=%s, chan=%u, rate =%u\n", __func__,
		vbc_data_fmt_to_str(data_fmt), chan_cnt, rate);

	if (chan_cnt > 2)
		pr_warn("%s channel count invalid\n", __func__);

	hw_param_lock_mtx(scene_id, stream);
	hw_param_add_ref(scene_id, stream);
	if (hw_param_get_ref(scene_id, stream) == 1) {
		dsp_hw_params(vbc_codec, scene_id, stream,
			      chan_cnt, rate, data_fmt);
	}
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_fast_hw_free(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FAST_P;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	hw_param_lock_mtx(scene_id, stream);
	hw_param_dec_ref(scene_id, stream);
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_fast_trigger(struct snd_pcm_substream *substream, int cmd,
			      struct snd_soc_dai *dai)
{
	int up_down;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FAST_P;
	int ret;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s, cmd=%d\n", __func__,
		dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream), cmd);
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	up_down = triggered_flag(cmd);
	/* default ret is 0 */
	ret = 0;
	if (up_down == 1) {
		trigger_lock_spin(scene_id, stream);
		trigger_add_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 1)
			ret = dsp_trigger(vbc_codec, scene_id, stream, up_down);

		trigger_unlock_spin(scene_id, stream);
	} else {
		trigger_lock_spin(scene_id, stream);
		trigger_dec_ref(scene_id, stream);
		trigger_unlock_spin(scene_id, stream);
	}

	return ret;
}

static struct snd_soc_dai_ops fast_ops = {
	.probe = sprd_dai_vbc_probe,
	.startup = scene_fast_startup,
	.shutdown = scene_fast_shutdown,
	.hw_params = scene_fast_hw_params,
	.trigger = scene_fast_trigger,
	.hw_free = scene_fast_hw_free,
};

/* offload */
static int scene_offload_startup(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_OFFLOAD;
	int ret = 0;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int be_dai_id = dai->id;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	startup_lock_mtx(scene_id, stream);
	startup_add_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 1) {
		ret = dsp_startup(vbc_codec, scene_id, stream);
		if (ret)
			startup_dec_ref(scene_id, stream);
		else
			set_scene_flag(scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);

	return ret;
}

static void scene_offload_shutdown(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_OFFLOAD;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return;
	}

	if (!vbc_codec)
		return;
	startup_lock_mtx(scene_id, stream);
	startup_dec_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 0) {
		clr_scene_flag(scene_id, stream);
		dsp_shutdown(vbc_codec, scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);
}

static int scene_offload_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int rate = 48000;
	int data_fmt = VBC_DAT_L16;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_OFFLOAD;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int chan_cnt = 2;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;

	pr_info("%s data_fmt=%s, chan=%u, rate =%u\n", __func__,
		vbc_data_fmt_to_str(data_fmt), chan_cnt, rate);
	if (chan_cnt > 2)
		pr_warn("%s channel count invalid\n", __func__);
	hw_param_lock_mtx(scene_id, stream);
	hw_param_add_ref(scene_id, stream);
	if (hw_param_get_ref(scene_id, stream) == 1) {
		dsp_hw_params(vbc_codec, scene_id, stream,
			      chan_cnt, rate, data_fmt);
	}
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_offload_hw_free(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_OFFLOAD;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	hw_param_lock_mtx(scene_id, stream);
	hw_param_dec_ref(scene_id, stream);
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_offload_trigger(struct snd_pcm_substream *substream, int cmd,
				 struct snd_soc_dai *dai)
{
	int up_down;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_OFFLOAD;
	int ret;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s, cmd=%d\n", __func__,
		dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream), cmd);
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	up_down = triggered_flag(cmd);
	/* default ret is 0 */
	ret = 0;
	if (up_down == 1) {
		trigger_lock_spin(scene_id, stream);
		trigger_add_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 1)
			ret = dsp_trigger(vbc_codec, scene_id, stream, up_down);
		trigger_unlock_spin(scene_id, stream);
	} else {
		trigger_lock_spin(scene_id, stream);
		trigger_dec_ref(scene_id, stream);
		trigger_unlock_spin(scene_id, stream);
	}

	return ret;
}

static struct snd_soc_dai_ops offload_ops = {
	.probe = sprd_dai_vbc_probe,
	.startup = scene_offload_startup,
	.shutdown = scene_offload_shutdown,
	.hw_params = scene_offload_hw_params,
	.trigger = scene_offload_trigger,
	.hw_free = scene_offload_hw_free,
};

/* offload a2dp */
static int scene_offload_a2dp_startup(struct snd_pcm_substream *substream,
				      struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_OFFLOAD_A2DP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int be_dai_id = dai->id;
	int ret = 0;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	startup_lock_mtx(scene_id, stream);
	startup_add_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 1) {
		ret = dsp_startup(vbc_codec, scene_id, stream);
		if (ret)
			startup_dec_ref(scene_id, stream);
		else
			set_scene_flag(scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);

	return ret;
}

static void scene_offload_a2dp_shutdown(struct snd_pcm_substream *substream,
					struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_OFFLOAD_A2DP;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return;
	}

	if (!vbc_codec)
		return;
	startup_lock_mtx(scene_id, stream);
	startup_dec_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 0) {
		clr_scene_flag(scene_id, stream);
		dsp_shutdown(vbc_codec, scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);
}

static int scene_offload_a2dp_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int rate;
	int data_fmt = VBC_DAT_L16;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_OFFLOAD_A2DP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int chan_cnt;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		data_fmt = VBC_DAT_L16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data_fmt = VBC_DAT_L24;
		break;
	default:
		pr_err("%s, ERR:VBC not support data fmt =%d", __func__,
		       data_fmt);
		break;
	}
	chan_cnt = params_channels(params);
	rate = params_rate(params);
	pr_info("%s data_fmt=%s, chan=%u, rate =%u\n", __func__,
		vbc_data_fmt_to_str(data_fmt), chan_cnt, rate);
	if (chan_cnt > 2)
		pr_warn("%s channel count invalid\n", __func__);
	hw_param_lock_mtx(scene_id, stream);
	hw_param_add_ref(scene_id, stream);
	if (hw_param_get_ref(scene_id, stream) == 1) {
		dsp_hw_params(vbc_codec, scene_id, stream,
			      chan_cnt, rate, data_fmt);
	}
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_offload_a2dp_hw_free(struct snd_pcm_substream *substream,
				      struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_OFFLOAD_A2DP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	hw_param_lock_mtx(scene_id, stream);
	hw_param_dec_ref(scene_id, stream);
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_offload_a2dp_trigger(struct snd_pcm_substream *substream,
				      int cmd, struct snd_soc_dai *dai)
{
	int up_down;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_OFFLOAD_A2DP;
	int ret;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s, cmd=%d\n", __func__,
		dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream), cmd);
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	up_down = triggered_flag(cmd);
	/* default ret is 0 */
	ret = 0;
	if (up_down == 1) {
		trigger_lock_spin(scene_id, stream);
		trigger_add_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 1)
			ret = dsp_trigger(vbc_codec, scene_id, stream, up_down);
		trigger_unlock_spin(scene_id, stream);
	} else {
		trigger_lock_spin(scene_id, stream);
		trigger_dec_ref(scene_id, stream);
		trigger_unlock_spin(scene_id, stream);
	}

	return ret;
}

static struct snd_soc_dai_ops offload_a2dp_ops = {
	.probe = sprd_dai_vbc_probe,
	.startup = scene_offload_a2dp_startup,
	.shutdown = scene_offload_a2dp_shutdown,
	.hw_params = scene_offload_a2dp_hw_params,
	.trigger = scene_offload_a2dp_trigger,
	.hw_free = scene_offload_a2dp_hw_free,
};

/* pcm a2dp */
static int scene_pcm_a2dp_startup(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_PCM_A2DP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int be_dai_id = dai->id;
	int ret = 0;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	startup_lock_mtx(scene_id, stream);
	startup_add_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 1) {
		ret = dsp_startup(vbc_codec, scene_id, stream);
		if (ret)
			startup_dec_ref(scene_id, stream);
		else
			set_scene_flag(scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);

	return ret;
}

static void scene_pcm_a2dp_shutdown(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_PCM_A2DP;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return;
	}

	if (!vbc_codec)
		return;
	startup_lock_mtx(scene_id, stream);
	startup_dec_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 0) {
		clr_scene_flag(scene_id, stream);
		dsp_shutdown(vbc_codec, scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);
}

static int scene_pcm_a2dp_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int rate;
	int data_fmt = VBC_DAT_L16;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_PCM_A2DP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int chan_cnt;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		data_fmt = VBC_DAT_L16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data_fmt = VBC_DAT_L24;
		break;
	default:
		pr_err("%s, ERR:VBC not support data fmt =%d", __func__,
		       data_fmt);
		break;
	}
	chan_cnt = params_channels(params);
	rate = params_rate(params);
	pr_info("%s data_fmt=%s, chan=%u, rate =%u\n", __func__,
		vbc_data_fmt_to_str(data_fmt), chan_cnt, rate);
	if (chan_cnt > 2)
		pr_warn("%s channel count invalid\n", __func__);
	hw_param_lock_mtx(scene_id, stream);
	hw_param_add_ref(scene_id, stream);
	if (hw_param_get_ref(scene_id, stream) == 1) {
		dsp_hw_params(vbc_codec, scene_id, stream,
			      chan_cnt, rate, data_fmt);
	}
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_pcm_a2dp_hw_free(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_PCM_A2DP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));

	if (!vbc_codec)
		return 0;
	hw_param_lock_mtx(scene_id, stream);
	hw_param_dec_ref(scene_id, stream);
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_pcm_a2dp_trigger(struct snd_pcm_substream *substream, int cmd,
				  struct snd_soc_dai *dai)
{
	int up_down;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_PCM_A2DP;
	int ret;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s, cmd=%d\n", __func__,
		dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream), cmd);
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	up_down = triggered_flag(cmd);
	/* default ret is 0 */
	ret = 0;
	if (up_down == 1) {
		trigger_lock_spin(scene_id, stream);
		trigger_add_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 1)
			ret = dsp_trigger(vbc_codec, scene_id, stream, up_down);
		trigger_unlock_spin(scene_id, stream);
	} else {
		trigger_lock_spin(scene_id, stream);
		trigger_dec_ref(scene_id, stream);
		trigger_unlock_spin(scene_id, stream);
	}

	return ret;
}

static struct snd_soc_dai_ops pcm_a2dp_ops = {
	.probe = sprd_dai_vbc_probe,
	.startup = scene_pcm_a2dp_startup,
	.shutdown = scene_pcm_a2dp_shutdown,
	.hw_params = scene_pcm_a2dp_hw_params,
	.trigger = scene_pcm_a2dp_trigger,
	.hw_free = scene_pcm_a2dp_hw_free,
};

/* voice */
static int scene_voice_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_VOICE;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int ret = 0;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	startup_lock_mtx(scene_id, stream);
	startup_add_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 1) {
		ret = dsp_startup(vbc_codec, scene_id, stream);
		if (ret)
			startup_dec_ref(scene_id, stream);
		else
			set_scene_flag(scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);

	return ret;
}

static void scene_voice_shutdown(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_VOICE;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return;
	}

	if (!vbc_codec)
		return;
	startup_lock_mtx(scene_id, stream);
	startup_dec_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 0) {
		clr_scene_flag(scene_id, stream);
		dsp_shutdown(vbc_codec, scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);
}

static int scene_voice_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int rate;
	int data_fmt = VBC_DAT_L16;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_VOICE;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int chan_cnt;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		data_fmt = VBC_DAT_L16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data_fmt = VBC_DAT_L24;
		break;
	default:
		pr_err("%s, ERR:VBC not support data fmt =%d", __func__,
		       data_fmt);
		break;
	}
	chan_cnt = params_channels(params);
	rate = params_rate(params);
	pr_info("%s data_fmt=%s, chan=%u, rate =%u\n", __func__,
		vbc_data_fmt_to_str(data_fmt), chan_cnt, rate);
	if (chan_cnt > 2)
		pr_warn("%s channel count invalid\n", __func__);


	hw_param_lock_mtx(scene_id, stream);
	hw_param_add_ref(scene_id, stream);
	if (hw_param_get_ref(scene_id, stream) == 1) {
		dsp_hw_params(vbc_codec, scene_id, stream,
			      chan_cnt, rate, data_fmt);
	}
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_voice_hw_free(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_VOICE;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	hw_param_lock_mtx(scene_id, stream);
	hw_param_dec_ref(scene_id, stream);
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_voice_trigger(struct snd_pcm_substream *substream, int cmd,
			       struct snd_soc_dai *dai)
{
	int up_down;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_VOICE;
	int ret;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s, cmd=%d\n", __func__,
		dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream), cmd);
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	up_down = triggered_flag(cmd);
	/* default ret is 0 */
	ret = 0;
	if (up_down == 1) {
		trigger_lock_spin(scene_id, stream);
		trigger_add_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 1)
			ret = dsp_trigger(vbc_codec, scene_id, stream, up_down);
		trigger_unlock_spin(scene_id, stream);
	} else {
		trigger_lock_spin(scene_id, stream);
		trigger_dec_ref(scene_id, stream);
		trigger_unlock_spin(scene_id, stream);
	}

	return ret;
}

static struct snd_soc_dai_ops voice_ops = {
	.probe = sprd_dai_vbc_probe,
	.startup = scene_voice_startup,
	.shutdown = scene_voice_shutdown,
	.hw_params = scene_voice_hw_params,
	.trigger = scene_voice_trigger,
	.hw_free = scene_voice_hw_free,
};

/* voice capture*/
static int scene_voice_capture_startup(struct snd_pcm_substream *substream,
				       struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_VOICE_CAPTURE;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int ret = 0;
	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	startup_lock_mtx(scene_id, stream);
	startup_add_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 1) {
		ret = dsp_startup(vbc_codec, scene_id, stream);
		if (ret)
			startup_dec_ref(scene_id, stream);
		else
			set_scene_flag(scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);

	return ret;
}

static void scene_voice_capture_shutdown(struct snd_pcm_substream *substream,
					 struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_VOICE_CAPTURE;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return;
	}

	if (!vbc_codec)
		return;
	startup_lock_mtx(scene_id, stream);
	startup_dec_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 0) {
		clr_scene_flag(scene_id, stream);
		dsp_shutdown(vbc_codec, scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);
}

static int scene_voice_capture_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int rate;
	int data_fmt = VBC_DAT_L16;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_VOICE_CAPTURE;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int chan_cnt;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		data_fmt = VBC_DAT_L16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data_fmt = VBC_DAT_L24;
		break;
	default:
		pr_err("%s, ERR:VBC not support data fmt =%d", __func__,
		       data_fmt);
		break;
	}
	chan_cnt = params_channels(params);
	rate = params_rate(params);
	pr_info("%s data_fmt=%s, chan=%u, rate =%u\n", __func__,
		vbc_data_fmt_to_str(data_fmt), chan_cnt, rate);
	if (chan_cnt > 2)
		pr_warn("%s channel count invalid\n", __func__);
	hw_param_lock_mtx(scene_id, stream);
	hw_param_add_ref(scene_id, stream);
	if (hw_param_get_ref(scene_id, stream) == 1) {
		dsp_hw_params(vbc_codec, scene_id, stream,
			      chan_cnt, rate, data_fmt);
	}
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_voice_capture_hw_free(struct snd_pcm_substream *substream,
				       struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_VOICE_CAPTURE;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	hw_param_lock_mtx(scene_id, stream);
	hw_param_dec_ref(scene_id, stream);
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_voice_capture_trigger(struct snd_pcm_substream *substream,
				       int cmd, struct snd_soc_dai *dai)
{
	int up_down;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_VOICE_CAPTURE;
	int ret;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s, cmd=%d\n", __func__,
		dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream), cmd);
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;

	up_down = triggered_flag(cmd);
	/* default ret is 0 */
	ret = 0;
	if (up_down == 1) {
		trigger_lock_spin(scene_id, stream);
		trigger_add_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 1)
			ret = dsp_trigger(vbc_codec, scene_id, stream, up_down);
		trigger_unlock_spin(scene_id, stream);
	} else {
		trigger_lock_spin(scene_id, stream);
		trigger_dec_ref(scene_id, stream);
		trigger_unlock_spin(scene_id, stream);
	}

	return ret;
}

static struct snd_soc_dai_ops voice_capture_ops = {
	.probe = sprd_dai_vbc_probe,
	.startup = scene_voice_capture_startup,
	.shutdown = scene_voice_capture_shutdown,
	.hw_params = scene_voice_capture_hw_params,
	.trigger = scene_voice_capture_trigger,
	.hw_free = scene_voice_capture_hw_free,
};

/* voice pcm */
static int scene_voice_pcm_startup(struct snd_pcm_substream *substream,
				       struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_VOICE_PCM_P;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int ret = 0;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	startup_lock_mtx(scene_id, stream);
	startup_add_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 1) {
		set_scene_flag(scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);

	return ret;
}

static void scene_voice_pcm_shutdown(struct snd_pcm_substream *substream,
					 struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_VOICE_PCM_P;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return;
	}

	if (!vbc_codec)
		return;
	startup_lock_mtx(scene_id, stream);
	startup_dec_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 0) {
		clr_scene_flag(scene_id, stream);
		dsp_vbc_voice_pcm_play_set(false,
			vbc_codec->voice_pcm_play_mode);
	}
	startup_unlock_mtx(scene_id, stream);
}

static int scene_voice_pcm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int rate;
	int data_fmt = VBC_DAT_L16;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_VOICE_PCM_P;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int chan_cnt;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		data_fmt = VBC_DAT_L16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data_fmt = VBC_DAT_L24;
		break;
	default:
		pr_err("%s, ERR:VBC not support data fmt =%d", __func__,
		       data_fmt);
		break;
	}
	chan_cnt = params_channels(params);
	rate = params_rate(params);
	pr_info("%s data_fmt=%s, chan=%u, rate =%u\n", __func__,
		vbc_data_fmt_to_str(data_fmt), chan_cnt, rate);
	if (chan_cnt > 2)
		pr_warn("%s channel count invalid\n", __func__);

	hw_param_lock_mtx(scene_id, stream);
	hw_param_add_ref(scene_id, stream);
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_voice_pcm_hw_free(struct snd_pcm_substream *substream,
				       struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_VOICE_PCM_P;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	hw_param_lock_mtx(scene_id, stream);
	hw_param_dec_ref(scene_id, stream);
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_voice_pcm_trigger(struct snd_pcm_substream *substream,
				       int cmd, struct snd_soc_dai *dai)
{
	int up_down;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_VOICE_PCM_P;
	int ret;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s, cmd=%d\n", __func__,
		dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream), cmd);
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;

	up_down = triggered_flag(cmd);
	/* default ret is 0 */
	ret = 0;
	if (up_down == 1) {
		trigger_lock_spin(scene_id, stream);
		trigger_add_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 1) {
			dsp_vbc_voice_pcm_play_set(true,
				vbc_codec->voice_pcm_play_mode);
		}
		trigger_unlock_spin(scene_id, stream);
	} else {
		trigger_lock_spin(scene_id, stream);
		trigger_dec_ref(scene_id, stream);
		trigger_unlock_spin(scene_id, stream);
	}

	return ret;
}

static struct snd_soc_dai_ops voice_pcm_ops = {
	.probe = sprd_dai_vbc_probe,
	.startup = scene_voice_pcm_startup,
	.shutdown = scene_voice_pcm_shutdown,
	.hw_params = scene_voice_pcm_hw_params,
	.trigger = scene_voice_pcm_trigger,
	.hw_free = scene_voice_pcm_hw_free,
};

/* voip */
static int scene_voip_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_VOIP;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int ret = 0;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	startup_lock_mtx(scene_id, stream);
	startup_add_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 1) {
		ret = dsp_startup(vbc_codec, scene_id, stream);
		if (ret)
			startup_dec_ref(scene_id, stream);
		else
			set_scene_flag(scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);

	return ret;
}

static void scene_voip_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_VOIP;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return;
	}

	if (!vbc_codec)
		return;
	startup_lock_mtx(scene_id, stream);
	startup_dec_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 0) {
		clr_scene_flag(scene_id, stream);
		dsp_shutdown(vbc_codec, scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);
}

static int scene_voip_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int rate;
	int data_fmt = VBC_DAT_L16;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_VOIP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int chan_cnt;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		data_fmt = VBC_DAT_L16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data_fmt = VBC_DAT_L24;
		break;
	default:
		pr_err("%s, ERR:VBC not support data fmt =%d", __func__,
		       data_fmt);
		break;
	}
	chan_cnt = params_channels(params);
	rate = params_rate(params);
	pr_info("%s data_fmt=%s, chan=%u, rate =%u\n", __func__,
		vbc_data_fmt_to_str(data_fmt), chan_cnt, rate);
	if (chan_cnt > 2)
		pr_warn("%s channel count invalid\n", __func__);
	hw_param_lock_mtx(scene_id, stream);
	hw_param_add_ref(scene_id, stream);
	if (hw_param_get_ref(scene_id, stream) == 1) {
		dsp_hw_params(vbc_codec, scene_id, stream,
			      chan_cnt, rate, data_fmt);
	}
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_voip_hw_free(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_VOIP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	hw_param_lock_mtx(scene_id, stream);
	hw_param_dec_ref(scene_id, stream);
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_voip_trigger(struct snd_pcm_substream *substream, int cmd,
			      struct snd_soc_dai *dai)
{
	int up_down;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_VOIP;
	int ret;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s, cmd=%d\n", __func__,
		dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream), cmd);
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	up_down = triggered_flag(cmd);
	/* default ret is 0 */
	ret = 0;
	if (up_down == 1) {
		trigger_lock_spin(scene_id, stream);
		trigger_add_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 1)
			ret = dsp_trigger(vbc_codec, scene_id, stream, up_down);
		trigger_unlock_spin(scene_id, stream);
	} else {
		trigger_lock_spin(scene_id, stream);
		trigger_dec_ref(scene_id, stream);
		trigger_unlock_spin(scene_id, stream);
	}

	return ret;
}

static struct snd_soc_dai_ops voip_ops = {
	.probe = sprd_dai_vbc_probe,
	.startup = scene_voip_startup,
	.shutdown = scene_voip_shutdown,
	.hw_params = scene_voip_hw_params,
	.trigger = scene_voip_trigger,
	.hw_free = scene_voip_hw_free,
};

/* loop */
static int scene_loop_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_LOOP;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int ret = 0;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	startup_lock_mtx(scene_id, stream);
	startup_add_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 1) {
		ret = dsp_startup(vbc_codec, scene_id, stream);
		if (ret)
			startup_dec_ref(scene_id, stream);
		else
			set_scene_flag(scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);

	return ret;
}

static void scene_loop_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_LOOP;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return;
	}

	if (!vbc_codec)
		return;
	startup_lock_mtx(scene_id, stream);
	startup_dec_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 0) {
		clr_scene_flag(scene_id, stream);
		dsp_shutdown(vbc_codec, scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);
}

static int scene_loop_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int rate;
	int data_fmt = VBC_DAT_L16;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_LOOP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int chan_cnt;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		data_fmt = VBC_DAT_L16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data_fmt = VBC_DAT_L24;
		break;
	default:
		pr_err("%s, ERR:VBC not support data fmt =%d", __func__,
		       data_fmt);
		break;
	}
	chan_cnt = params_channels(params);
	rate = params_rate(params);
	pr_info("%s data_fmt=%s, chan=%u, rate =%u\n", __func__,
		vbc_data_fmt_to_str(data_fmt), chan_cnt, rate);
	if (chan_cnt > 2)
		pr_warn("%s channel count invalid\n", __func__);
	hw_param_lock_mtx(scene_id, stream);
	hw_param_add_ref(scene_id, stream);
	if (hw_param_get_ref(scene_id, stream) == 1) {
		dsp_hw_params(vbc_codec, scene_id, stream,
			      chan_cnt, rate, data_fmt);
	}
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_loop_hw_free(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_LOOP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	hw_param_lock_mtx(scene_id, stream);
	hw_param_dec_ref(scene_id, stream);
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_loop_trigger(struct snd_pcm_substream *substream, int cmd,
			      struct snd_soc_dai *dai)
{
	int up_down;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_LOOP;
	int ret;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s, cmd = %d\n", __func__,
		dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream), cmd);
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	up_down = triggered_flag(cmd);
	/* default ret is 0 */
	ret = 0;
	if (up_down == 1) {
		trigger_lock_spin(scene_id, stream);
		trigger_add_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 1)
			ret = dsp_trigger(vbc_codec, scene_id, stream, up_down);
		trigger_unlock_spin(scene_id, stream);
	} else {
		trigger_lock_spin(scene_id, stream);
		trigger_dec_ref(scene_id, stream);
		trigger_unlock_spin(scene_id, stream);
	}

	return ret;
}

static struct snd_soc_dai_ops loop_ops = {
	.probe = sprd_dai_vbc_probe,
	.startup = scene_loop_startup,
	.shutdown = scene_loop_shutdown,
	.hw_params = scene_loop_hw_params,
	.trigger = scene_loop_trigger,
	.hw_free = scene_loop_hw_free,
};

/* FM */
static int scene_fm_startup(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FM;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int ret = 0;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	startup_lock_mtx(scene_id, stream);
	startup_add_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 1) {
		pr_info("%s, force_on_xtl\n", __func__);
		force_on_xtl(true);
		ret = dsp_startup(vbc_codec, scene_id, stream);
		if (ret) {
			startup_dec_ref(scene_id, stream);
			force_on_xtl(false);
		} else
			set_scene_flag(scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);

	return ret;
}

static void scene_fm_shutdown(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FM;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return;
	}

	if (!vbc_codec)
		return;
	startup_lock_mtx(scene_id, stream);
	startup_dec_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 0) {
		clr_scene_flag(scene_id, stream);
		pr_info("%s, force_off_xtl\n", __func__);
		force_on_xtl(false);
		dsp_shutdown(vbc_codec, scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);
}

static int scene_fm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int rate;
	int data_fmt = VBC_DAT_L16;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FM;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int chan_cnt;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		data_fmt = VBC_DAT_L16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data_fmt = VBC_DAT_L24;
		break;
	default:
		pr_err("%s, ERR:VBC not support data fmt =%d", __func__,
		       data_fmt);
		break;
	}
	chan_cnt = params_channels(params);
	rate = params_rate(params);
	pr_info("%s data_fmt=%s, chan=%u, rate =%u\n", __func__,
		vbc_data_fmt_to_str(data_fmt), chan_cnt, rate);
	if (chan_cnt > 2)
		pr_warn("%s channel count invalid\n", __func__);
	hw_param_lock_mtx(scene_id, stream);
	hw_param_add_ref(scene_id, stream);
	if (hw_param_get_ref(scene_id, stream) == 1) {
		dsp_hw_params(vbc_codec, scene_id, stream,
			      chan_cnt, rate, data_fmt);
	}
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_fm_hw_free(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FM;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	hw_param_lock_mtx(scene_id, stream);
	hw_param_dec_ref(scene_id, stream);
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_fm_trigger(struct snd_pcm_substream *substream, int cmd,
			    struct snd_soc_dai *dai)
{
	int up_down;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FM;
	int ret;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s, cmd=%d\n", __func__,
		dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream), cmd);
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	up_down = triggered_flag(cmd);
	/* default ret is 0 */
	ret = 0;
	if (up_down == 1) {
		trigger_lock_spin(scene_id, stream);
		trigger_add_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 1)
			ret = dsp_trigger(vbc_codec, scene_id, stream, up_down);
		trigger_unlock_spin(scene_id, stream);
	} else {
		trigger_lock_spin(scene_id, stream);
		trigger_dec_ref(scene_id, stream);
		trigger_unlock_spin(scene_id, stream);
	}

	return ret;
}

static struct snd_soc_dai_ops fm_ops = {
	.probe = sprd_dai_vbc_probe,
	.startup = scene_fm_startup,
	.shutdown = scene_fm_shutdown,
	.hw_params = scene_fm_hw_params,
	.trigger = scene_fm_trigger,
	.hw_free = scene_fm_hw_free,
};

/* bt capture*/
static int scene_bt_capture_startup(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_BT_CAPTURE_AP;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int ret = 0;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	startup_lock_mtx(scene_id, stream);
	startup_add_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 1) {
		ret = dsp_startup(vbc_codec, scene_id, stream);
		if (ret)
			startup_dec_ref(scene_id, stream);
		else
			set_scene_flag(scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);

	return ret;
}

static void scene_bt_capture_shutdown(struct snd_pcm_substream *substream,
				      struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_BT_CAPTURE_AP;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return;
	}

	if (!vbc_codec)
		return;
	startup_lock_mtx(scene_id, stream);
	startup_dec_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 0) {
		clr_scene_flag(scene_id, stream);
		dsp_shutdown(vbc_codec, scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);
}

static int scene_bt_capture_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int rate;
	int data_fmt = VBC_DAT_L16;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_BT_CAPTURE_AP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int chan_cnt;
	int vbc_chan = VBC_ALL_CHAN;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		data_fmt = VBC_DAT_L16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data_fmt = VBC_DAT_L24;
		break;
	default:
		pr_err("%s, ERR:VBC not support data fmt =%d", __func__,
		       data_fmt);
		break;
	}
	chan_cnt = params_channels(params);
	rate = params_rate(params);
	pr_info("%s data_fmt=%s, chan=%u, rate =%u\n", __func__,
		vbc_data_fmt_to_str(data_fmt), chan_cnt, rate);
	if (chan_cnt > 2)
		pr_warn("%s channel count invalid\n", __func__);
	hw_param_lock_mtx(scene_id, stream);
	hw_param_add_ref(scene_id, stream);
	if (hw_param_get_ref(scene_id, stream) == 1) {
		vbc_chan = to_vbc_chan(chan_cnt);
		set_vbc_chan(scene_id, stream, vbc_chan);
		ap_hw_params(vbc_codec, scene_id, stream, vbc_chan, rate,
			     data_fmt);
		dsp_hw_params(vbc_codec, scene_id, stream,
			      chan_cnt, rate, data_fmt);
	}
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_bt_capture_hw_free(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_BT_CAPTURE_AP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;

	hw_param_lock_mtx(scene_id, stream);
	hw_param_dec_ref(scene_id, stream);
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_bt_capture_trigger(struct snd_pcm_substream *substream,
				    int cmd, struct snd_soc_dai *dai)
{
	int up_down;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_BT_CAPTURE_AP;
	int ret;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int vbc_chan = VBC_ALL_CHAN;

	pr_info("%s dai:%s(%d) scene:%s %s, cmd=%d\n", __func__,
		dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream), cmd);
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	up_down = triggered_flag(cmd);
	/* default ret is 0 */
	ret = 0;
	if (up_down == 1) {
		trigger_lock_spin(scene_id, stream);
		trigger_add_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 1) {
			vbc_chan = get_vbc_chan(scene_id, stream);
			ap_trigger(vbc_codec, scene_id, stream, vbc_chan,
				   up_down);
			ret = dsp_trigger(vbc_codec, scene_id, stream, up_down);
			normal_vbc_protect_spin_unlock(stream);
		}
		trigger_unlock_spin(scene_id, stream);
	} else {
		trigger_lock_spin(scene_id, stream);
		trigger_dec_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 0) {
			vbc_chan = get_vbc_chan(scene_id, stream);
			ap_trigger(vbc_codec, scene_id, stream, vbc_chan,
				   up_down);
		}
		trigger_unlock_spin(scene_id, stream);
	}

	return ret;
}


static struct snd_soc_dai_ops bt_capture_ops = {
	.probe = sprd_dai_vbc_probe,
	.startup = scene_bt_capture_startup,
	.shutdown = scene_bt_capture_shutdown,
	.hw_params = scene_bt_capture_hw_params,
	.trigger = scene_bt_capture_trigger,
	.hw_free = scene_bt_capture_hw_free,
};

/* fm capture*/
static int scene_fm_capture_startup(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FM_CAPTURE_AP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;

	startup_lock_mtx(scene_id, stream);
	startup_add_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 1)
		set_scene_flag(scene_id, stream);

	startup_unlock_mtx(scene_id, stream);

	return 0;
}

static void scene_fm_capture_shutdown(struct snd_pcm_substream *substream,
				      struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FM_CAPTURE_AP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return;
	}

	if (!vbc_codec)
		return;
	startup_lock_mtx(scene_id, stream);
	startup_dec_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 0)
		clr_scene_flag(scene_id, stream);

	startup_unlock_mtx(scene_id, stream);
}

static int scene_fm_capture_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int rate;
	int data_fmt = VBC_DAT_L16;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FM_CAPTURE_AP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int chan_cnt;
	int vbc_chan = VBC_ALL_CHAN;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		data_fmt = VBC_DAT_L16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data_fmt = VBC_DAT_L24;
		break;
	default:
		pr_err("%s, ERR:VBC not support data fmt =%d", __func__,
		       data_fmt);
		break;
	}
	chan_cnt = params_channels(params);
	rate = params_rate(params);
	pr_info("%s data_fmt=%s, chan=%u, rate =%u\n", __func__,
		vbc_data_fmt_to_str(data_fmt), chan_cnt, rate);
	if (chan_cnt > 2)
		pr_warn("%s channel count invalid\n", __func__);
	hw_param_lock_mtx(scene_id, stream);
	hw_param_add_ref(scene_id, stream);
	if (hw_param_get_ref(scene_id, stream) == 1) {
		vbc_chan = to_vbc_chan(chan_cnt);
		set_vbc_chan(scene_id, stream, vbc_chan);
		ap_hw_params(vbc_codec, scene_id, stream, vbc_chan, rate,
			     data_fmt);
	}
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_fm_capture_hw_free(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FM_CAPTURE_AP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	hw_param_lock_mtx(scene_id, stream);
	hw_param_dec_ref(scene_id, stream);
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_fm_capture_trigger(struct snd_pcm_substream *substream,
				    int cmd, struct snd_soc_dai *dai)
{
	int up_down;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FM_CAPTURE_AP;
	int ret;
	int vbc_chan = VBC_ALL_CHAN;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s cmd=%d\n", __func__,
		dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream), cmd);
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;

	up_down = triggered_flag(cmd);
	/* default ret is 0 */
	ret = 0;
	if (up_down == 1) {
		trigger_lock_spin(scene_id, stream);
		trigger_add_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 1) {
			vbc_chan = get_vbc_chan(scene_id, stream);
			ap_trigger(vbc_codec, scene_id, stream, vbc_chan,
				   up_down);
		}
		trigger_unlock_spin(scene_id, stream);
	} else {
		trigger_lock_spin(scene_id, stream);
		trigger_dec_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 0) {
			vbc_chan = get_vbc_chan(scene_id, stream);
			ap_trigger(vbc_codec, scene_id, stream, vbc_chan,
				   up_down);
		}
		trigger_unlock_spin(scene_id, stream);
	}

	return ret;
}

static struct snd_soc_dai_ops fm_capture_ops = {
	.probe = sprd_dai_vbc_probe,
	.startup = scene_fm_capture_startup,
	.shutdown = scene_fm_capture_shutdown,
	.hw_params = scene_fm_capture_hw_params,
	.trigger = scene_fm_capture_trigger,
	.hw_free = scene_fm_capture_hw_free,
};

/* capture fm dsp */
static int scene_capture_fm_dsp_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FM_CAPTURE_DSP;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int ret = 0;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	startup_lock_mtx(scene_id, stream);
	startup_add_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 1) {
		ret = dsp_startup(vbc_codec, scene_id, stream);
		if (ret)
			startup_dec_ref(scene_id, stream);
		else
			set_scene_flag(scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);

	return ret;
}

static void scene_capture_fm_dsp_shutdown(struct snd_pcm_substream *substream,
				       struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FM_CAPTURE_DSP;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return;
	}

	if (!vbc_codec)
		return;
	startup_lock_mtx(scene_id, stream);
	startup_dec_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 0) {
		clr_scene_flag(scene_id, stream);
		dsp_shutdown(vbc_codec, scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);

}
static int scene_capture_fm_dsp_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int rate;
	int data_fmt = VBC_DAT_L16;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FM_CAPTURE_DSP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int chan_cnt;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		data_fmt = VBC_DAT_L16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data_fmt = VBC_DAT_L24;
		break;
	default:
		pr_err("%s, ERR:VBC not support data fmt =%d", __func__,
			   data_fmt);
		break;
	}
	chan_cnt = params_channels(params);
	rate = params_rate(params);
	pr_info("%s data_fmt=%s, chan=%u, rate =%u\n", __func__,
		vbc_data_fmt_to_str(data_fmt), chan_cnt, rate);

	if (chan_cnt > 2)
		pr_warn("%s channel count invalid\n", __func__);

	hw_param_lock_mtx(scene_id, stream);
	hw_param_add_ref(scene_id, stream);
	if (hw_param_get_ref(scene_id, stream) == 1) {
		dsp_hw_params(vbc_codec, scene_id, stream,
				  chan_cnt, rate, data_fmt);
	}
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}


static int scene_capture_fm_dsp_hw_free(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FM_CAPTURE_DSP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	hw_param_lock_mtx(scene_id, stream);
	hw_param_dec_ref(scene_id, stream);
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}


static int scene_capture_fm_dsp_trigger(struct snd_pcm_substream *substream,
				     int cmd, struct snd_soc_dai *dai)
{
	int up_down;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FM_CAPTURE_DSP;
	int ret;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s, cmd=%d\n", __func__,
		dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream), cmd);
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	up_down = triggered_flag(cmd);
	/* default ret is 0 */
	ret = 0;
	if (up_down == 1) {
		trigger_lock_spin(scene_id, stream);
		trigger_add_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 1)
			ret = dsp_trigger(vbc_codec, scene_id, stream, up_down);

		trigger_unlock_spin(scene_id, stream);
	} else {
		trigger_lock_spin(scene_id, stream);
		trigger_dec_ref(scene_id, stream);
		trigger_unlock_spin(scene_id, stream);
	}

	return ret;
}

static struct snd_soc_dai_ops capture_fm_dsp_ops = {
	.probe = sprd_dai_vbc_probe,
	.startup = scene_capture_fm_dsp_startup,
	.shutdown = scene_capture_fm_dsp_shutdown,
	.hw_params = scene_capture_fm_dsp_hw_params,
	.trigger = scene_capture_fm_dsp_trigger,
	.hw_free = scene_capture_fm_dsp_hw_free,
};

/* capture btsco dsp */
static int scene_capture_btsco_dsp_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_BT_SCO_CAPTURE_DSP;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int ret = 0;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	startup_lock_mtx(scene_id, stream);
	startup_add_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 1) {
		ret = dsp_startup(vbc_codec, scene_id, stream);
		if (ret)
			startup_dec_ref(scene_id, stream);
		else
			set_scene_flag(scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);

	return ret;
}

static void scene_capture_btsco_dsp_shutdown(
	struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_BT_SCO_CAPTURE_DSP;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return;
	}

	if (!vbc_codec)
		return;
	startup_lock_mtx(scene_id, stream);
	startup_dec_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 0) {
		clr_scene_flag(scene_id, stream);
		dsp_shutdown(vbc_codec, scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);

}

static int scene_capture_btsco_dsp_hw_params(
	struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params,
	struct snd_soc_dai *dai)
{
	unsigned int rate;
	int data_fmt = VBC_DAT_L16;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_BT_SCO_CAPTURE_DSP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int chan_cnt;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		data_fmt = VBC_DAT_L16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data_fmt = VBC_DAT_L24;
		break;
	default:
		pr_err("%s, ERR:VBC not support data fmt =%d", __func__,
			   data_fmt);
		break;
	}
	chan_cnt = params_channels(params);
	rate = params_rate(params);
	pr_info("%s data_fmt=%s, chan=%u, rate =%u\n", __func__,
		vbc_data_fmt_to_str(data_fmt), chan_cnt, rate);

	if (chan_cnt > 2)
		pr_warn("%s channel count invalid\n", __func__);

	hw_param_lock_mtx(scene_id, stream);
	hw_param_add_ref(scene_id, stream);
	if (hw_param_get_ref(scene_id, stream) == 1) {
		dsp_hw_params(vbc_codec, scene_id, stream,
				  chan_cnt, rate, data_fmt);
	}
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}


static int scene_capture_btsco_dsp_hw_free(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_BT_SCO_CAPTURE_DSP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	hw_param_lock_mtx(scene_id, stream);
	hw_param_dec_ref(scene_id, stream);
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_capture_btsco_dsp_trigger(struct snd_pcm_substream *substream,
				     int cmd, struct snd_soc_dai *dai)
{
	int up_down;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_BT_SCO_CAPTURE_DSP;
	int ret;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s, cmd=%d\n", __func__,
		dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream), cmd);
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	up_down = triggered_flag(cmd);
	/* default ret is 0 */
	ret = 0;
	if (up_down == 1) {
		trigger_lock_spin(scene_id, stream);
		trigger_add_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 1)
			ret = dsp_trigger(vbc_codec, scene_id, stream, up_down);

		trigger_unlock_spin(scene_id, stream);
	} else {
		trigger_lock_spin(scene_id, stream);
		trigger_dec_ref(scene_id, stream);
		trigger_unlock_spin(scene_id, stream);
	}

	return ret;
}

static struct snd_soc_dai_ops capture_btsco_dsp_ops = {
	.probe = sprd_dai_vbc_probe,
	.startup = scene_capture_btsco_dsp_startup,
	.shutdown = scene_capture_btsco_dsp_shutdown,
	.hw_params = scene_capture_btsco_dsp_hw_params,
	.trigger = scene_capture_btsco_dsp_trigger,
	.hw_free = scene_capture_btsco_dsp_hw_free,
};

/* FM DSP */
static int scene_fm_dsp_startup(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FM_DSP;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int ret = 0;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	startup_lock_mtx(scene_id, stream);
	startup_add_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 1) {
		set_scene_flag(scene_id, stream);
		pr_info("%s, force_on_xtl\n", __func__);
		force_on_xtl(true);
		ret = dsp_startup(vbc_codec, scene_id, stream);
		if (ret) {
			startup_dec_ref(scene_id, stream);
			force_on_xtl(false);
		} else
			set_scene_flag(scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);

	return ret;
}

static void scene_fm_dsp_shutdown(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FM_DSP;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return;
	}

	if (!vbc_codec)
		return;
	startup_lock_mtx(scene_id, stream);
	startup_dec_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 0) {
		clr_scene_flag(scene_id, stream);
		pr_info("%s, force_off_xtl\n", __func__);
		force_on_xtl(false);
		dsp_shutdown(vbc_codec, scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);
}

static int scene_fm_dsp_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int rate;
	int data_fmt = VBC_DAT_L16;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FM_DSP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int chan_cnt;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		data_fmt = VBC_DAT_L16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data_fmt = VBC_DAT_L24;
		break;
	default:
		pr_err("%s, ERR:VBC not support data fmt =%d", __func__,
		       data_fmt);
		break;
	}
	chan_cnt = params_channels(params);
	rate = params_rate(params);
	pr_info("%s data_fmt=%s, chan=%u, rate =%u\n", __func__,
		vbc_data_fmt_to_str(data_fmt), chan_cnt, rate);
	if (chan_cnt > 2)
		pr_warn("%s channel count invalid\n", __func__);
	hw_param_lock_mtx(scene_id, stream);
	hw_param_add_ref(scene_id, stream);
	if (hw_param_get_ref(scene_id, stream) == 1) {
		dsp_hw_params(vbc_codec, scene_id, stream,
			      chan_cnt, rate, data_fmt);
	}
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_fm_dsp_hw_free(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FM_DSP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	hw_param_lock_mtx(scene_id, stream);
	hw_param_dec_ref(scene_id, stream);
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_fm_dsp_trigger(struct snd_pcm_substream *substream, int cmd,
			    struct snd_soc_dai *dai)
{
	int up_down;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FM_DSP;
	int ret;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s, cmd=%d\n", __func__,
		dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream), cmd);
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	up_down = triggered_flag(cmd);
	/* default ret is 0 */
	ret = 0;
	if (up_down == 1) {
		trigger_lock_spin(scene_id, stream);
		trigger_add_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 1)
			ret = dsp_trigger(vbc_codec, scene_id, stream, up_down);
		trigger_unlock_spin(scene_id, stream);
	} else {
		trigger_lock_spin(scene_id, stream);
		trigger_dec_ref(scene_id, stream);
		trigger_unlock_spin(scene_id, stream);
	}

	return ret;
}

static struct snd_soc_dai_ops fm_dsp_ops = {
	.probe = sprd_dai_vbc_probe,
	.startup = scene_fm_dsp_startup,
	.shutdown = scene_fm_dsp_shutdown,
	.hw_params = scene_fm_dsp_hw_params,
	.trigger = scene_fm_dsp_trigger,
	.hw_free = scene_fm_dsp_hw_free,
};

void scene_dump_set(enum vbc_dump_position_e pos)
{
	switch (pos) {
	default:
	case DUMP_POS_DAC0_E:
		dsp_vbc_mux_adc_set(VBC_MUX_IN_ADC0, ADC_IN_DAC0);
		break;
	case DUMP_POS_DAC1_E:
		dsp_vbc_mux_adc_set(VBC_MUX_IN_ADC0, ADC_IN_DAC1);
		break;
	case DUMP_POS_A4:
		dsp_vbc_mux_loop_da0_set(VBC_MUX_LOOP_DAC0, DAC0_MBDRC_OUT);
		dsp_vbc_mux_loop_da0_da1_set(VBC_MUX_LOOP_DAC0_DAC1,
					     DAC0_DAC1_SEL_DAC0);
		dsp_vbc_mux_adc_set(VBC_MUX_IN_ADC0, ADC_IN_DAC_LOOP);
		break;
	case DUMP_POS_A3:
		dsp_vbc_mux_loop_da0_set(VBC_MUX_LOOP_DAC0, DAC0_EQ4_OUT);
		dsp_vbc_mux_loop_da0_da1_set(VBC_MUX_LOOP_DAC0_DAC1,
					     DAC0_DAC1_SEL_DAC0);
		dsp_vbc_mux_adc_set(VBC_MUX_IN_ADC0, ADC_IN_DAC_LOOP);
		break;
	case DUMP_POS_A2:
		dsp_vbc_mux_loop_da0_set(VBC_MUX_LOOP_DAC0,
					 DAC0_MIX1_OUT);
		dsp_vbc_mux_loop_da0_da1_set(VBC_MUX_LOOP_DAC0_DAC1,
					     DAC0_DAC1_SEL_DAC0);
		dsp_vbc_mux_adc_set(VBC_MUX_IN_ADC0, ADC_IN_DAC_LOOP);
		break;
	case DUMP_POS_A1:
		dsp_vbc_mux_loop_da0_set(VBC_MUX_LOOP_DAC0,
					 DAC0_SMTHDG_OUT);
		dsp_vbc_mux_loop_da0_da1_set(VBC_MUX_LOOP_DAC0_DAC1,
					     DAC0_DAC1_SEL_DAC0);
		dsp_vbc_mux_adc_set(VBC_MUX_IN_ADC0, ADC_IN_DAC_LOOP);
		break;
	case DUMP_POS_V2:
		dsp_vbc_mux_loop_da1_set(VBC_MUX_LOOP_DAC1,
					 DAC1_MIXERDG_OUT);
		dsp_vbc_mux_loop_da0_da1_set(VBC_MUX_LOOP_DAC0_DAC1,
					     DAC0_DAC1_SEL_DAC1);
		dsp_vbc_mux_adc_set(VBC_MUX_IN_ADC0, ADC_IN_DAC_LOOP);
		break;
	case DUMP_POS_V1:
		dsp_vbc_mux_loop_da1_set(VBC_MUX_LOOP_DAC1,
					 DAC1_MIXER_OUT);
		dsp_vbc_mux_loop_da0_da1_set(VBC_MUX_LOOP_DAC0_DAC1,
					     DAC0_DAC1_SEL_DAC1);
		dsp_vbc_mux_adc_set(VBC_MUX_IN_ADC0, ADC_IN_DAC_LOOP);
		break;
	case DUMP_POS_DAC0_TO_ADC1:
		dsp_vbc_mux_adc_set(VBC_MUX_IN_ADC1, ADC_IN_DAC0);
		break;
	case DUMP_POS_DAC0_TO_ADC2:
		dsp_vbc_mux_adc_set(VBC_MUX_IN_ADC2, ADC_IN_DAC0);
		break;
	case DUMP_POS_DAC0_TO_ADC3:
		dsp_vbc_mux_adc_set(VBC_MUX_IN_ADC3, ADC_IN_DAC0);
		break;
	}
}

/* vbc dump */
static int scene_dump_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	enum vbc_dump_position_e pos;
	int scene_id = VBC_DAI_ID_NORMAL_AP01;
	int ret;

	pr_info("%s dai:%s(%d) %s, scene: %s\n", __func__,
		dai_id_to_str(dai->id), dai->id, stream_to_str(stream),
		scene_id_to_str(scene_id));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}
	if (!vbc_codec)
		return 0;
	startup_lock_mtx(scene_id, stream);
	startup_add_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 1) {
		ret = dsp_startup(vbc_codec, scene_id, stream);
		if (ret) {
			startup_dec_ref(scene_id, stream);
			startup_unlock_mtx(scene_id, stream);
			return ret;
		}
		pos = vbc_codec->vbc_dump_position;
		pr_info("dump scene pos = %s\n", vbc_dumppos2name(pos));
		scene_dump_set(pos);
		dsp_vbc_mux_audrcd_set(VBC_MUX_AUDRCD01, AUDRCD_IN_ADC0);
	}
	startup_unlock_mtx(scene_id, stream);

	return 0;
}

static void scene_dump_shutdown(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int scene_id = VBC_DAI_ID_NORMAL_AP01;

	pr_info("%s dai:%s(%d) %s, scene: %s\n", __func__,
		dai_id_to_str(dai->id), dai->id, stream_to_str(stream),
		scene_id_to_str(scene_id));
	if (!vbc_codec)
		return;
	startup_lock_mtx(scene_id, stream);
	startup_dec_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 0) {
		dsp_vbc_mux_audrcd_set(VBC_MUX_AUDRCD01, AUDRCD_IN_ADC0);
		dsp_vbc_mux_adc_set(VBC_MUX_IN_ADC0, ADC_IN_IIS0_ADC);
		dsp_shutdown(vbc_codec, scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);
}
static int scene_dump_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int rate;
	int data_fmt = VBC_DAT_L16;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_NORMAL_AP01;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int chan_cnt;
	int vbc_chan = VBC_ALL_CHAN;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		data_fmt = VBC_DAT_L16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data_fmt = VBC_DAT_L24;
		break;
	default:
		pr_err("%s, ERR:VBC not support data fmt =%d", __func__,
		       data_fmt);
		break;
	}
	chan_cnt = params_channels(params);
	rate = params_rate(params);
	pr_info("%s data_fmt=%s, chan=%u, rate =%u\n", __func__,
		vbc_data_fmt_to_str(data_fmt), chan_cnt, rate);
	if (chan_cnt > 2)
		pr_warn("%s channel count invalid\n", __func__);
	hw_param_lock_mtx(scene_id, stream);
	hw_param_add_ref(scene_id, stream);
	if (hw_param_get_ref(scene_id, stream) == 1) {
		vbc_chan = to_vbc_chan(chan_cnt);
		set_vbc_chan(scene_id, stream, vbc_chan);
		ap_hw_params(vbc_codec, scene_id, stream, vbc_chan, rate,
			     data_fmt);
	}
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_dump_hw_free(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_NORMAL_AP01;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	hw_param_lock_mtx(scene_id, stream);
	hw_param_dec_ref(scene_id, stream);
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_dump_trigger(struct snd_pcm_substream *substream,
				    int cmd, struct snd_soc_dai *dai)
{
	int up_down;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_NORMAL_AP01;
	int ret;
	int vbc_chan = VBC_ALL_CHAN;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s cmd=%d\n", __func__,
		dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream), cmd);
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;

	up_down = triggered_flag(cmd);
	/* default ret is 0 */
	ret = 0;
	if (up_down == 1) {
		trigger_lock_spin(scene_id, stream);
		trigger_add_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 1) {
			vbc_chan = get_vbc_chan(scene_id, stream);
			ap_trigger(vbc_codec, scene_id, stream, vbc_chan,
				   up_down);
		}
		trigger_unlock_spin(scene_id, stream);
	} else {
		trigger_lock_spin(scene_id, stream);
		trigger_dec_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 0) {
			vbc_chan = get_vbc_chan(scene_id, stream);
			ap_trigger(vbc_codec, scene_id, stream, vbc_chan,
				   up_down);
		}
		trigger_unlock_spin(scene_id, stream);
	}

	return ret;
}

static struct snd_soc_dai_ops vbc_dump_ops = {
	.probe = sprd_dai_vbc_probe,
	.startup = scene_dump_startup,
	.shutdown = scene_dump_shutdown,
	.hw_params = scene_dump_hw_params,
	.trigger = scene_dump_trigger,
	.hw_free = scene_dump_hw_free,
};

/* HFP */
static int scene_hfp_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_HFP;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int ret = 0;

	pr_debug("%s dai:%s(%d) scene:%s %s\n", __func__,
			dai_id_to_str(be_dai_id),
			be_dai_id, scene_id_to_str(scene_id),
			stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	startup_lock_mtx(scene_id, stream);
	startup_add_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 1) {
		ret = dsp_startup(vbc_codec, scene_id, stream);
		if (ret)
			startup_dec_ref(scene_id, stream);
		else
			set_scene_flag(scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);

	return ret;
}

static void scene_hfp_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_HFP;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_debug("%s dai:%s(%d) scene:%s %s\n", __func__,
			dai_id_to_str(be_dai_id),
			be_dai_id, scene_id_to_str(scene_id),
			stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return;
	}

	if (!vbc_codec)
		return;
	startup_lock_mtx(scene_id, stream);
	startup_dec_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 0) {
		clr_scene_flag(scene_id, stream);
		dsp_shutdown(vbc_codec, scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);
}

static int scene_hfp_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int rate;
	int data_fmt = VBC_DAT_L16;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_HFP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int chan_cnt;

	pr_debug("%s dai:%s(%d) scene:%s %s\n", __func__,
			dai_id_to_str(dai->id),
			dai->id, scene_id_to_str(scene_id),
			stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		data_fmt = VBC_DAT_L16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data_fmt = VBC_DAT_L24;
		break;
	default:
		pr_err("%s, ERR:VBC not support data fmt =%d", __func__,
		       data_fmt);
		break;
	}

	chan_cnt = params_channels(params);
	rate = params_rate(params);
	pr_debug("%s data_fmt=%s, chan=%u, rate =%u\n", __func__,
			vbc_data_fmt_to_str(data_fmt), chan_cnt, rate);
	if (chan_cnt > 2)
		pr_warn("%s channel count invalid\n", __func__);
	hw_param_lock_mtx(scene_id, stream);
	hw_param_add_ref(scene_id, stream);
	if (hw_param_get_ref(scene_id, stream) == 1) {
		dsp_hw_params(vbc_codec, scene_id, stream,
			      chan_cnt, rate, data_fmt);
	}
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_hfp_hw_free(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_HFP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_debug("%s dai:%s(%d) scene:%s %s\n", __func__,
			dai_id_to_str(dai->id),
			dai->id, scene_id_to_str(scene_id),
			stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	hw_param_lock_mtx(scene_id, stream);
	hw_param_dec_ref(scene_id, stream);
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_hfp_trigger(struct snd_pcm_substream *substream, int cmd,
			      struct snd_soc_dai *dai)
{
	int up_down;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_HFP;
	int ret;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_debug("%s dai:%s(%d) scene:%s %s, cmd=%d\n", __func__,
			dai_id_to_str(dai->id),
			dai->id, scene_id_to_str(scene_id),
			stream_to_str(stream), cmd);
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	up_down = triggered_flag(cmd);
	/* default ret is 0 */
	ret = 0;
	if (up_down == 1) {
		trigger_lock_spin(scene_id, stream);
		trigger_add_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 1)
			ret = dsp_trigger(vbc_codec, scene_id, stream, up_down);
		trigger_unlock_spin(scene_id, stream);
	} else {
		trigger_lock_spin(scene_id, stream);
		trigger_dec_ref(scene_id, stream);
		trigger_unlock_spin(scene_id, stream);
	}

	return ret;
}

static struct snd_soc_dai_ops hfp_ops = {
	.probe = sprd_dai_vbc_probe,
	.startup = scene_hfp_startup,
	.shutdown = scene_hfp_shutdown,
	.hw_params = scene_hfp_hw_params,
	.trigger = scene_hfp_trigger,
	.hw_free = scene_hfp_hw_free,
};

static int scene_hifi_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = AUDCP_DAI_ID_HIFI;
	int be_dai_id = dai->id;
	int ret = 0;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	startup_lock_mtx(scene_id, stream);
	startup_add_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 1) {
		ret = hifi_startup(scene_id, stream);
		if (ret)
			startup_dec_ref(scene_id, stream);
		else
			set_scene_flag(scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);

	return ret;
}

static void scene_hifi_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = AUDCP_DAI_ID_HIFI;
	int be_dai_id = dai->id;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return;
	}

	startup_lock_mtx(scene_id, stream);
	startup_dec_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 0) {
		clr_scene_flag(scene_id, stream);
		hifi_shutdown(scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);
}

static int scene_hifi_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int rate;
	int data_fmt = VBC_DAT_L16;
	int stream = substream->stream;
	int scene_id = AUDCP_DAI_ID_HIFI;
	int chan_cnt;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		data_fmt = VBC_DAT_L16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data_fmt = VBC_DAT_L24;
		break;
	default:
		pr_err("%s, ERR:VBC not support data fmt =%d", __func__,
		       data_fmt);
		break;
	}
	chan_cnt = params_channels(params);
	rate = params_rate(params);
	pr_info("%s data_fmt=%s, chan=%u, rate =%u\n", __func__,
		vbc_data_fmt_to_str(data_fmt), chan_cnt, rate);

	if (chan_cnt > 2)
		pr_warn("%s channel count invalid\n", __func__);

	hw_param_lock_mtx(scene_id, stream);
	hw_param_add_ref(scene_id, stream);
	if (hw_param_get_ref(scene_id, stream) == 1) {
		hifi_hw_params(scene_id, stream,
			      chan_cnt, rate, data_fmt);
	}
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_hifi_hw_free(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = AUDCP_DAI_ID_HIFI;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	hw_param_lock_mtx(scene_id, stream);
	hw_param_dec_ref(scene_id, stream);
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_hifi_trigger(struct snd_pcm_substream *substream, int cmd,
			      struct snd_soc_dai *dai)
{
	int up_down;
	int stream = substream->stream;
	int scene_id = AUDCP_DAI_ID_HIFI;
	int ret;

	pr_info("%s dai:%s(%d) scene:%s %s, cmd=%d\n", __func__,
		dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream), cmd);
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}
	up_down = triggered_flag(cmd);
	ret = 0;
	if (up_down == 1) {
		trigger_lock_spin(scene_id, stream);
		trigger_add_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 1)
			ret = hifi_dsp_trigger(scene_id, stream, up_down);

		trigger_unlock_spin(scene_id, stream);
	} else {
		trigger_lock_spin(scene_id, stream);
		trigger_dec_ref(scene_id, stream);
		trigger_unlock_spin(scene_id, stream);
	}

	return ret;
}

static struct snd_soc_dai_ops hifi_ops = {
	.probe = sprd_dai_vbc_probe,
	.startup = scene_hifi_startup,
	.shutdown = scene_hifi_shutdown,
	.hw_params = scene_hifi_hw_params,
	.trigger = scene_hifi_trigger,
	.hw_free = scene_hifi_hw_free,
};

static int scene_hifi_fast_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = AUDCP_DAI_ID_FAST;
	int be_dai_id = dai->id;
	int ret = 0;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	startup_lock_mtx(scene_id, stream);
	startup_add_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 1) {
		ret = hifi_startup(scene_id, stream);
		if (ret)
			startup_dec_ref(scene_id, stream);
		else
			set_scene_flag(scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);

	return ret;
}

static void scene_hifi_fast_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = AUDCP_DAI_ID_FAST;
	int be_dai_id = dai->id;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return;
	}

	startup_lock_mtx(scene_id, stream);
	startup_dec_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 0) {
		clr_scene_flag(scene_id, stream);
		hifi_shutdown(scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);
}

static int scene_hifi_fast_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int rate;
	int data_fmt = VBC_DAT_L16;
	int stream = substream->stream;
	int scene_id = AUDCP_DAI_ID_FAST;
	int chan_cnt;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		data_fmt = VBC_DAT_L16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data_fmt = VBC_DAT_L24;
		break;
	default:
		pr_err("%s, ERR:VBC not support data fmt =%d", __func__,
		       data_fmt);
		break;
	}
	chan_cnt = params_channels(params);
	rate = params_rate(params);
	pr_info("%s data_fmt=%s, chan=%u, rate =%u\n", __func__,
		vbc_data_fmt_to_str(data_fmt), chan_cnt, rate);

	if (chan_cnt > 2)
		pr_warn("%s channel count invalid\n", __func__);

	hw_param_lock_mtx(scene_id, stream);
	hw_param_add_ref(scene_id, stream);
	if (hw_param_get_ref(scene_id, stream) == 1) {
		hifi_hw_params(scene_id, stream,
			      chan_cnt, rate, data_fmt);
	}
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_hifi_fast_hw_free(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = AUDCP_DAI_ID_FAST;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	hw_param_lock_mtx(scene_id, stream);
	hw_param_dec_ref(scene_id, stream);
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_hifi_fast_trigger(struct snd_pcm_substream *substream, int cmd,
			      struct snd_soc_dai *dai)
{
	int up_down;
	int stream = substream->stream;
	int scene_id = AUDCP_DAI_ID_FAST;
	int ret;

	pr_info("%s dai:%s(%d) scene:%s %s, cmd=%d\n", __func__,
		dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream), cmd);
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}
	up_down = triggered_flag(cmd);
	ret = 0;
	if (up_down == 1) {
		trigger_lock_spin(scene_id, stream);
		trigger_add_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 1)
			ret = hifi_dsp_trigger(scene_id, stream, up_down);

		trigger_unlock_spin(scene_id, stream);
	} else {
		trigger_lock_spin(scene_id, stream);
		trigger_dec_ref(scene_id, stream);
		trigger_unlock_spin(scene_id, stream);
	}

	return ret;
}

static struct snd_soc_dai_ops hifi_fast_ops = {
	.probe = sprd_dai_vbc_probe,
	.startup = scene_hifi_fast_startup,
	.shutdown = scene_hifi_fast_shutdown,
	.hw_params = scene_hifi_fast_hw_params,
	.trigger = scene_hifi_fast_trigger,
	.hw_free = scene_hifi_fast_hw_free,
};

/* smtpa fast */
static int scene_smtpa_fast_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FAST_P_SMART_AMP;
	int be_dai_id = dai->id;
	int ret = 0;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	startup_lock_mtx(scene_id, stream);
	startup_add_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 1) {
		ret = dsp_startup(vbc_codec, scene_id, stream);
		if (ret)
			startup_dec_ref(scene_id, stream);
		else
			set_scene_flag(scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);

	return ret;
}

static void scene_smtpa_fast_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FAST_P_SMART_AMP;
	int be_dai_id = dai->id;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__,
		dai_id_to_str(be_dai_id),
		be_dai_id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(be_dai_id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return;
	}

	if (!vbc_codec)
		return;
	startup_lock_mtx(scene_id, stream);
	startup_dec_ref(scene_id, stream);
	if (startup_get_ref(scene_id, stream) == 0) {
		clr_scene_flag(scene_id, stream);
		dsp_shutdown(vbc_codec, scene_id, stream);
	}
	startup_unlock_mtx(scene_id, stream);
}

static int scene_smtpa_fast_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int rate;
	int data_fmt = VBC_DAT_L16;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FAST_P_SMART_AMP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);
	int chan_cnt;

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		data_fmt = VBC_DAT_L16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data_fmt = VBC_DAT_L24;
		break;
	default:
		pr_err("%s, ERR:VBC not support data fmt =%d", __func__,
		       data_fmt);
		break;
	}
	chan_cnt = params_channels(params);
	rate = params_rate(params);
	pr_info("%s data_fmt=%s, chan=%u, rate =%u\n", __func__,
		vbc_data_fmt_to_str(data_fmt), chan_cnt, rate);

	if (chan_cnt > 2)
		pr_warn("%s channel count invalid\n", __func__);

	hw_param_lock_mtx(scene_id, stream);
	hw_param_add_ref(scene_id, stream);
	if (hw_param_get_ref(scene_id, stream) == 1) {
		dsp_hw_params(vbc_codec, scene_id, stream,
			      chan_cnt, rate, data_fmt);
	}
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_smtpa_fast_hw_free(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FAST_P_SMART_AMP;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s\n", __func__, dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream));
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	hw_param_lock_mtx(scene_id, stream);
	hw_param_dec_ref(scene_id, stream);
	hw_param_unlock_mtx(scene_id, stream);

	return 0;
}

static int scene_smtpa_fast_trigger(struct snd_pcm_substream *substream, int cmd,
			      struct snd_soc_dai *dai)
{
	int up_down;
	int stream = substream->stream;
	int scene_id = VBC_DAI_ID_FAST_P_SMART_AMP;
	int ret;
	struct vbc_codec_priv *vbc_codec = dev_get_drvdata(dai->dev);

	pr_info("%s dai:%s(%d) scene:%s %s, cmd=%d\n", __func__,
		dai_id_to_str(dai->id),
		dai->id, scene_id_to_str(scene_id), stream_to_str(stream), cmd);
	if (scene_id != check_be_dai_id(dai->id)) {
		pr_err("%s check_be_dai_id failed\n", __func__);
		return -EINVAL;
	}

	if (!vbc_codec)
		return 0;
	up_down = triggered_flag(cmd);
	/* default ret is 0 */
	ret = 0;
	if (up_down == 1) {
		trigger_lock_spin(scene_id, stream);
		trigger_add_ref(scene_id, stream);
		if (trigger_get_ref(scene_id, stream) == 1)
			ret = dsp_trigger(vbc_codec, scene_id, stream, up_down);

		trigger_unlock_spin(scene_id, stream);
	} else {
		trigger_lock_spin(scene_id, stream);
		trigger_dec_ref(scene_id, stream);
		trigger_unlock_spin(scene_id, stream);
	}

	return ret;
}

static struct snd_soc_dai_ops smtpa_fast_ops = {
	.probe = sprd_dai_vbc_probe,
	.startup = scene_smtpa_fast_startup,
	.shutdown = scene_smtpa_fast_shutdown,
	.hw_params = scene_smtpa_fast_hw_params,
	.trigger = scene_smtpa_fast_trigger,
	.hw_free = scene_smtpa_fast_hw_free,
};

static struct snd_soc_dai_driver vbc_dais[BE_DAI_ID_MAX] = {
	/* 0: BE_DAI_ID_NORMAL_AP01_CODEC */
	{
		.name = TO_STRING(BE_DAI_ID_NORMAL_AP01_CODEC),
		.id = BE_DAI_ID_NORMAL_AP01_CODEC,
		.playback = {
			.stream_name = "BE_DAI_NORMAL_AP01_CODEC_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.capture = {
			.stream_name = "BE_DAI_NORMAL_AP01_CODEC_C",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &normal_ops,
	},
	/* 1: BE_DAI_ID_NORMAL_AP23_CODEC */
	{
		.name = TO_STRING(BE_DAI_ID_NORMAL_AP23_CODEC),
		.id = BE_DAI_ID_NORMAL_AP23_CODEC,
		.playback = {
			.stream_name = "BE_DAI_NORMAL_AP23_CODEC_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.capture = {
			.stream_name = "BE_DAI_NORMAL_AP23_CODEC_C",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &normal_ap23_ops,
	},
	/* 2: BE_DAI_ID_CAPTURE_DSP_CODEC */
	{
		.name = TO_STRING(BE_DAI_ID_CAPTURE_DSP_CODEC),
		.id = BE_DAI_ID_CAPTURE_DSP_CODEC,
		.capture = {
			.stream_name = "BE_DAI_CAP_DSP_CODEC_C",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &capture_dsp_ops,
	},
	/* 3: BE_DAI_ID_FAST_P_CODEC */
	{
		.name = TO_STRING(BE_DAI_ID_FAST_P_CODEC),
		.id = BE_DAI_ID_FAST_P_CODEC,
		.playback = {
			.stream_name = "BE_DAI_FAST_CODEC_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &fast_ops,
	},
	/* 4: BE_DAI_ID_OFFLOAD_CODEC */
	{
		.name = TO_STRING(BE_DAI_ID_OFFLOAD_CODEC),
		.id = BE_DAI_ID_OFFLOAD_CODEC,
		.playback = {
			.stream_name = "BE_DAI_OFFLOAD_CODEC_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &offload_ops,
	},
	/* 5: BE_DAI_ID_VOICE_CODEC */
	{
		.name = TO_STRING(BE_DAI_ID_VOICE_CODEC),
		.id = BE_DAI_ID_VOICE_CODEC,
		.playback = {
			.stream_name = "BE_DAI_VOICE_CODEC_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.capture = {
			.stream_name = "BE_DAI_VOICE_CODEC_C",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &voice_ops,
	},
	/* 6: BE_DAI_ID_VOIP_CODEC */
	{
		.name = TO_STRING(BE_DAI_ID_VOIP_CODEC),
		.id = BE_DAI_ID_VOIP_CODEC,
		.playback = {
			.stream_name = "BE_DAI_VOIP_CODEC_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.capture = {
			.stream_name = "BE_DAI_VOIP_CODEC_C",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &voip_ops,
	},
	/* 7: BE_DAI_ID_FM_CODEC */
	{
		.name = TO_STRING(BE_DAI_ID_FM_CODEC),
		.id = BE_DAI_ID_FM_CODEC,
		.playback = {
			.stream_name = "BE_DAI_FM_CODEC_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &fm_ops,
	},
	/* 8: BE_DAI_ID_LOOP_CODEC */
	{
		.name = TO_STRING(BE_DAI_ID_LOOP_CODEC),
		.id = BE_DAI_ID_LOOP_CODEC,
		.playback = {
			.stream_name = "BE_DAI_LOOP_CODEC_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.capture = {
			.stream_name = "BE_DAI_LOOP_CODEC_C",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &loop_ops,
	},
	/* 9: BE_DAI_ID_FM_DSP_CODEC */
	{
		.name = TO_STRING(BE_DAI_ID_FM_DSP_CODEC),
		.id = BE_DAI_ID_FM_DSP_CODEC,
		.playback = {
			.stream_name = "BE_DAI_FM_DSP_CODEC_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &fm_dsp_ops,
	},
	/* 10: BE_DAI_ID_NORMAL_AP01_USB */
	{
		.name = TO_STRING(BE_DAI_ID_NORMAL_AP01_USB),
		.id = BE_DAI_ID_NORMAL_AP01_USB,
		.playback = {
			.stream_name = "BE_DAI_NORMAL_AP01_USB_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.capture = {
			.stream_name = "BE_DAI_NORMAL_AP01_USB_C",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &normal_ops,
	},
	/* 11: BE_DAI_ID_NORMAL_AP23_USB */
	{
		.name = TO_STRING(BE_DAI_ID_NORMAL_AP23_USB),
		.id = BE_DAI_ID_NORMAL_AP23_USB,
		.playback = {
			.stream_name = "BE_DAI_NORMAL_AP23_USB_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.capture = {
			.stream_name = "BE_DAI_NORMAL_AP23_USB_C",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &normal_ops,
	},
	/* 12: BE_DAI_ID_CAPTURE_DSP_USB */
	{
		.name = TO_STRING(BE_DAI_ID_CAPTURE_DSP_USB),
		.id = BE_DAI_ID_CAPTURE_DSP_USB,
		.capture = {
			.stream_name = "BE_DAI_CAP_DSP_USB_C",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &capture_dsp_ops,
	},
	/* 13: BE_DAI_ID_FAST_P_USB */
	{
		.name = TO_STRING(BE_DAI_ID_FAST_P_USB),
		.id = BE_DAI_ID_FAST_P_USB,
		.playback = {
			.stream_name = "BE_DAI_FAST_USB_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &fast_ops,
	},
	/* 14: BE_DAI_ID_OFFLOAD_USB */
	{
		.name = TO_STRING(BE_DAI_ID_OFFLOAD_USB),
		.id = BE_DAI_ID_OFFLOAD_USB,
		.playback = {
			.stream_name = "BE_DAI_OFFLOAD_USB_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &offload_ops,
	},
	/* 15: BE_DAI_ID_VOICE_USB */
	{
		.name = TO_STRING(BE_DAI_ID_VOICE_USB),
		.id = BE_DAI_ID_VOICE_USB,
		.playback = {
			.stream_name = "BE_DAI_VOICE_USB_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.capture = {
			.stream_name = "BE_DAI_VOICE_USB_C",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &voice_ops,
	},
	/* 16: BE_DAI_ID_VOIP_USB */
	{
		.name = TO_STRING(BE_DAI_ID_VOIP_USB),
		.id = BE_DAI_ID_VOIP_USB,
		.playback = {
			.stream_name = "BE_DAI_VOIP_USB_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.capture = {
			.stream_name = "BE_DAI_VOIP_USB_C",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &voip_ops,
	},
	/* 17: BE_DAI_ID_FM_USB */
	{
		.name = TO_STRING(BE_DAI_ID_FM_USB),
		.id = BE_DAI_ID_FM_USB,
		.playback = {
			.stream_name = "BE_DAI_FM_USB_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &fm_ops,
	},
	/* 18: BE_DAI_ID_LOOP_USB */
	{
		.name = TO_STRING(BE_DAI_ID_LOOP_USB),
		.id = BE_DAI_ID_LOOP_USB,
		.playback = {
			.stream_name = "BE_DAI_LOOP_USB_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.capture = {
			.stream_name = "BE_DAI_LOOP_USB_C",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &loop_ops,
	},
	/* 19: BE_DAI_ID_FM_DSP_USB */
	{
		.name = TO_STRING(BE_DAI_ID_FM_DSP_USB),
		.id = BE_DAI_ID_FM_DSP_USB,
		.playback = {
			.stream_name = "BE_DAI_FM_DSP_USB_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &fm_dsp_ops,
	},
	/* 20: BE_DAI_ID_OFFLOAD_A2DP */
	{
		.name = TO_STRING(BE_DAI_ID_OFFLOAD_A2DP),
		.id = BE_DAI_ID_OFFLOAD_A2DP,
		.playback = {
			.stream_name = "BE_DAI_OFFLOAD_A2DP_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &offload_a2dp_ops,
	},
	/* 21: BE_DAI_ID_PCM_A2DP */
	{
		.name = TO_STRING(BE_DAI_ID_PCM_A2DP),
		.id = BE_DAI_ID_PCM_A2DP,
		.playback = {
			.stream_name = "BE_DAI_PCM_A2DP_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &pcm_a2dp_ops,
	},
	/* 22: BE_DAI_ID_VOICE_BT */
	{
		.name = TO_STRING(BE_DAI_ID_VOICE_BT),
		.id = BE_DAI_ID_VOICE_BT,
		.playback = {
			.stream_name = "BE_DAI_VOICE_BT_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.capture = {
			.stream_name = "BE_DAI_VOICE_BT_C",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &voice_ops,
	},
	/* 23: BE_DAI_ID_VOIP_BT */
	{
		.name = TO_STRING(BE_DAI_ID_VOIP_BT),
		.id = BE_DAI_ID_VOIP_BT,
		.playback = {
			.stream_name = "BE_DAI_VOIP_BT_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.capture = {
			.stream_name = "BE_DAI_VOIP_BT_C",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &voip_ops,
	},
	/* 24: BE_DAI_ID_LOOP_BT */
	{
		.name = TO_STRING(BE_DAI_ID_LOOP_BT),
		.id = BE_DAI_ID_LOOP_BT,
		.playback = {
			.stream_name = "BE_DAI_LOOP_BT_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.capture = {
			.stream_name = "BE_DAI_LOOP_BT_C",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &loop_ops,
	},
	/* 25: BE_DAI_ID_CAPTURE_BT */
	{
		.name = TO_STRING(BE_DAI_ID_CAPTURE_BT),
		.id = BE_DAI_ID_CAPTURE_BT,
		.capture = {
			.stream_name = "BE_DAI_CAP_BT_C",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &bt_capture_ops,
	},
	/* 26: BE_DAI_ID_VOICE_CAPTURE */
	{
		.name = TO_STRING(BE_DAI_ID_VOICE_CAPTURE),
		.id = BE_DAI_ID_VOICE_CAPTURE,
		.capture = {
			.stream_name = "BE_DAI_VOICE_CAP_C",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &voice_capture_ops,
	},
	/* 27: BE_DAI_ID_FM_CAPTURE */
	{
		.name = TO_STRING(BE_DAI_ID_FM_CAPTURE),
		.id = BE_DAI_ID_FM_CAPTURE,
		.capture = {
			.stream_name = "BE_DAI_FM_CAP_C",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &fm_capture_ops,
	},
	/* 28: BE_DAI_ID_CAPTURE_FM_DSP */
	{
		.name = TO_STRING(BE_DAI_ID_FM_CAPTURE_DSP),
		.id = BE_DAI_ID_FM_CAPTURE_DSP,
		.capture = {
			.stream_name = "BE_DAI_CAP_DSP_FM_C",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &capture_fm_dsp_ops,
	},
	/* 29: BE_DAI_ID_CAPTURE_DSP_BTSCO */
	{
		.name = TO_STRING(BE_DAI_ID_CAPTURE_DSP_BTSCO),
		.id = BE_DAI_ID_CAPTURE_DSP_BTSCO,
		.capture = {
			.stream_name = "BE_DAI_CAP_DSP_BTSCO_C",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &capture_btsco_dsp_ops,
	},
	/* 30: BE_DAI_ID_DUMP */
	{
		.name = TO_STRING(BE_DAI_ID_DUMP),
		.id = BE_DAI_ID_DUMP,
		.capture = {
			.stream_name = "BE_DAI_DUMP_C",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &vbc_dump_ops,
	},
	/*
	 * 31. DUMMY CPU DAI NOT BE(no stream, no be dai)
	 * only dais with stream that can play as BE DAI.
	 */
	{
		.name = TO_STRING(BE_DAI_ID_DUMMY_VBC_DAI_NOTBE),
		.id = BE_DAI_ID_DUMMY_VBC_DAI_NOTBE,
		.playback = {
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.capture = {
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
	},
	/* 32: BE_DAI_ID_FAST_P_BTSCO */
	{
		.name = TO_STRING(BE_DAI_ID_FAST_P_BTSCO),
		.id = BE_DAI_ID_FAST_P_BTSCO,
		.playback = {
			.stream_name = "BE_DAI_FAST_BTSCO_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &fast_ops,
	},
	/* 33: BE_DAI_ID_NORMAL_AP01_P_BTSCO */
	{
		.name = TO_STRING(BE_DAI_ID_NORMAL_AP01_P_BTSCO),
		.id = BE_DAI_ID_NORMAL_AP01_P_BTSCO,
		.playback = {
			.stream_name = "BE_DAI_NORMAL_AP01_BTSCO_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &normal_ops,
	},
	/* 34: BE_DAI_ID_NORMAL_AP01_P_HIFI */
	{
		.name = TO_STRING(BE_DAI_ID_NORMAL_AP01_P_HIFI),
		.id = BE_DAI_ID_NORMAL_AP01_P_HIFI,
		.playback = {
			.stream_name = "BE_DAI_ID_NORMAL_AP01_P_HIFI",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &normal_ops,
	},
	/* 35: BE_DAI_ID_NORMAL_AP23_HIFI */
	{
		.name = TO_STRING(BE_DAI_ID_NORMAL_AP23_HIFI),
		.id = BE_DAI_ID_NORMAL_AP23_HIFI,
		.playback = {
			.stream_name = "BE_DAI_ID_NORMAL_AP23_HIFI",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &normal_ap23_ops,
	},
	/* 36: BE_DAI_ID_FAST_P_HIFI */
	{
		.name = TO_STRING(BE_DAI_ID_FAST_P_HIFI),
		.id = BE_DAI_ID_FAST_P_HIFI,
		.playback = {
			.stream_name = "BE_DAI_ID_FAST_P_HIFI",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &fast_ops,
	},
	/* 37: BE_DAI_ID_OFFLOAD_HIFI */
	{
		.name = TO_STRING(BE_DAI_ID_OFFLOAD_HIFI),
		.id = BE_DAI_ID_OFFLOAD_HIFI,
		.playback = {
			.stream_name = "BE_DAI_ID_OFFLOAD_HIFI",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &offload_ops,
	},
	/* 38: BE_DAI_ID_VOICE_HIFI */
	{
		.name = TO_STRING(BE_DAI_ID_VOICE_HIFI),
		.id = BE_DAI_ID_VOICE_HIFI,
		.playback = {
			.stream_name = "BE_DAI_ID_VOICE_HIFI",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &voice_ops,
	},
	/* 39: BE_DAI_ID_VOIP_HIFI */
	{
		.name = TO_STRING(BE_DAI_ID_VOIP_HIFI),
		.id = BE_DAI_ID_VOIP_HIFI,
		.playback = {
			.stream_name = "BE_DAI_ID_VOIP_HIFI",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &voip_ops,
	},
	/* 40: BE_DAI_ID_FM_HIFI */
	{
		.name = TO_STRING(BE_DAI_ID_FM_HIFI),
		.id = BE_DAI_ID_FM_HIFI,
		.playback = {
			.stream_name = "BE_DAI_ID_FM_HIFI",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &fm_ops,
	},
	/* 41: BE_DAI_ID_LOOP_HIFI */
	{
		.name = TO_STRING(BE_DAI_ID_LOOP_HIFI),
		.id = BE_DAI_ID_LOOP_HIFI,
		.playback = {
			.stream_name = "BE_DAI_ID_LOOP_HIFI",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &loop_ops,
	},
	/* 42: BE_DAI_ID_FM_DSP_HIFI */
	{
		.name = TO_STRING(BE_DAI_ID_FM_DSP_HIFI),
		.id = BE_DAI_ID_FM_DSP_HIFI,
		.playback = {
			.stream_name = "BE_DAI_ID_FM_DSP_HIFI",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &fm_dsp_ops,
	},
	/* 43: BE_DAI_ID_HFP */
	{
		.name = TO_STRING(BE_DAI_ID_HFP),
		.id = BE_DAI_ID_HFP,
		.playback = {
			.stream_name = "BE_DAI_HFP_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.capture = {
			.stream_name = "BE_DAI_HFP_C",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &hfp_ops,
	},

	/* 44: BE_DAI_ID_RECOGNISE_CAPTURE */
	{
		.name = TO_STRING(BE_DAI_ID_RECOGNISE_CAPTURE),
		.id = BE_DAI_ID_RECOGNISE_CAPTURE,
		.capture = {
			.stream_name = "BE_DAI_CAP_RECOGNISE_CODEC_C",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &recognise_capture_ops,
	},

	/* 45: BE_DAI_ID_VOICE_PCM_P */
	{
		.name = TO_STRING(BE_DAI_ID_VOICE_PCM_P),
		.id = BE_DAI_ID_VOICE_PCM_P,
		.playback = {
			.stream_name = "BE_DAI_VOICE_PCM_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &voice_pcm_ops,
	},

	/* 46: BE_DAI_ID_NORMAL_AP01_P_SMTPA */
	{
		.name = TO_STRING(BE_DAI_ID_NORMAL_AP01_P_SMTPA),
		.id = BE_DAI_ID_NORMAL_AP01_P_SMTPA,
		.playback = {
			.stream_name = "BE_DAI_ID_NORMAL_AP01_P_SMTPA",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &normal_ops,
	},
	/* 47: BE_DAI_ID_NORMAL_AP23_SMTPA */
	{
		.name = TO_STRING(BE_DAI_ID_NORMAL_AP23_SMTPA),
		.id = BE_DAI_ID_NORMAL_AP23_SMTPA,
		.playback = {
			.stream_name = "BE_DAI_ID_NORMAL_AP23_SMTPA",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &normal_ap23_ops,
	},
	/* 48: BE_DAI_ID_FAST_P_SMTPA */
	{
		.name = TO_STRING(BE_DAI_ID_FAST_P_SMTPA),
		.id = BE_DAI_ID_FAST_P_SMTPA,
		.playback = {
			.stream_name = "BE_DAI_ID_FAST_P_SMTPA",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &fast_ops,
	},
	/* 49: BE_DAI_ID_OFFLOAD_SMTPA */
	{
		.name = TO_STRING(BE_DAI_ID_OFFLOAD_SMTPA),
		.id = BE_DAI_ID_OFFLOAD_SMTPA,
		.playback = {
			.stream_name = "BE_DAI_ID_OFFLOAD_SMTPA",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &offload_ops,
	},
	/* 50: BE_DAI_ID_VOICE_SMTPA */
	{
		.name = TO_STRING(BE_DAI_ID_VOICE_SMTPA),
		.id = BE_DAI_ID_VOICE_SMTPA,
		.playback = {
			.stream_name = "BE_DAI_ID_VOICE_SMTPA",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &voice_ops,
	},
	/* 51: BE_DAI_ID_VOIP_SMTPA */
	{
		.name = TO_STRING(BE_DAI_ID_VOIP_SMTPA),
		.id = BE_DAI_ID_VOIP_SMTPA,
		.playback = {
			.stream_name = "BE_DAI_ID_VOIP_SMTPA",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &voip_ops,
	},
	/* 52: BE_DAI_ID_FM_SMTPA */
	{
		.name = TO_STRING(BE_DAI_ID_FM_SMTPA),
		.id = BE_DAI_ID_FM_SMTPA,
		.playback = {
			.stream_name = "BE_DAI_ID_FM_SMTPA",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &fm_ops,
	},
	/* 53: BE_DAI_ID_LOOP_SMTPA */
	{
		.name = TO_STRING(BE_DAI_ID_LOOP_SMTPA),
		.id = BE_DAI_ID_LOOP_SMTPA,
		.playback = {
			.stream_name = "BE_DAI_ID_LOOP_SMTPA",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &loop_ops,
	},
	/* 54: BE_DAI_ID_FM_DSP_SMTPA */
	{
		.name = TO_STRING(BE_DAI_ID_FM_DSP_SMTPA),
		.id = BE_DAI_ID_FM_DSP_SMTPA,
		.playback = {
			.stream_name = "BE_DAI_ID_FM_DSP_SMTPA",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &fm_dsp_ops,
	},
	/* 55: BE_DAI_ID_HIFI_P */
	{
		.name = TO_STRING(BE_DAI_ID_HIFI_P),
		.id = BE_DAI_ID_HIFI_P,
		.playback = {
			.stream_name = "BE_DAI_HIFI_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &hifi_ops,
	},

	/* 56: BE_DAI_ID_HIFI_FAST_P */
	{
		.name = TO_STRING(BE_DAI_ID_HIFI_FAST_P),
		.id = BE_DAI_ID_HIFI_FAST_P,
		.playback = {
			.stream_name = "BE_DAI_HIFI_FAST_P",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &hifi_fast_ops,
	},

	/* 57: BE_DAI_ID_FAST_P_SMART_AMP */
	{
		.name = TO_STRING(BE_DAI_ID_FAST_P_SMART_AMP),
		.id = BE_DAI_ID_FAST_P_SMART_AMP,
		.playback = {
			.stream_name = "BE_DAI_ID_FAST_P_SMART_AMP",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &smtpa_fast_ops,
	},

	/* 58: BE_DAI_ID_FAST_P_CODEC_MM */
	{
		.name = TO_STRING(BE_DAI_ID_FAST_P_CODEC_MM),
		.id = BE_DAI_ID_FAST_P_CODEC_MM,
		.playback = {
			.stream_name = "BE_DAI_FAST_CODEC_P_MM",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_max = 192000,
			.formats = SPRD_VBC_DAI_PCM_FORMATS,
		},
		.ops = &smtpa_fast_ops,
	},
};

static struct aud_pm_vbc *aud_pm_vbc_get(void)
{
	return pm_vbc;
}

static void pm_vbc_init(void)
{
	struct aud_pm_vbc *pm_vbc;

	pm_vbc = aud_pm_vbc_get();
	if (pm_vbc == NULL)
		return;

	mutex_init(&pm_vbc->pm_mtx_cmd_prot);
	spin_lock_init(&pm_vbc->pm_spin_cmd_prot);
	mutex_init(&pm_vbc->lock_scene_flag);
	mutex_init(&pm_vbc->lock_mtx_suspend_resume);
}

int vbc_of_setup(struct platform_device *pdev)
{
	struct resource *res;
	struct device_node *np;
	struct regmap *agcp_ahb_gpr;
	struct pinctrl *pctrl;
	struct vbc_codec_priv *vbc_codec;
	int ret;
	u32 val;
	void *sprd_ap_vbc_virt_base;
	u32 sprd_ap_vbc_phy_base;

	if (!pdev) {
		pr_err("ERR: %s, pdev is NULL!\n", __func__);
		return -ENODEV;
	}
	np = pdev->dev.of_node;

	vbc_codec = platform_get_drvdata(pdev);
	if (!vbc_codec) {
		pr_err("%s vbc_codec is null failed\n", __func__);
		return -EINVAL;
	}

	/* Prepare for global registers accessing. */
	agcp_ahb_gpr = syscon_regmap_lookup_by_phandle(
		np, "sprd,syscon-agcp-ahb");
	if (IS_ERR(agcp_ahb_gpr)) {
		pr_err("ERR: [%s] Get the agcp ahb syscon failed!\n",
		       __func__);
		agcp_ahb_gpr = NULL;
		return -EPROBE_DEFER;
	}
	arch_audio_set_agcp_ahb_gpr(agcp_ahb_gpr);

	/* Prepare for vbc registers accessing. */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res != NULL) {
		sprd_ap_vbc_phy_base = (u32)res->start;
		sprd_ap_vbc_virt_base = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(sprd_ap_vbc_virt_base)) {
			pr_err("ERR: cannot create iomap address for AP-VBC!\n");
			return -EINVAL;
		}
	} else {
		pr_err("ERR:Must give me the AP-VBC reg address!\n");
		return -EINVAL;
	}
	set_ap_vbc_virt_base(sprd_ap_vbc_virt_base);
	set_ap_vbc_phy_base(sprd_ap_vbc_phy_base);
	ret = of_property_read_u32(np, "sprd,vbc-phy-offset", &val);
	if (ret) {
		pr_err("ERR: %s :no property of 'reg'\n", __func__);
		return -EINVAL;
	}
	set_vbc_dsp_ap_offset(val);

	/* PIN MUX */
	pctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(pctrl)) {
		pr_err("ERR: %s :get pinctrl failed\n", __func__);
		return -ENODEV;
	}
	sp_asoc_pr_dbg("get pinctrl device!\n");
	vbc_codec->pctrl = pctrl;

	return 0;
}

static int vbc_drv_probe(struct platform_device *pdev)
{
	int ret;
	struct vbc_codec_priv *vbc_codec = NULL;

	pr_info("%s: to setup vbc dt\n", __func__);
	/* 1. probe CODEC */
	ret = sprd_vbc_codec_probe(pdev);
	if (ret < 0)
		goto probe_err;
	vbc_codec = platform_get_drvdata(pdev);
	g_vbc_codec = vbc_codec;
	/*
	 * should first call sprd_vbc_codec_probe
	 * because we will call platform_get_drvdata(pdev)
	 */
	ret = vbc_of_setup(pdev);
	if (ret < 0) {
		pr_err("%s: failed to setup vbc dt, ret=%d\n", __func__, ret);
		return -ENODEV;
	}
	ret = aud_ipc_ch_open(AMSG_CH_DSP_GET_PARAM_FROM_SMSG_NOREPLY);
	if (ret) {
		pr_err("%s: audio sipc not ready, deferring\n", __func__);
		return -EPROBE_DEFER;
	}
	aud_ipc_ch_open(AMSG_CH_VBC_CTL);
	aud_ipc_ch_open(AMSG_CH_DSP_HIFI);

	/* 2. probe DAIS */
	ret = snd_soc_register_component(&pdev->dev, &sprd_vbc_codec, vbc_dais,
				     ARRAY_SIZE(vbc_dais));

	if (ret < 0) {
		pr_err("%s, Register VBC to DAIS Failed!\n", __func__);
		goto probe_err;
	}

	pm_vbc = devm_kzalloc(&pdev->dev, sizeof(*pm_vbc), GFP_KERNEL);
	if (!pm_vbc) {
		ret = -ENOMEM;
		goto probe_err;
	}
	pm_vbc_init();
	init_pcm_ops_lock();

	vbc_turning_ndp_init();
	return ret;
probe_err:
	pr_err("%s, error return %i\n", __func__, ret);

	return ret;
}

static void vbc_drv_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);

	sprd_vbc_codec_remove(pdev);
	aud_ipc_ch_close(AMSG_CH_DSP_GET_PARAM_FROM_SMSG_NOREPLY);
	aud_ipc_ch_close(AMSG_CH_VBC_CTL);

	}

#ifdef CONFIG_OF
static const struct of_device_id vbc_of_match[] = {
	{.compatible = "unisoc,sharkl5-vbc",},
	{.compatible = "unisoc,roc1-vbc",},
	{.compatible = "unisoc,qogirl6-vbc",},
	{.compatible = "unisoc,qogirn6pro-vbc",},
	{},
};

MODULE_DEVICE_TABLE(of, vbc_of_match);
#endif

static struct platform_driver vbc_driver = {
	.driver = {
		.name = "vbc-v4",
		.owner = THIS_MODULE,
		.of_match_table = vbc_of_match,
	},

	.probe = vbc_drv_probe,
	.remove = vbc_drv_remove,
};

static int __init sprd_vbc_driver_init(void)
{
	return platform_driver_register(&vbc_driver);
}

late_initcall(sprd_vbc_driver_init);

MODULE_DESCRIPTION("SPRD ASoC VBC CPU-DAI driver");
MODULE_AUTHOR("Jian chen <jian.chen@spreadtrum.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("cpu-dai:vbc-v4");
