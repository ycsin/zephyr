/* Kionix KX022 3-axis accelerometer driver
 *
 * Copyright (c) 2021 G-Technologies Sdn. Bhd.
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
	k_work_submit(&data->work);
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

	ret = data->hw_tf->read_reg(dev, KX022_REG_INS2, &status);
	if (ret < 0) {
		return;
	}

	if (status & KX022_MASK_INS2_WUFS) {
		kx022_handle_motion_int(dev);
	}

	if (status & KX022_MASK_INS2_TPS) {
		kx022_handle_tilt_int(dev);
	}

	ret = data->hw_tf->read_reg(dev, KX022_REG_INT_REL, &clr);
	if (ret < 0) {
		LOG_ERR("Failed clear int report flag");
	}
}

#ifdef CONFIG_KX022_TRIGGER_OWN_THREAD
static void kx022_thread(struct kx022_data *data)
{
	while (1) {
		k_sem_take(&data->trig_sem, K_FOREVER);
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


	if(!device_is_ready(cfg->gpio_int.port))
	{
		if(cfg->gpio_int.port){
			LOG_ERR("%s: device %s is not ready", dev->name,
						cfg->gpio_int.port->name);
			return -ENODEV;
		}
	}

	ret = gpio_pin_configure_dt(&cfg->gpio_int,GPIO_INPUT);
	if (ret <0) {
		LOG_ERR("Could not configure gpio");
		return -EINVAL;
	}

	gpio_init_callback(&data->gpio_cb,
			   kx022_gpio_callback,
			   BIT(cfg->gpio_int.pin));

	if (gpio_add_callback(cfg->gpio_int.port, &data->gpio_cb) < 0) {
		LOG_ERR("Could not set gpio callback.");
		return -EIO;
	}

	/* Enable kx022 physical int 1 */
	val = KX022_MASK_INC1_IEN1;
	val |= (cfg->int_pin_1_polarity << KX022_INC1_IEA1_SHIFT);
	val |= (cfg->int_pin_1_response << KX022_INC1_IEL1_SHIFT);

	ret = data->hw_tf->write_reg(dev, KX022_REG_INC1, val);
	if (ret  < 0) {
		LOG_ERR("Failed set physical int 1");
		return -EIO;
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
	ret = gpio_pin_interrupt_configure_dt(&cfg->gpio_int,
					       GPIO_INT_DISABLE);
	if (ret < 0) {
		LOG_ERR("Failed INT_DISABLE");
		return -EINVAL;
	}

	return 0;
}

int kx022_motion_setup(const struct device *dev, sensor_trigger_handler_t handler)
{
	struct kx022_data *data = dev->data;
	const struct kx022_config *cfg = dev->config;
	int ret;

	data->motion_handler = handler;

	if (kx022_mode(dev, KX022_STANDY_MODE) < 0) {
		return -EIO;
	}

	/* Enable motion detection */
	ret = data->hw_tf->update_reg(dev, KX022_REG_CNTL1, KX022_MASK_CNTL1_WUFE, KX022_CNTL1_WUFE);
	if (ret < 0) {
		LOG_ERR("Failed set motion detect");
		return -EIO;
	}

	ret = data->hw_tf->update_reg(dev, KX022_REG_CNTL3, KX022_MASK_CNTL3_OWUF, cfg->motion_odr);
	if (ret < 0) {
		LOG_ERR("Failed set motion odr");
		return -EIO;
	}

	ret = data->hw_tf->write_reg(dev, KX022_REG_INC2, KX022_DEFAULT_INC2);
	if (ret< 0) {
		LOG_ERR("Failed set motion axis");
		return -EIO;
	}

	ret = data->hw_tf->write_reg(dev, KX022_REG_WUFC, cfg->motion_detection_timer);
	if (ret < 0) {
		LOG_ERR("Failed set motion delay");
		return -EIO;
	}

	ret = data->hw_tf->write_reg(dev, KX022_REG_ATH, cfg->motion_threshold);
	if (ret < 0) {
		LOG_ERR("Failed set motion ath");
		return -EIO;
	}

	ret = data->hw_tf->update_reg(dev, KX022_REG_INC4, KX022_MASK_INC4_WUFI1,
				    KX022_INC4_WUFI1_SET) ;
	if (ret < 0) {
		LOG_ERR("Failed set motion int1");
		return -EIO;
	}

	kx022_mode(dev, KX022_OPERATING_MODE);

	return 0;
}

