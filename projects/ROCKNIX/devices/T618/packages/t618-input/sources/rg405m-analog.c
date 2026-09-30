// SPDX-License-Identifier: GPL-2.0-or-later
/* See SOURCE.md: RG405M vendor arithmetic and mainline adc-joystick interfaces. */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/iio/consumer.h>
#include <linux/iio/iio.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#define RG405M_AXES 4
#define RG405M_MAX 1800
#define RG405M_DEADZONE 216

struct rg405m_analog {
	struct input_dev *input;
	struct iio_channel *adc;
	struct gpio_desc *select_a;
	struct gpio_desc *select_b;
	struct gpio_desc *enable;
	struct gpio_descs *power;
	struct mutex lock;
	int center[RG405M_AXES];
	bool invert[RG405M_AXES];
	bool suspended;
};

static const unsigned int rg405m_axes[RG405M_AXES] = {
	ABS_RY, ABS_RX, ABS_Y, ABS_X,
};

static void rg405m_set_power(struct rg405m_analog *joy, bool enabled)
{
	unsigned int i;

	if (!enabled)
		gpiod_set_value_cansleep(joy->enable, 0);
	for (i = 0; i < joy->power->ndescs; i++)
		gpiod_set_value_cansleep(joy->power->desc[i], enabled);
	if (enabled) {
		usleep_range(1000, 2000);
		gpiod_set_value_cansleep(joy->enable, 1);
	}
}

static void rg405m_power_off(void *data)
{
	rg405m_set_power(data, false);
}

static int rg405m_read_axes(struct rg405m_analog *joy, int values[RG405M_AXES])
{
	int i, millivolt, ret;

	for (i = 0; i < RG405M_AXES; i++) {
		gpiod_set_value_cansleep(joy->select_a, i & 1);
		gpiod_set_value_cansleep(joy->select_b, (i >> 1) & 1);
		usleep_range(10, 20);
		ret = iio_read_channel_processed(joy->adc, &millivolt);
		if (ret < 0)
			return ret;
		millivolt = clamp(millivolt, 0, 1850);
		values[i] = RG405M_MAX - (millivolt * 1800 / 1850) * 2;
	}
	return 0;
}

static int rg405m_tune(int value)
{
	int magnitude = abs(value);

	if (magnitude <= RG405M_DEADZONE)
		return 0;
	magnitude = RG405M_MAX * (magnitude - RG405M_DEADZONE) /
		    (RG405M_MAX - RG405M_DEADZONE);
	magnitude = min(magnitude * 2, RG405M_MAX);
	return value < 0 ? -magnitude : magnitude;
}

static void rg405m_poll(struct input_dev *input)
{
	struct rg405m_analog *joy = input_get_drvdata(input);
	int values[RG405M_AXES];
	int i;

	mutex_lock(&joy->lock);
	if (!joy->suspended && !rg405m_read_axes(joy, values)) {
		for (i = 0; i < RG405M_AXES; i++)
			input_report_abs(input, rg405m_axes[i],
				 (joy->invert[i] ? -1 : 1) *
				 rg405m_tune(values[i] - joy->center[i]));
		input_sync(input);
	}
	mutex_unlock(&joy->lock);
}

static int rg405m_open(struct input_dev *input)
{
	struct rg405m_analog *joy = input_get_drvdata(input);
	int ret;

	mutex_lock(&joy->lock);
	ret = rg405m_read_axes(joy, joy->center);
	mutex_unlock(&joy->lock);
	return ret;
}

static int rg405m_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rg405m_analog *joy;
	struct input_dev *input;
	int i, ret;

	joy = devm_kzalloc(dev, sizeof(*joy), GFP_KERNEL);
	if (!joy)
		return -ENOMEM;
	mutex_init(&joy->lock);
	joy->adc = devm_iio_channel_get(dev, "joypad_adc");
	if (IS_ERR(joy->adc))
		return dev_err_probe(dev, PTR_ERR(joy->adc), "ADC unavailable\n");
	ret = devm_regulator_get_enable(dev, "vdd");
	if (ret)
		return dev_err_probe(dev, ret, "Input supply unavailable\n");
	joy->select_a = devm_gpiod_get(dev, "amux-a", GPIOD_OUT_LOW);
	if (IS_ERR(joy->select_a))
		return PTR_ERR(joy->select_a);
	joy->select_b = devm_gpiod_get(dev, "amux-b", GPIOD_OUT_LOW);
	if (IS_ERR(joy->select_b))
		return PTR_ERR(joy->select_b);
	joy->enable = devm_gpiod_get(dev, "amux-en", GPIOD_OUT_LOW);
	if (IS_ERR(joy->enable))
		return PTR_ERR(joy->enable);
	joy->power = devm_gpiod_get_array(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(joy->power))
		return PTR_ERR(joy->power);
	if (joy->power->ndescs != 4)
		return dev_err_probe(dev, -EINVAL, "Four input power GPIOs required\n");
	ret = devm_add_action_or_reset(dev, rg405m_power_off, joy);
	if (ret)
		return ret;
	rg405m_set_power(joy, true);
	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;
	joy->input = input;
	input->name = "RG405M Analog";
	input->phys = "rg405m/analog0";
	input->id.bustype = BUS_HOST;
	input->id.product = 0x1101;
	input->id.version = 0x0100;
	input->open = rg405m_open;
	input_set_drvdata(input, joy);
	platform_set_drvdata(pdev, joy);
	{
		static const char * const inv[RG405M_AXES] = {
			"invert-absry", "invert-absrx", "invert-absy", "invert-absx",
		};

		for (i = 0; i < RG405M_AXES; i++)
			joy->invert[i] = device_property_read_bool(dev, inv[i]);
	}
	for (i = 0; i < RG405M_AXES; i++)
		input_set_abs_params(input, rg405m_axes[i], -RG405M_MAX,
				     RG405M_MAX, 64, 32);
	ret = input_setup_polling(input, rg405m_poll);
	if (ret)
		return ret;
	input_set_poll_interval(input, 5);
	return input_register_device(input);
}

static int rg405m_suspend(struct device *dev)
{
	struct rg405m_analog *joy = dev_get_drvdata(dev);

	mutex_lock(&joy->lock);
	joy->suspended = true;
	rg405m_set_power(joy, false);
	mutex_unlock(&joy->lock);
	return 0;
}

static int rg405m_resume(struct device *dev)
{
	struct rg405m_analog *joy = dev_get_drvdata(dev);

	mutex_lock(&joy->lock);
	rg405m_set_power(joy, true);
	joy->suspended = false;
	mutex_unlock(&joy->lock);
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(rg405m_pm_ops, rg405m_suspend, rg405m_resume);
static const struct of_device_id rg405m_match[] = {
	{ .compatible = "anbernic,rg405m-analog" },
	{ }
};
MODULE_DEVICE_TABLE(of, rg405m_match);

static struct platform_driver rg405m_driver = {
	.probe = rg405m_probe,
	.driver = {
		.name = "rg405m-analog",
		.of_match_table = rg405m_match,
		.pm = pm_sleep_ptr(&rg405m_pm_ops),
	},
};
module_platform_driver(rg405m_driver);
MODULE_DESCRIPTION("RG405M SC2730 analog sticks");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_CONSUMER");
