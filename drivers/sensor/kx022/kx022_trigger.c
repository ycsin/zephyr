/* Kionix KX022 3-axis accelerometer driver
 *
 * Copyright (c) 2021-2022 G-Technologies Sdn. Bhd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kx022.h"

LOG_MODULE_DECLARE(KX022, CONFIG_SENSOR_LOG_LEVEL);

static void kx022_gpio_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	struct kx022_data *data = CONTAINER_OF(cb, struct kx022_data, gpio_cb);

#if defined(CONFIG_KX022_TRIGGER_OWN_THREAD)
	k_sem_give(&data->trig_sem);
#elif defined(CONFIG_KX022_TRIGGER_GLOBAL_THREAD)
	(void)k_work_submit(&data->work);
#endif
}

static void kx022_handle_motion_int(const struct device *dev)
{
	struct kx022_data *data = dev->data;

	if (data->motion_handler != NULL) {
		data->motion_handler(dev, &data->motion_trigger);
	}
}

static void kx022_handle_tilt_int(const struct device *dev)
{
	struct kx022_data *data = dev->data;

	if (data->tilt_handler != NULL) {
		data->tilt_handler(dev, &data->tilt_trigger);
	}
}

static void kx022_handle_int(const struct device *dev)
{
	struct kx022_data *data = dev->data;
	uint8_t status, clr;
	int ret;

	if (data->hw_tf->read_reg(dev, KX022_REG_INS2, &status)) {
		return;
	}

	if (status & KX022_MASK_INS2_WUFS) {
		kx022_handle_motion_int(dev);
	}

	if (status & KX022_MASK_INS2_TPS) {
		kx022_handle_tilt_int(dev);
	}

	ret = data->hw_tf->read_reg(dev, KX022_REG_INT_REL, &clr);
	if (ret) {
		LOG_DBG("%s: Failed clear int report flag: %d", dev->name, ret);
	}
}

#ifdef CONFIG_KX022_TRIGGER_OWN_THREAD
static void kx022_thread(struct kx022_data *data)
{
	while (1) {
		(void)k_sem_take(&data->trig_sem, K_FOREVER);
		kx022_handle_int(data->dev);
	}
}
#endif

#ifdef CONFIG_KX022_TRIGGER_GLOBAL_THREAD
static void kx022_work_cb(struct k_work *work)
{
	struct kx022_data *data = CONTAINER_OF(work, struct kx022_data, work);

	kx022_handle_int(data->dev);
}
#endif

/**************************************************************
 * FUNCTION : kx022_trigger_init
 * use to initialize trigger function
 *
 * ************************************************************/
int kx022_trigger_init(const struct device *dev)
{
	uint8_t val;
	struct kx022_data *data = dev->data;
	const struct kx022_config *cfg = dev->config;
	int ret;

	/* setup data ready gpio interrupt */

	if (!device_is_ready(cfg->gpio_int.port)) {
		if (cfg->gpio_int.port) {
			LOG_DBG("%s: device %s is not ready", dev->name, cfg->gpio_int.port->name);
			return -ENODEV;
		}
	}

	ret = gpio_pin_configure_dt(&cfg->gpio_int, GPIO_INPUT);
	if (ret) {
		LOG_DBG("%s: Failed to configure gpio %s: %d", dev->name, "pin", ret);
		return ret;
	}

	gpio_init_callback(&data->gpio_cb, kx022_gpio_callback, BIT(cfg->gpio_int.pin));

	ret = gpio_add_callback(cfg->gpio_int.port, &data->gpio_cb);
	if (ret) {
		LOG_DBG("%s: Failed to configure gpio %s: %d", dev->name, "callback", ret);
		return ret;
	}

	/* Enable kx022 physical int 1 */
	val = KX022_MASK_INC1_IEN1;
	val |= (cfg->int_pin_1_polarity << KX022_INC1_IEA1_SHIFT);
	val |= (cfg->int_pin_1_response << KX022_INC1_IEL1_SHIFT);

	ret = data->hw_tf->write_reg(dev, KX022_REG_INC1, val);
	if (ret) {
		LOG_DBG("%s: Failed to write %s: %d", dev->name, "physical int 1", ret);
		return ret;
	}

	data->dev = dev;
#if defined(CONFIG_KX022_TRIGGER_OWN_THREAD)
	k_sem_init(&data->trig_sem, 0, UINT_MAX);

	k_thread_create(&data->thread, data->thread_stack, CONFIG_KX022_THREAD_STACK_SIZE,
			(k_thread_entry_t)kx022_thread, data, NULL, NULL,
			K_PRIO_COOP(CONFIG_KX022_THREAD_PRIORITY), 0, K_NO_WAIT);
#elif defined(CONFIG_KX022_TRIGGER_GLOBAL_THREAD)
	data->work.handler = kx022_work_cb;
#endif
	ret = gpio_pin_interrupt_configure_dt(&cfg->gpio_int, GPIO_INT_DISABLE);
	if (ret) {
		LOG_DBG("%s: Failed to configure gpio %s: %d", dev->name, "interrupt-DISABLE", ret);
	}

	return ret;
}

