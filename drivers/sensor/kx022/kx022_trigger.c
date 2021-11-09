/* Kionix KX022 3-axis accelerometer driver
 *
 * Copyright (c) 2021 G-Technologies Sdn. Bhd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <device.h>
#include <drivers/i2c.h>
#include <sys/__assert.h>
#include <sys/util.h>
#include <kernel.h>
#include <drivers/sensor.h>
#include <logging/log.h>
#include "kx022.h"

#if KX022_DEFAULT_INT_PIN == KX022_INT2
#error INT2 not supported
#endif

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

static void kx022_handle_drdy_int(const struct device *dev)
{
	struct kx022_data *data = dev->data;

	if (data->drdy_handler != NULL) {
		data->drdy_handler(dev, &data->drdy_trigger);
	}
}
static void kx022_handle_slope_int(const struct device *dev)
{
	struct kx022_data *data = dev->data;

	if (data->slope_handler != NULL) {
		data->slope_handler(dev, &data->slope_trigger);
	}
}

static void kx022_handle_int(const struct device *dev)
{
	struct kx022_data *data = dev->data;
	uint8_t status, clr;

	if (data->hw_tf->read_reg(data, KX022_REG_INS2, &status) < 0) {
		return;
	}

	if (status & KX022_MASK_INS2_WUFS) {
		kx022_handle_motion_int(dev);
	}

	if (status & KX022_MASK_INS2_DRDY) {
		kx022_handle_drdy_int(dev);
		//k_msleep(50);
	}

	if (status & KX022_MASK_INS2_TPS) {
		kx022_handle_slope_int(dev);
	}

	if(data->hw_tf->read_reg(data, KX022_REG_INT_REL, &clr) < 0) {
		LOG_ERR("KX022 : Failed clear int report flag");
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

	/* setup data ready gpio interrupt */
	data->gpio = device_get_binding(cfg->irq_port);
	if (data->gpio == NULL) {
		LOG_ERR("KX022 : Cannot get pointer to %s device.", cfg->irq_port);
		return -EINVAL;
	}

	if(gpio_pin_configure(data->gpio, cfg->irq_pin, GPIO_INPUT | cfg->irq_flags)) {
		LOG_ERR("KX022 : Unable to configure GPIO pin %u", cfg->irq_pin);
		return -EINVAL;
	}

	gpio_init_callback(&data->gpio_cb, kx022_gpio_callback, BIT(cfg->irq_pin));

	if (gpio_add_callback(data->gpio, &data->gpio_cb) < 0) {
		LOG_ERR("KX022 : Could not set gpio callback.");
		return -EIO;
	}

	data->dev = dev;
	switch (KX022_DEFAULT_INT_PIN) {
		case KX022_INT1:
			val = KX022_MASK_INC1_IEN1 | (KX022_DEFAULT_INT_PIN_1_POLARITY << KX022_INC1_IEA1_SHIFT) | (KX022_DEFAULT_INT_PIN_1_RESPONSE << KX022_INC1_IEL1_SHIFT);
			if(data->hw_tf->write_reg(data, KX022_REG_INC1, 
								val) < 0) {
				LOG_ERR("KX022 : Failed set physical int 1");
				return -EIO;
			}
			break;

		case KX022_INT2:
			val = KX022_MASK_INC5_IEN2 | (KX022_DEFAULT_INT_PIN_2_POLARITY << KX022_INC5_IEA2_SHIFT) | (KX022_DEFAULT_INT_PIN_2_RESPONSE << KX022_INC5_IEL2_SHIFT);
			if(data->hw_tf->write_reg(data, KX022_REG_INC5, 
								val) < 0) {
				LOG_ERR("KX022 : Failed set physical int 2");
				return -EIO;
			}
			break;

		default:
			break;
	}

#if defined(CONFIG_KX022_TRIGGER_OWN_THREAD)
	k_sem_init(&data->trig_sem, 0, UINT_MAX);

	k_thread_create(&data->thread, data->thread_stack, CONFIG_KX022_THREAD_STACK_SIZE,
			(k_thread_entry_t)kx022_thread, data, NULL, NULL,
			K_PRIO_COOP(CONFIG_KX022_THREAD_PRIORITY), 0, K_NO_WAIT);
#elif defined(CONFIG_KX022_TRIGGER_GLOBAL_THREAD)
	data->work.handler = kx022_work_cb;

#endif

	if(gpio_pin_interrupt_configure(data->gpio, cfg->irq_pin, GPIO_INT_DISABLE)) {
		LOG_ERR("KX022 : Failed INT_DISABLE");
		return -EINVAL;
	}

	return 0;
}