int kx022_tilt_setup(const struct device *dev, sensor_trigger_handler_t handler)
{
	struct kx022_data *data = dev->data;
	const struct kx022_config *cfg = dev->config;
	int ret;

	data->tilt_handler = handler;

	if (kx022_mode(dev, KX022_STANDY_MODE) < 0) {
		return -EIO;
	}

	ret = data->hw_tf->update_reg(dev, KX022_REG_CNTL1, KX022_MASK_CNTL1_TPE,
				    KX022_CNTL1_TPE_EN);
	if (ret < 0) {
		LOG_ERR("Failed set tilt");
		return -EIO;
	}

	ret = data->hw_tf->write_reg(dev, KX022_REG_CNTL2, KX022_CNTL_TILT_ALL_EN);
	if (ret < 0) {
		LOG_ERR("Failed set tilt axis");
		return -EIO;
	}

	ret =  data->hw_tf->update_reg(dev, KX022_REG_CNTL3, KX022_MASK_CNTL3_OTP,
				    (cfg->tilt_odr << KX022_CNTL3_OTP_SHIFT));
	if ( ret < 0) {
		LOG_ERR("Failed set tilt odr");
		return -EIO;
	}

	ret = data->hw_tf->write_reg(dev, KX022_REG_TILT_TIMER, cfg->tilt_timer);
	if (ret < 0) {
		LOG_ERR("Failed set tilt timer");
		return -EIO;
	}

	ret = data->hw_tf->write_reg(dev, KX022_REG_TILT_ANGLE_LL, cfg->tilt_angle_ll);
	if (ret  < 0) {
		LOG_ERR("Failed set tilt angle ll");
		return -EIO;
	}

	ret = data->hw_tf->write_reg(dev, KX022_REG_TILT_ANGLE_HL, cfg->tilt_angle_hl);
	if (ret < 0) {
		LOG_ERR("Failed set tilt angle hl");
		return -EIO;
	}

	ret = data->hw_tf->update_reg(dev, KX022_REG_INC4, KX022_MASK_INC6_TPI2,
				    KX022_INC4_TPI1_SET);
	if (ret < 0) {
		LOG_ERR("Failed set tilt int1");
		return -EIO;
	}

	kx022_mode(dev, KX022_OPERATING_MODE);

	return 0;
}

int kx022_trigger_set(const struct device *dev, const struct sensor_trigger *trig,
		      sensor_trigger_handler_t handler)
{
	int ret;
	struct kx022_data *data = dev->data;
	const struct kx022_config *cfg = dev->config;
	uint8_t buf[6];

	switch ((enum sensor_trigger_type_kx022)trig->type) {
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

	__ASSERT_NO_MSG(trig->type == SENSOR_TRIG_DELTA);

	// if (gpio_pin_interrupt_configure(data->gpio, cfg->irq_pin, GPIO_INT_DISABLE)) {
	if (gpio_pin_interrupt_configure_dt(&cfg->gpio_int, GPIO_INT_DISABLE)) {
		LOG_ERR("Failed INT_DISABLE");
		return -EINVAL;
	}

	if (handler == NULL) {
		LOG_WRN("kx022: no handler");
	}

	/* re-trigger lost interrupt */
	ret = data->hw_tf->read_data(dev, KX022_REG_XOUT_L, buf, sizeof(buf));

	if (ret < 0) {
		LOG_ERR("status reading error");
		return -EIO;
	}

	if (gpio_pin_interrupt_configure_dt(&cfg->gpio_int, GPIO_INT_EDGE_TO_ACTIVE)) {
		LOG_ERR("Failed INT_EDGE_TO_ACTIVE");
		return -EINVAL;
	}

	return 0;
}

int kx022_restore_default_motion_setup(const struct device *dev)
{
	struct kx022_data *data = dev->data;
	int ret;

	if (kx022_mode(dev, KX022_STANDY_MODE) < 0) {
		return -EIO;
	}

	/* reset motion detect function*/
	ret = data->hw_tf->update_reg(dev, KX022_REG_CNTL1, KX022_MASK_CNTL1_WUFE,
				    KX022_CNTL1_WUFE_RESET);

	if  (ret < 0) {
		LOG_ERR("Failed to disable motion detect");
		return -EIO;
	}

	/* reset motion detect int 1 report */
	ret = data->hw_tf->update_reg(dev, KX022_REG_INC4, KX022_MASK_INC4_WUFI1,
				    KX022_INC4_WUFI1_RESET);

	if (ret < 0) {
		LOG_DBG("Failed to set KX022 int1 report");
		return -EIO;
	}

	kx022_mode(dev, KX022_OPERATING_MODE);

	return 0;
}

int kx022_restore_default_tilt_setup(const struct device *dev)
{
	struct kx022_data *data = dev->data;
	int ret;
	if (kx022_mode(dev, KX022_STANDY_MODE) < 0) {
		return -EIO;
	}

	/* reset the tilt function */
	ret = data->hw_tf->update_reg(dev, KX022_REG_CNTL1, KX022_MASK_CNTL1_TPE,
				    KX022_CNTL1_TPE_RESET);

	if (data->hw_tf->update_reg(dev, KX022_REG_CNTL1, KX022_MASK_CNTL1_TPE,
				    KX022_CNTL1_TPE_RESET) < 0) {
		LOG_ERR("Failed to disable tilt");
		return -EIO;
	}

	/* reset tilt int 1 report */
	ret = data->hw_tf->update_reg(dev, KX022_REG_INC4, KX022_MASK_INC4_TPI1,
				    KX022_INC4_TPI1_RESET);
	if (ret < 0) {
		LOG_DBG("Failed to set KX022 tilt int1 report");
		return -EIO;
	}

	kx022_mode(dev, KX022_OPERATING_MODE);

	return 0;
}

int kx022_restore_default_trigger_setup(const struct device *dev, const struct sensor_trigger *trig)
{
	int ret;

	switch ((enum sensor_trigger_type_kx022)trig->type) {
	case SENSOR_TRIG_KX022_TILT:
		ret = kx022_restore_default_tilt_setup(dev);
		break;

	case SENSOR_TRIG_KX022_MOTION:
		ret = kx022_restore_default_motion_setup(dev);
		break;

	default:
		return -ENOTSUP;
	}

	if (ret < 0) {
		return ret;
	}

	return 0;
}