int kx022_motion_setup(const struct device *dev, sensor_trigger_handler_t handler)
{
	struct kx022_data *data = dev->data;
	const struct kx022_config *cfg = dev->config;
	int ret;

	if (handler == NULL) {
		LOG_WRN("%s: no handler", dev->name);
	}

	data->motion_handler = handler;

	ret = kx022_standby_mode(dev);
	if (ret) {
		return ret;
	}

	/* Enable motion detection */
	ret = data->hw_tf->update_reg(dev, KX022_REG_CNTL1, KX022_MASK_CNTL1_WUFE,
				      KX022_CNTL1_WUFE);
	if (ret) {
		LOG_DBG("%s: Failed to %s: %d motion detect", dev->name, "physical int 1", ret);
		goto exit;
	}

	ret = data->hw_tf->update_reg(dev, KX022_REG_CNTL3, KX022_MASK_CNTL3_OWUF, cfg->motion_odr);
	if (ret) {
		LOG_DBG("%s: Failed to update %s: %d", dev->name, "motion odr", ret);
		goto exit;
	}

	ret = data->hw_tf->write_reg(dev, KX022_REG_INC2, KX022_DEFAULT_INC2);
	if (ret) {
		LOG_DBG("%s: Failed to write %s: %d", dev->name, "motion axis", ret);
		goto exit;
	}

	ret = data->hw_tf->write_reg(dev, KX022_REG_WUFC, cfg->motion_detection_timer);
	if (ret) {
		LOG_DBG("%s: Failed to write %s: %d", dev->name, "motion delay", ret);
		goto exit;
	}

	ret = data->hw_tf->write_reg(dev, KX022_REG_ATH, cfg->motion_threshold);
	if (ret) {
		LOG_DBG("%s: Failed to write %s: %d", dev->name, "motion ath", ret);
		goto exit;
	}

	ret = data->hw_tf->update_reg(dev, KX022_REG_INC4, KX022_MASK_INC4_WUFI1,
				      KX022_INC4_WUFI1_SET);
	if (ret) {
		LOG_DBG("%s: Failed to update %s: %d", dev->name, "motion int1", ret);
	}

exit:
	(void)kx022_operating_mode(dev);
	return ret;
}

static int kx022_tilt_setup(const struct device *dev, sensor_trigger_handler_t handler)
{
	struct kx022_data *data = dev->data;
	const struct kx022_config *cfg = dev->config;
	int ret;

	if (handler == NULL) {
		LOG_WRN("%s: no handler", dev->name);
	}

	data->tilt_handler = handler;

	if (kx022_standby_mode(dev) < 0) {
		return -EIO;
	}

	ret = data->hw_tf->update_reg(dev, KX022_REG_CNTL1, KX022_MASK_CNTL1_TPE,
				      KX022_CNTL1_TPE_EN);
	if (ret < 0) {
		LOG_DBG("%s: Failed to update %s: %d", dev->name, "tilt", ret);
		goto exit;
	}

	ret = data->hw_tf->write_reg(dev, KX022_REG_CNTL2, KX022_CNTL_TILT_ALL_EN);
	if (ret < 0) {
		LOG_DBG("%s: Failed to write %s: %d", dev->name, "tilt axis", ret);
		goto exit;
	}

	ret = data->hw_tf->update_reg(dev, KX022_REG_CNTL3, KX022_MASK_CNTL3_OTP,
				      (cfg->tilt_odr << KX022_CNTL3_OTP_SHIFT));
	if (ret < 0) {
		LOG_DBG("%s: Failed to update %s: %d", dev->name, "tilt odr", ret);
		goto exit;
	}

	ret = data->hw_tf->write_reg(dev, KX022_REG_TILT_TIMER, cfg->tilt_timer);
	if (ret < 0) {
		LOG_DBG("%s: Failed to write %s: %d", dev->name, "tilt timer", ret);
		goto exit;
	}

	ret = data->hw_tf->write_reg(dev, KX022_REG_TILT_ANGLE_LL, cfg->tilt_angle_ll);
	if (ret < 0) {
		LOG_DBG("%s: Failed to write %s: %d", dev->name, "tilt angle ll", ret);
		goto exit;
	}

	ret = data->hw_tf->write_reg(dev, KX022_REG_TILT_ANGLE_HL, cfg->tilt_angle_hl);
	if (ret < 0) {
		LOG_DBG("%s: Failed to write %s: %d", dev->name, "tilt angle hl", ret);
		goto exit;
	}

	ret = data->hw_tf->update_reg(dev, KX022_REG_INC4, KX022_MASK_INC6_TPI2,
				      KX022_INC4_TPI1_SET);
	if (ret < 0) {
		LOG_DBG("%s: Failed to update %s: %d", dev->name, "tilt int1", ret);
	}

exit:
	(void)kx022_operating_mode(dev);
	return ret;
}

