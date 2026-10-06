// SPDX-License-Identifier: GPL-2.0-only
#include <linux/device.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/workqueue.h>

static DEFINE_MUTEX(scan_lock);
static DEFINE_MUTEX(scan_config_lock);
static struct led_classdev *scan_leds[4];
static const char * const scan_nodes[] = {
	"led-left", "led-left-stick", "led-right-stick", "led-right",
};
static const char * const scan_names[] = {
	"blue:backlight-1", "blue:backlight-3", "blue:backlight-4", "blue:backlight-2",
};
static const unsigned int scan_order[] = { 0, 1, 2, 3, 2, 1 };
static unsigned long long scan_step;
static bool scan_active;
static struct delayed_work scan_work;

static int scan_slot(struct led_classdev *led)
{
	const char *node = fwnode_get_name(dev_fwnode(led->dev));
	unsigned int i;

	if (led->max_brightness != 1 || led->color != LED_COLOR_ID_BLUE)
		return -EINVAL;
	for (i = 0; i < ARRAY_SIZE(scan_leds); i++) {
		if (node && !strcmp(node, scan_nodes[i]))
			return i;
		if (!node && !strcmp(led->name, scan_names[i]))
			return i;
	}
	return -EINVAL;
}

static bool scan_complete(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(scan_leds); i++)
		if (!scan_leds[i])
			return false;
	return true;
}

static int scan_brightness(struct led_classdev *led, enum led_brightness value)
{
	if (led->brightness_set) {
		led_set_brightness(led, value);
		return 0;
	}
	return led_set_brightness_sync(led, value);
}

static int scan_off(void)
{
	unsigned int i;
	int err = 0;

	for (i = 0; i < ARRAY_SIZE(scan_leds); i++)
		if (scan_leds[i]) {
			int result = scan_brightness(scan_leds[i], LED_OFF);

			if (result && !err)
				err = result;
		}
	return err;
}

static void scan_tick(struct work_struct *work)
{
	unsigned int selected;
	int err;

	mutex_lock(&scan_lock);
	if (!scan_active || !scan_complete())
		goto unlock;
	selected = scan_order[scan_step % ARRAY_SIZE(scan_order)];
	err = scan_off();
	if (!err)
		err = scan_brightness(scan_leds[selected], LED_ON);
	if (err) {
		scan_active = false;
		goto unlock;
	}
	scan_step++;
	queue_delayed_work(system_freezable_wq, &scan_work, msecs_to_jiffies(240));
unlock:
	mutex_unlock(&scan_lock);
}

static int scan_activate(struct led_classdev *led)
{
	int slot = scan_slot(led);
	int err = 0;

	if (slot < 0)
		return slot;
	mutex_lock(&scan_config_lock);
	mutex_lock(&scan_lock);
	if (scan_leds[slot] && scan_leds[slot] != led) {
		err = -EBUSY;
		goto unlock;
	}
	scan_leds[slot] = led;
	err = scan_brightness(led, LED_OFF);
	if (err) {
		scan_leds[slot] = NULL;
		goto unlock;
	}
	if (scan_complete()) {
		scan_step = 0;
		scan_active = true;
		queue_delayed_work(system_freezable_wq, &scan_work, 0);
	}
unlock:
	mutex_unlock(&scan_lock);
	mutex_unlock(&scan_config_lock);
	return err;
}

static void scan_deactivate(struct led_classdev *led)
{
	int slot = scan_slot(led);

	mutex_lock(&scan_config_lock);
	mutex_lock(&scan_lock);
	scan_active = false;
	scan_off();
	if (slot >= 0 && scan_leds[slot] == led)
		scan_leds[slot] = NULL;
	mutex_unlock(&scan_lock);
	cancel_delayed_work_sync(&scan_work);
	mutex_unlock(&scan_config_lock);
}

static ssize_t scan_running_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	bool active;

	mutex_lock(&scan_lock);
	active = scan_active && scan_complete();
	mutex_unlock(&scan_lock);
	return sysfs_emit(buf, "%u\n", active);
}
static DEVICE_ATTR_RO(scan_running);

static ssize_t scan_position_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	unsigned int position;

	mutex_lock(&scan_lock);
	position = scan_step ? scan_order[(scan_step - 1) % ARRAY_SIZE(scan_order)] : 0;
	mutex_unlock(&scan_lock);
	return sysfs_emit(buf, "%u\n", position);
}
static DEVICE_ATTR_RO(scan_position);

static ssize_t scan_count_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	unsigned long long count;

	mutex_lock(&scan_lock);
	count = scan_step;
	mutex_unlock(&scan_lock);
	return sysfs_emit(buf, "%llu\n", count);
}
static DEVICE_ATTR_RO(scan_count);

static struct attribute *odin_scan_attrs[] = {
	&dev_attr_scan_running.attr,
	&dev_attr_scan_position.attr,
	&dev_attr_scan_count.attr,
	NULL,
};
ATTRIBUTE_GROUPS(odin_scan);

static struct led_trigger odin_scan_trigger = {
	.name = "odin-scan",
	.activate = scan_activate,
	.deactivate = scan_deactivate,
	.groups = odin_scan_groups,
};

static int __init odin_scan_init(void)
{
	if (!of_machine_is_compatible("ayn,odin") &&
	    !of_machine_is_compatible("ayn,odin-m2"))
		return -ENODEV;
	INIT_DELAYED_WORK(&scan_work, scan_tick);
	return led_trigger_register(&odin_scan_trigger);
}

static void __exit odin_scan_exit(void)
{
	led_trigger_unregister(&odin_scan_trigger);
	cancel_delayed_work_sync(&scan_work);
}

module_init(odin_scan_init);
module_exit(odin_scan_exit);
MODULE_ALIAS("ledtrig:odin-scan");
MODULE_DESCRIPTION("AYN Odin synchronized left and right LED scan");
MODULE_LICENSE("GPL");