int kx022_motion_setup(const struct device *dev, sensor_trigger_handler_t handler)
{
	struct kx022_data *data = dev->data;

	data->motion_handler = handler;

	/* khshi */
	if(kx022_mode(dev, KX022_STANDY_MODE) < 0) {
		return -EIO;
	}

	/* Enable Motion detection */
	if(data->hw_tf->update_reg(data, KX022_REG_CNTL1, KX022_MASK_CNTL1_WUFE, 
						KX022_CNTL1_WUFE) < 0) {
		LOG_ERR("KX022 : Failed set motion detect");
		return -EIO;
	}

	if(data->hw_tf->update_reg(data, KX022_REG_CNTL3, KX022_MASK_CNTL3_OWUF, 
						KX022_DEFAULT_MOTION_ODR) < 0) {
		LOG_ERR("KX022 : Failed set motion odr");
		return -EIO;
	}

	if(data->hw_tf->write_reg(data, KX022_REG_INC2, 
						KX022_DEFAULT_INC2) < 0) {
		LOG_ERR("KX022 : Failed set motion axis");
		return -EIO;
	}

	if(data->hw_tf->write_reg(data, KX022_REG_WUFC, 
						KX022_DEFAULT_WUFC_DUR) < 0) {
		LOG_ERR("KX022 : Failed set motion delay");
		return -EIO;
	}

	if(data->hw_tf->write_reg(data, KX022_REG_ATH, 
						KX022_DEFAULT_ATH_THS) < 0) {
		LOG_ERR("KX022 : Failed set motion ath");
		return -EIO;
	}

	if (KX022_DEFAULT_INT_PIN == KX022_INT1) {
		if(data->hw_tf->update_reg(data, KX022_REG_INC4, KX022_MASK_INC4_WUFI1,
							KX022_INC4_WUFI1_SET) < 0) {
			LOG_ERR("KX022 : Failed set motion int1");
			return -EIO;
		}
	} else if (KX022_DEFAULT_INT_PIN == KX022_INT2) {
		if(data->hw_tf->update_reg(data, KX022_REG_INC6, KX022_MASK_INC6_WUFI2,
							KX022_INC6_WUFI2_SET) < 0) {
			LOG_ERR("KX022 : Failed set motion int2");
			return -EIO;
		}
	}

	if(kx022_mode(dev, KX022_OPERATING_MODE) < 0) {
		return -EIO;
	}

	return 0;
}

int kx022_drdy_setup(const struct device *dev, sensor_trigger_handler_t handler)
{
	struct kx022_data *data = dev->data;

	data->drdy_handler = handler;
	
	if(kx022_mode(dev, KX022_STANDY_MODE) < 0) {
		return -EIO;
	}

	if(data->hw_tf->update_reg(data, KX022_REG_CNTL1, KX022_MASK_CNTL1_DRDYE,
						KX022_CNTL1_DRDYE) < 0) {
		LOG_ERR("KX022 : Failed set DRDYE");
		return -EIO;
	}

	if (KX022_DEFAULT_INT_PIN == KX022_INT1) {
		if(data->hw_tf->update_reg(data, KX022_REG_INC4, KX022_MASK_INC4_DRDYI1,
					KX022_INC4_DRDYI1) < 0) {
			LOG_ERR("KX022 : Failed set DRDYE int1");
			return -EIO;
		}
	} else if (KX022_DEFAULT_INT_PIN == KX022_INT2) {
		if(data->hw_tf->update_reg(data, KX022_REG_INC6, KX022_MASK_INC6_DRDYI2,
					KX022_INC6_DRDYI2) < 0) {
			LOG_ERR("KX022 : Failed set DRDYE int2");
			return -EIO;
		}
	}

	if(kx022_mode(dev, KX022_OPERATING_MODE) < 0) {
		return -EIO;
	}

	return 0;
}