int kx022_trigger_set(const struct device *dev, const struct sensor_trigger *trig,
		      sensor_trigger_handler_t handler)
{
	int ret;
	struct kx022_data *data = dev->data;
	const struct kx022_config *cfg = dev->config;
	uint8_t buf[6];

	if (handler == NULL) {
		LOG_WRN("%s: no handler", dev->name);
	}

	switch ((int)trig->type) {
	case SENSOR_TRIG_KX022_MOTION:
		ret = kx022_motion_setup(dev, handler);
		break;

	case SENSOR_TRIG_KX022_TILT:
		ret = kx022_tilt_setup(dev, handler);
		break;

	default:
		return -ENOTSUP;
	}

	if (ret < 0) {
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&cfg->gpio_int, GPIO_INT_DISABLE);
	if (ret) {
		LOG_DBG("%s: Failed to configure gpio %s: %d", dev->name, "interrupt-DISABLE", ret);
		return -EINVAL;
	}

	/* re-trigger lost interrupt */
	ret = data->hw_tf->read_data(dev, KX022_REG_XOUT_L, buf, sizeof(buf));
	if (ret < 0) {
		LOG_DBG("%s: Failed to read %s: %d", dev->name, "status", ret);
		return -EIO;
	}

	ret = gpio_pin_interrupt_configure_dt(&cfg->gpio_int, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret) {
		LOG_DBG("%s: Failed to configure gpio %s: %d", dev->name,
			"interrupt-EDGE_TO_ACTIVE", ret);
		return -EINVAL;
	}

	return 0;
}

static int kx022_restore_default_motion_setup(const struct device *dev)
{
	struct kx022_data *data = dev->data;
	int ret;

	if (kx022_standby_mode(dev) < 0) {
		return -EIO;
	}

	/* reset motion detect function*/
	ret = data->hw_tf->update_reg(dev, KX022_REG_CNTL1, KX022_MASK_CNTL1_WUFE,
				      KX022_CNTL1_WUFE_RESET);
	if (ret < 0) {
		LOG_DBG("%s: Failed to disable motion detect", dev->name);
		goto exit;
	}

	/* reset motion detect int 1 report */
	ret = data->hw_tf->update_reg(dev, KX022_REG_INC4, KX022_MASK_INC4_WUFI1,
				      KX022_INC4_WUFI1_RESET);
	if (ret < 0) {
		LOG_DBG("%s: Failed to set KX022 int1 report", dev->name);
		goto exit;
	}

exit:
	(void)kx022_operating_mode(dev);
	return ret;
}

static int kx022_restore_default_tilt_setup(const struct device *dev)
{
	struct kx022_data *data = dev->data;
	int ret;

	if (kx022_standby_mode(dev) < 0) {
		return -EIO;
	}

	/* reset the tilt function */
	ret = data->hw_tf->update_reg(dev, KX022_REG_CNTL1, KX022_MASK_CNTL1_TPE,
				      KX022_CNTL1_TPE_RESET);
	if (ret < 0) {
		LOG_DBG("%s: Failed to %s", dev->name, "disable tilt");
		goto exit;
	}

	/* reset tilt int 1 report */
	ret = data->hw_tf->update_reg(dev, KX022_REG_INC4, KX022_MASK_INC4_TPI1,
				      KX022_INC4_TPI1_RESET);
	if (ret < 0) {
		LOG_DBG("%s: Failed to %s", dev->name, "set tilt int1 report");
		goto exit;
	}

exit:
	(void)kx022_operating_mode(dev);
	return ret;
}

int kx022_restore_default_trigger_setup(const struct device *dev, const struct sensor_trigger *trig)
{
	switch ((int)trig->type) {
	case SENSOR_TRIG_KX022_TILT:
		return kx022_restore_default_tilt_setup(dev);
	case SENSOR_TRIG_KX022_MOTION:
		return kx022_restore_default_motion_setup(dev);
	default:
		return -ENOTSUP;
	}
}