int kx022_Tilt_setup(const struct device *dev, sensor_trigger_handler_t handler)
{
	struct kx022_data *data = dev->data;

	data->slope_handler = handler;

	if(kx022_mode(dev, KX022_STANDY_MODE) < 0) {
		return -EIO;
	}

	if(data->hw_tf->update_reg(data, KX022_REG_CNTL1, KX022_MASK_CNTL1_TPE, 
						KX022_CNTL1_TPE_EN) < 0) {
		LOG_ERR("KX022 : Failed set tilt");
		return -EIO;
	}

	//k_msleep(50);
	if(data->hw_tf->write_reg(data, KX022_REG_CNTL2, 
						KX022_CNTL_TILT_ALL_EN) < 0) {
		LOG_ERR("KX022 : Failed set tilt axis");
		return -EIO;
	}
	//k_msleep(50);

	if(data->hw_tf->update_reg(data, KX022_REG_CNTL3, KX022_MASK_CNTL3_OTP,
						(KX022_DEFAULT_TILT_ODR << KX022_CNTL3_OTP_SHIFT)) < 0) {
		LOG_ERR("KX022 : Failed set tilt odr");
		return -EIO;
	}
	//k_msleep(50);

	if(data->hw_tf->write_reg(data, KX022_REG_TILT_TIMER, 
						KX022_DEFAULT_TILT_DUR) < 0) {
		LOG_ERR("KX022 : Failed set tilt timer");
		return -EIO;
	}
	//k_msleep(50);

	if(data->hw_tf->write_reg(data, KX022_REG_TILT_ANGLE_LL, 
						KX022_DEFAULT_TILT_ANGLE_LL) < 0) {
		LOG_ERR("KX022 : Failed set tilt angle ll");
		return -EIO;
	}
	//k_msleep(50);

	if(data->hw_tf->write_reg(data, KX022_REG_TILT_ANGLE_HL, 
						KX022_DEFAULT_TILT_ANGLE_HL) < 0) {
		LOG_ERR("KX022 : Failed set tilt angle hl");
		return -EIO;
	}
	//k_msleep(50);

	//data->hw_tf->write_reg(data, KX022_REG_HYST_SET, KX022_DEF_HYST_SET);
	//k_msleep(50);

	if (KX022_DEFAULT_INT_PIN == KX022_INT1) {
		if(data->hw_tf->update_reg(data, KX022_REG_INC4, KX022_MASK_INC6_TPI2,
					KX022_INC4_TPI1_SET) < 0) {
			LOG_ERR("KX022 : Failed set tilt int1");
			return -EIO;
		}
	} else if (KX022_DEFAULT_INT_PIN == KX022_INT2) {
		if(data->hw_tf->update_reg(data, KX022_REG_INC6, KX022_MASK_INC6_TPI2,
					KX022_INC6_TPI2_SET) < 0) {
			LOG_ERR("KX022 : Failed set tilt int2");
			return -EIO;
		}
	}

	if(kx022_mode(dev, KX022_OPERATING_MODE) < 0) {
		return -EIO;
	}
	//k_msleep(100);

	return 0;
}

int kx022_clear_setup(const struct device *dev)
{
	struct kx022_data *data = dev->data;

	if(kx022_mode(dev, KX022_STANDY_MODE) < 0) {
		return -EIO;
	}

	if(data->hw_tf->write_reg(data, KX022_REG_CNTL1, KX022_DEFAULT_CNTL1) < 0) {
		LOG_ERR("KX022 : Failed set CNTL1");
		return -EIO;
	}

	if(kx022_mode(dev, KX022_OPERATING_MODE) < 0) {
		return -EIO;
	}

	//k_msleep(50);
	return 0;
}

int kx022_trigger_set(const struct device *dev, const struct sensor_trigger *trig,
		      sensor_trigger_handler_t handler)
{
	int ret;
	struct kx022_data *data = dev->data;
	const struct kx022_config *cfg = dev->config;
	uint8_t buf[6];

	switch (trig->type) {
	case SENSOR_TRIG_FREEFALL:
		//kx022_clear_setup(dev);
		ret = kx022_motion_setup(dev, handler);
		break;

	case SENSOR_TRIG_DATA_READY:
		//kx022_clear_setup(dev);
		ret = kx022_drdy_setup(dev, handler);
		break;

	case SENSOR_TRIG_ALL:
		//kx022_clear_setup(dev);
		//kx022_drdy_setup(dev, handler);
		ret = kx022_motion_setup(dev, handler);

		if(ret < 0){
			return ret;
		}

		ret = kx022_Tilt_setup(dev, handler);
		break;

	case SENSOR_TRIG_NEAR_FAR:
		//kx022_clear_setup(dev);
		ret = kx022_Tilt_setup(dev, handler);
		break;

	default:
		return -ENOTSUP;
	}

	if(ret < 0){
		return ret;
	}

	__ASSERT_NO_MSG(trig->type == SENSOR_TRIG_DELTA);

	if(gpio_pin_interrupt_configure(data->gpio, cfg->irq_pin, GPIO_INT_DISABLE)) {
		LOG_ERR("KX022 : Failed INT_DISABLE");
		return -EINVAL;
	}

	if (handler == NULL) {
		LOG_WRN("KX022 : kx022: no handler");
		//return 0;
	}

	/* re-trigger lost interrupt */
	if (data->hw_tf->read_data(data, KX022_REG_XOUT_L, buf, sizeof(buf)) < 0) {
		LOG_ERR("KX022 : status reading error");
		return -EIO;
	}

	if(gpio_pin_interrupt_configure(data->gpio, cfg->irq_pin, GPIO_INT_EDGE_TO_ACTIVE)) {
		LOG_ERR("KX022 : Failed INT_EDGE_TO_ACTIVE");
		return -EINVAL;
	}

	return 0;
}
