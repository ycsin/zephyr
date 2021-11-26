/* Kionix KX022 3-axis accelerometer driver
 *
 * Copyright (c) 2021 G-Technologies Sdn. Bhd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT kionix_kx022

#include <drivers/sensor.h>
#include <kernel.h>
#include <device.h>
#include <init.h>
#include <string.h>
#include <sys/byteorder.h>
#include <sys/__assert.h>
#include <logging/log.h>

#include "kx022.h"

LOG_MODULE_REGISTER(KX022, CONFIG_SENSOR_LOG_LEVEL);
 static struct kx022_data dev_data;
static struct kx022_config dev_config = {
	.comm_master_dev_name = DT_INST_BUS_LABEL(0),
#if DT_ANY_INST_ON_BUS_STATUS_OKAY(spi)
#error "KX022 : KX022 SPI NOT IMPLEMENTED"
#elif DT_ANY_INST_ON_BUS_STATUS_OKAY(i2c)
	.bus_init = kx022_i2c_init,
#else
#error "KX022 : BUS MACRO NOT DEFINED IN DTS"
#endif

#ifdef CONFIG_KX022_TRIGGER
	.irq_port = DT_INST_GPIO_LABEL(0, int_gpios),
	.irq_pin = DT_INST_GPIO_PIN(0, int_gpios),
	.irq_flags = DT_INST_GPIO_FLAGS(0, int_gpios),
#endif

};

//#define kx022_delay(msec) k_msleep(((uint16_t)msec) * 1000 / CONFIG_KX022_ODR + 1);

#ifdef CONFIG_KX022_DIAGNOSTIC_MODE
static struct kx022_configuration kx022_setting;
#endif

/************************************************************
 * info: kx022_mode use to set pc1 in standby or operating mode
 * input: 0 - standby
 *        1 - operating
 * **********************************************************/
int kx022_mode(const struct device *dev, bool mode)
{
	struct kx022_data *data = dev->data;
	uint8_t chip_id;
	uint8_t val = mode << KX022_CNTL1_PC1_SHIFT;

	/*khshi*/
	if(mode == KX022_STANDY_MODE){
		if (data->hw_tf->read_reg(data, KX022_REG_WHO_AM_I, &chip_id) < 0) {
			LOG_DBG("Failed reading chip id");
			return -EIO;
		}

		if (chip_id != KX022_VAL_WHO_AM_I) {
			LOG_DBG("KX022 : Invalid chip id 0x%x", chip_id);
			return -EIO;
		}

		if (data->hw_tf->update_reg(data, KX022_REG_CNTL1, KX022_MASK_CNTL1_PC1,
					val) < 0) {
			LOG_DBG("KX022 : Failed to set KX022 standby");
			return -EIO;
		}
	} else if(mode == KX022_OPERATING_MODE) {
		if (data->hw_tf->update_reg(data, KX022_REG_CNTL1, KX022_MASK_CNTL1_PC1,
					val) < 0) {
			LOG_DBG("KX022 : Failed to set KX022 operating mode");
			return -EIO;
		}
	} else {
		return -ENOTSUP;
	}

	return 0;
	/*khshi*/
}

#ifdef CONFIG_KX022_ODR_RUNTIME
static int kx022_accel_odr_set(const struct device *dev, uint16_t freq)
{
	struct kx022_data *data = dev->data;

	/*khshi*/
	if(freq > KX022_ODR_RANGE_MAX) {
		return -EINVAL;
	} 

	if (data->hw_tf->update_reg(data, KX022_REG_ODCNTL, KX022_MASK_ODCNTL_OSA,
					    (uint8_t)freq) < 0) {
		LOG_DBG("KX022 : Failed to set KX022 odr");
		return -EIO;
	}

	/*khshi*/

	return 0;
}
#endif /* defined(KX022_ODR_RUNTIME) */

#ifdef CONFIG_KX022_FS_RUNTIME
static int kx022_accel_range_set(const struct device *dev, int32_t range)
{
	struct kx022_data *data = dev->data;

	/*khshi*/
	if(range > KX022_FS_RANGE_MAX) {
		return -EINVAL;
	}

	if (data->hw_tf->update_reg(data, KX022_REG_CNTL1, KX022_MASK_CNTL1_GSEL, 
						((uint8_t)range << KX022_CNTL1_GSEL_SHIFT)) < 0) {
		LOG_DBG("KX022 : Failed to set kx022 full-scale");
		return -EIO;
	}

	if(range == KX022_FS_2G){
		data->gain = (float)(range * GAIN_XL);
	} else {
		data->gain = (float)(2 * range * GAIN_XL);
	}
	/* khshi */

	return 0;
}
#endif /* KX022_FS_RUNTIME */

#ifdef CONFIG_KX022_RES_RUNTIME
static int kx022_accel_res_set(const struct device *dev, uint16_t res)
{
	struct kx022_data *data = dev->data;

	/*khshi*/
	if(res > KX022_RES_RANGE_MAX) {
		return -EINVAL;
	}

	if (data->hw_tf->update_reg(data, KX022_REG_CNTL1, KX022_MASK_CNTL1_RES,
					    ((uint8_t)res << KX022_CNTL1_RES_SHIFT)) < 0) {
		LOG_DBG("KX022 : Failed to set KX022 res");
		return -EIO;
	}
	/* khshi */

	return 0;
}
#endif /* KX022_RES_RUNTIME */

#ifdef CONFIG_KX022_WUFC_RUNTIME
static int KX022_accel_wufc_set(const struct device *dev, uint16_t delay)
{
	struct kx022_data *data = dev->data;

	if(delay > KX022_WUFC_RANGE_MAX) {
		return -EINVAL;
	}

	if (data->hw_tf->write_reg(data, KX022_REG_WUFC, 
						(uint8_t)delay) < 0) {
		LOG_DBG("KX022 : Failed to set KX022 wufc");
		return -EIO;
	}

	return 0;
}
#endif /* KX022_WUFC_RUNTIME */

#if CONFIG_KX022_TILT_TIMER_RUNTIME
static int KX022_accel_timer_set(const struct device *dev, uint16_t delay)
{
	struct kx022_data *data = dev->data;

	if(delay > KX022_TILT_TIMER_RANGE_MAX) {
		return -EINVAL;
	}

	if (data->hw_tf->write_reg(data, KX022_REG_TILT_TIMER, 
						(uint8_t)delay) < 0) {
		LOG_DBG("KX022 : Failed to set KX022 tilt timer");
		return -EIO;
	}

	return 0;
}
#endif /* KX022_TILT_TIMER_RUNTIME */

static int kx022_accel_config(const struct device *dev, enum sensor_channel chan,
			      enum sensor_attribute attr, const struct sensor_value *val)
{
	switch (attr) {
#ifdef CONFIG_KX022_FS_RUNTIME
	case SENSOR_ATTR_FULL_SCALE:
		return kx022_accel_range_set(dev, val->val1);
#endif /* CONFIG_KX022_FS_RUNTIME */

#ifdef CONFIG_KX022_ODR_RUNTIME
	case SENSOR_ATTR_ODR:
		return kx022_accel_odr_set(dev, val->val1);
#endif /* CONFIG_KX022_ODR_RUNTIME */

#ifdef CONFIG_KX022_RES_RUNTIME
	case SENSOR_ATTR_RESOLUTION:
		return kx022_accel_res_set(dev, val->val1);
#endif /* CONFIG_KX022_RES_RUNTIME */

#ifdef CONFIG_KX022_WUFC_RUNTIME
	case SENSOR_ATTR_WUFF_DUR:
		return KX022_accel_wufc_set(dev, val->val1);
		break;
#endif /* CONFIG_KX022_WUFC_RUNTIME */

#ifdef CONFIG_KX022_TILT_TIMER_RUNTIME
	case SENSOR_ATTR_SLOPE_DUR:
		return KX022_accel_timer_set(dev, val->val1);
		break;
#endif /* CONFIG_KX022_TILT_TIMER_RUNTIME */

	default:
		LOG_DBG("KX022 : Accel attribute not supported.");
		return -ENOTSUP;
	}

	return 0;
}

/* khshi */
static int KX022_accel_wuff_set(const struct device *dev, uint16_t ath)
{
	struct kx022_data *data = dev->data;

	if(ath > KX022_ATH_RANGE_MAX) {
		return -EINVAL;
	}

	if (ath == 0) {
#if 0	
		// disable motion detect
		if(data->hw_tf->update_reg(data, KX022_REG_CNTL1, KX022_MASK_CNTL1_WUFE, 
						KX022_CNTL1_WUFE_RESET) < 0) {
			LOG_ERR("KX022 : Failed to disable motion detect");
			return -EIO;
		}
#endif

		switch (KX022_DEFAULT_INT_PIN) {
			case KX022_INT1:
				if (data->hw_tf->update_reg(data, KX022_REG_INC4, KX022_MASK_INC4_WUFI1,
							KX022_INC4_WUFI1_RESET) < 0) {
					LOG_DBG("KX022 : Failed to set KX022 int1");
					return -EIO;
				}
				break;

			case KX022_INT2:
				if (data->hw_tf->update_reg(data, KX022_REG_INC6, KX022_MASK_INC6_WUFI2,
							KX022_INC6_WUFI2_RESET) < 0) {
					LOG_DBG("KX022 : Failed to reset KX022 int2");
					return -EIO;
				}
				break;

			default:
				return -ENOTSUP;
				break;
		}
	} else {
		if (data->hw_tf->write_reg(data, KX022_REG_ATH, 
							(uint8_t)ath) < 0) {
			LOG_DBG("KX022 : Failed to set KX022 ath");
			return -EIO;
		}
	}

	return 0;
}

static int kx022_motion_detect_config(const struct device *dev, enum sensor_attribute attr,
				const struct sensor_value *val)
{
	switch (attr) {
	case SENSOR_ATTR_WUFF_TH:
		return KX022_accel_wuff_set(dev, val->val1);
		break;

	default:
		LOG_DBG("KX022 : Accel attribute not supported.");
		return -ENOTSUP;
	}
	
	return 0;
}

static int KX022_accel_angle_set(const struct device *dev, uint16_t angle)
{
	struct kx022_data *data = dev->data;

	if(angle > KX022_TILT_ANGLE_LL_RANGE_MAX) {
		return -EINVAL;
	}

	if (angle == 0) {
#if 0		
		// disable the tilt
		if(data->hw_tf->update_reg(data, KX022_REG_CNTL1, KX022_MASK_CNTL1_TPE, 
						KX022_CNTL1_TPE_RESET) < 0) {
			LOG_ERR("KX022 : Failed to disable tilt");
			return -EIO;
		}
#endif

		switch (KX022_DEFAULT_INT_PIN) {
			case KX022_INT1:
				if (data->hw_tf->update_reg(data, KX022_REG_INC4, KX022_MASK_INC4_TPI1,
							KX022_INC4_TPI1_RESET) < 0) {
					LOG_DBG("KX022 : Failed to set KX022 tilt int1");
					return -EIO;
				}
				break;

			case KX022_INT2:
				if (data->hw_tf->update_reg(data, KX022_REG_INC6, KX022_MASK_INC6_TPI2,
							KX022_INC6_TPI2_RESET) < 0) {
					LOG_DBG("KX022 : Failed to reset KX022 tilt int2");
					return -EIO;
				}
				break;

			default:
				return -ENOTSUP;
				break;
		}
	} else {
		if (data->hw_tf->write_reg(data, KX022_REG_TILT_ANGLE_LL, 
							(uint8_t)angle) < 0) {
			LOG_DBG("KX022 : Failed to set KX022 tilt angle ll");
			return -EIO;
		}
	}

	return 0;
}

static int kx022_tilt_position_config(const struct device *dev, enum sensor_attribute attr,
				const struct sensor_value *val)
{
	switch (attr) {
	case SENSOR_ATTR_SLOPE_TH:
		return KX022_accel_angle_set(dev, val->val1);
		break;

	default:
		LOG_DBG("KX022 : Accel attribute not supported.");
		return -ENOTSUP;
	}
	
	return 0;
}
/* khshi */

static int kx022_attr_set(const struct device *dev, enum sensor_channel chan,
			  enum sensor_attribute attr, const struct sensor_value *val)
{
	int ret;

	/* khshi */
	if (kx022_mode(dev, KX022_STANDY_MODE) < 0){
		return -EIO;
	}

	switch (chan) {
	case SENSOR_CHAN_ACCEL_XYZ:
		ret = kx022_accel_config(dev, chan, attr, val);
		break;
	case SENSOR_CHAN_FREE_FALL:
		ret = kx022_motion_detect_config(dev, attr, val);
		break;
	case SENSOR_CHAN_NEAR_FAR:
		ret = kx022_tilt_position_config(dev, attr, val);
		break;
	default:
		LOG_WRN("KX022 : Attr_set() not supported on this channel.");
		ret = -ENOTSUP;
	}

	if (kx022_mode(dev, KX022_OPERATING_MODE) < 0){
		return -EIO;
	}

	return ret;
	/* khshi */
}

/* khshi dignostic mode */
#ifdef CONFIG_KX022_DIAGNOSTIC_MODE
static inline void kx022_data_get(struct sensor_value *val, int length, int raw_val1, int raw_val2)
{
	uint16_t dval;

	dval = (int8_t)raw_val1;
	val->val1 = dval;

	if(length >= 2){
		dval = (int8_t)raw_val2;
		val->val2 = dval;
	}
}

static int kx022_accel_get(const struct device *dev, struct sensor_value *val)
{
	struct kx022_data *data = dev->data;
	uint8_t rd_data;

	/* setting */
	if(data->hw_tf->read_reg(data, KX022_REG_CNTL1, &rd_data) < 0){
		LOG_DBG("KX022 : Failed to read CNTL1");
		return -EIO;
	}
	kx022_setting.operating_mode = (rd_data & KX022_MASK_CNTL1_PC1) >> 7;
	kx022_setting.resolution = (rd_data & KX022_MASK_CNTL1_RES) >> 6;
	kx022_setting.acceleration_range = (rd_data & KX022_MASK_CNTL1_GSEL) >> 3;
	kx022_setting.tap_enable = (rd_data & KX022_MASK_CNTL1_TDTE) >> 2;
	kx022_setting.motion_detect_enable = (rd_data & KX022_MASK_CNTL1_WUFE) >> 1;
	kx022_setting.tilt_enable = rd_data & KX022_MASK_CNTL1_TPE;
	
	/* data output rate */
	if(data->hw_tf->read_reg(data, KX022_REG_ODCNTL, &rd_data) < 0){
		LOG_DBG("KX022 : Failed to read ODCNTL");
		return -EIO;
	}
	kx022_setting.ODR= rd_data & KX022_MASK_ODCNTL_OSA;

	/* interrupt 1 setting */
	if(data->hw_tf->read_reg(data, KX022_REG_INC1, &rd_data) < 0){
		LOG_DBG("KX022 : Failed to read INC1");
		return -EIO;
	}
	kx022_setting.pin_1.enable = (rd_data & KX022_MASK_INC5_IEN2) >> 5;
	kx022_setting.pin_1.polarity = (rd_data & KX022_MASK_INC5_IEA2) >> 4;

	/* interrupt 1  event */
	if(data->hw_tf->read_reg(data, KX022_REG_INC4, &rd_data) < 0){
		LOG_DBG("KX022 : Failed to read INC4");
		return -EIO;
	}
	kx022_setting.pin_1.tap = (rd_data & KX022_MASK_INC4_TDTI1) >> 2; 
	kx022_setting.pin_1.motion_detect = (rd_data & KX022_MASK_INC4_WUFI1) >> 1; 
	kx022_setting.pin_1.tilt = rd_data & KX022_MASK_INC4_TPI1; 

	/* interrupt 2 setting */
	if(data->hw_tf->read_reg(data, KX022_REG_INC5, &rd_data) < 0){
		LOG_DBG("KX022 : Failed to read INC5");
		return -EIO;
	}
	kx022_setting.pin_2.enable = (rd_data & KX022_MASK_INC5_IEN2) >> 5;
	kx022_setting.pin_2.polarity = (rd_data & KX022_MASK_INC5_IEA2) >> 4;

	/* interrupt 2 event */
	if(data->hw_tf->read_reg(data, KX022_REG_INC6, &rd_data) < 0){
		LOG_DBG("KX022 : Failed to read INC6");
		return -EIO;
	}
	kx022_setting.pin_2.tap = (rd_data & KX022_MASK_INC4_TDTI1) >> 2; 
	kx022_setting.pin_2.motion_detect = (rd_data & KX022_MASK_INC4_WUFI1) >> 1; 
	kx022_setting.pin_2.tilt = rd_data & KX022_MASK_INC4_TPI1; 

	/* sampling rate */
	if(data->hw_tf->read_reg(data, KX022_REG_LP_CNTL, &rd_data) < 0){
		LOG_DBG("KX022 : Failed to read LP_CNTL");
		return -EIO;
	}
	kx022_setting.sampling_rate = (rd_data & KX022_MASK_LP_CNTL_AVC) >> 4;

	kx022_data_get(val, 2, kx022_setting.operating_mode, kx022_setting.resolution);
	kx022_data_get(val + 1, 2, kx022_setting.acceleration_range, kx022_setting.ODR);
	kx022_data_get(val + 2, 2, kx022_setting.tap_enable, kx022_setting.motion_detect_enable);
	kx022_data_get(val + 3, 2, kx022_setting.tilt_enable, kx022_setting.sampling_rate);

	kx022_data_get(val + 4, 2, kx022_setting.pin_1.enable, kx022_setting.pin_1.polarity);
	kx022_data_get(val + 5, 2, kx022_setting.pin_1.tap, kx022_setting.pin_1.motion_detect);
	kx022_data_get(val + 6, 2, kx022_setting.pin_1.tilt, 0);

	kx022_data_get(val + 7, 2, kx022_setting.pin_2.enable, kx022_setting.pin_2.polarity);
	kx022_data_get(val + 8, 2, kx022_setting.pin_2.tap, kx022_setting.pin_2.motion_detect);
	kx022_data_get(val + 9, 2, kx022_setting.pin_2.tilt, 0);

	return 0;
}

static int kx022_accel_motion_get(const struct device *dev, struct sensor_value *val)
{
	struct kx022_data *data = dev->data;
	uint8_t rd_data;

	if(data->hw_tf->read_reg(data, KX022_REG_INC2, &rd_data) < 0){
		LOG_DBG("KX022 : Failed to read INC2");
		return -EIO;
	}
	kx022_setting.motion_detect.xn_direction = (rd_data & KX022_MASK_INC2_XNWUE) >> 5;
	kx022_setting.motion_detect.xp_direction = (rd_data & KX022_MASK_INC2_XPWUE) >> 4;
	kx022_setting.motion_detect.yn_direction = (rd_data & KX022_MASK_INC2_YNWUE) >> 3;
	kx022_setting.motion_detect.yp_direction = (rd_data & KX022_MASK_INC2_YPWUE) >> 2;
	kx022_setting.motion_detect.zn_direction = (rd_data & KX022_MASK_INC2_ZNWUE) >> 1;
	kx022_setting.motion_detect.zp_direction = rd_data & KX022_MASK_INC2_ZPWUE;

	if(data->hw_tf->read_reg(data, KX022_REG_CNTL3, &rd_data) < 0){
		LOG_DBG("KX022 : Failed to read CNTL3");
		return -EIO;
	}
	kx022_setting.motion_detect.ODR = rd_data & KX022_MASK_CNTL3_OWUF;

	if(data->hw_tf->read_reg(data, KX022_REG_WUFC, &rd_data) < 0){
		LOG_DBG("KX022 : Failed to read WUFC");
		return -EIO;
	}
	kx022_setting.motion_detect.delay = rd_data;

	if(data->hw_tf->read_reg(data, KX022_REG_ATH, &rd_data) < 0){
		LOG_DBG("KX022 : Failed to read ATH");
		return -EIO;
	}
	kx022_setting.motion_detect.gravity = rd_data;

	kx022_data_get(val, 2, kx022_setting.motion_detect.xn_direction, kx022_setting.motion_detect.xp_direction);
	kx022_data_get(val + 1, 2, kx022_setting.motion_detect.yn_direction, kx022_setting.motion_detect.yp_direction);
	kx022_data_get(val + 1, 2, kx022_setting.motion_detect.zn_direction, kx022_setting.motion_detect.zp_direction);
	kx022_data_get(val + 1, 2, kx022_setting.motion_detect.ODR, kx022_setting.motion_detect.delay);
	kx022_data_get(val + 1, 1, kx022_setting.motion_detect.gravity, 0);

	return 0;
}

static int kx022_accel_tilt_get(const struct device *dev, struct sensor_value *val)
{
	struct kx022_data *data = dev->data;
	uint8_t rd_data;

	if(data->hw_tf->read_reg(data, KX022_REG_CNTL2, &rd_data) < 0){
		LOG_DBG("KX022 : Failed to read CNTL2");
		return -EIO;
	}
	kx022_setting.tilt.xn_direction = (rd_data & KX022_MASK_CNTL2_LEM) >> 5;
	kx022_setting.tilt.xp_direction = (rd_data & KX022_MASK_CNTL2_RIM) >> 4;
	kx022_setting.tilt.yn_direction = (rd_data & KX022_MASK_CNTL2_DOM) >> 3;
	kx022_setting.tilt.yp_direction = (rd_data & KX022_MASK_CNTL2_UPM) >> 2;
	kx022_setting.tilt.zn_direction = (rd_data & KX022_MASK_CNTL2_FDM) >> 1;
	kx022_setting.tilt.zp_direction = rd_data & KX022_MASK_CNTL2_FUM;

	if(data->hw_tf->read_reg(data, KX022_REG_CNTL3, &rd_data) < 0){
		LOG_DBG("KX022 : Failed to read CNTL3");
		return -EIO;
	}
	kx022_setting.tilt.ODR = (rd_data & KX022_MASK_CNTL3_OTP) >> 6;

	if(data->hw_tf->read_reg(data, KX022_REG_TILT_TIMER, &rd_data) < 0){
		LOG_DBG("KX022 : Failed to read TILT TIMER");
		return -EIO;
	}
	kx022_setting.tilt.delay = rd_data & KX022_MASK_TILT_TIMER_TSC;

	if(data->hw_tf->read_reg(data, KX022_REG_TILT_ANGLE_LL, &rd_data) < 0){
		LOG_DBG("KX022 : Failed to read ");
		return -EIO;
	}
	kx022_setting.tilt.angle_ll = rd_data & KX022_MASK_LL_LL;

	if(data->hw_tf->read_reg(data, KX022_REG_TILT_ANGLE_HL, &rd_data) < 0){
		LOG_DBG("KX022 : Failed to read TILT TIMER");
		return -EIO;
	}
	kx022_setting.tilt.angle_hl = rd_data & KX022_MASK_HL_HL;

	kx022_data_get(val, 2, kx022_setting.tilt.xn_direction, kx022_setting.tilt.xp_direction);
	kx022_data_get(val + 1, 2, kx022_setting.tilt.yn_direction, kx022_setting.tilt.yp_direction);
	kx022_data_get(val + 1, 2, kx022_setting.tilt.zn_direction, kx022_setting.tilt.zp_direction);
	kx022_data_get(val + 1, 2, kx022_setting.tilt.ODR, kx022_setting.tilt.delay);

	return 0;
}

static int kx022_attr_get(const struct device *dev, enum sensor_channel chan,
			  enum sensor_attribute attr, struct sensor_value *val)
{
	switch (chan) {
	case SENSOR_ATTR_CONFIGURATION:
		return kx022_accel_get(dev, val);
	case SENSOR_CHAN_FREE_FALL:
		return kx022_accel_motion_get(dev, val);
		break;
	case SENSOR_CHAN_NEAR_FAR:
		return kx022_accel_tilt_get(dev, val);
		break;
	default:
		LOG_WRN("KX022 : Attr_get() not supported on this channel.");
		return -ENOTSUP;
	}

	return 0;
}
#endif /*CONFIG_KX022_DIAGNOSTIC_MODE*/
/* khshi dignostic mode */

static int kx022_sample_fetch_accel(const struct device *dev, uint8_t reg_addr)
{
	struct kx022_data *data = dev->data;
	uint8_t buf[2];
	uint16_t val;

	if (data->hw_tf->read_data(data, reg_addr, buf, sizeof(buf)) < 0) {
		LOG_DBG("KX022 : Failed to read sample");
		return -EIO;
	}

	val = (int16_t)((uint16_t)(buf[0]) | ((uint16_t)(buf[1]) << 8));

	switch (reg_addr) {
	case KX022_REG_XOUT_L:
		data->sample_x = val;
		break;
	case KX022_REG_YOUT_L:
		data->sample_y = val;
		break;
	case KX022_REG_ZOUT_L:
		data->sample_z = val;
		break;
	default:
		LOG_ERR("KX022 : Invalid register address");
		return -EIO;
	}

	return 0;
}

#define kx022_sample_fetch_accel_x(dev) kx022_sample_fetch_accel(dev, KX022_REG_XOUT_L)
#define kx022_sample_fetch_accel_y(dev) kx022_sample_fetch_accel(dev, KX022_REG_YOUT_L)
#define kx022_sample_fetch_accel_z(dev) kx022_sample_fetch_accel(dev, KX022_REG_ZOUT_L)

static int kx022_sample_fetch_accel_xyz(const struct device *dev)
{
	struct kx022_data *data = dev->data;
	uint8_t buf[6];

	if (data->hw_tf->read_data(data, KX022_REG_XOUT_L, buf, sizeof(buf)) < 0) {
		LOG_DBG("KX022 : Failed to read sample");
		return -EIO;
	}

	data->sample_x = (int16_t)((uint16_t)(buf[0]) | ((uint16_t)(buf[1]) << 8));
	data->sample_y = (int16_t)((uint16_t)(buf[2]) | ((uint16_t)(buf[3]) << 8));
	data->sample_z = (int16_t)((uint16_t)(buf[4]) | ((uint16_t)(buf[5]) << 8));

	return 0;
}

static int kx022_tilt_pos(const struct device *dev)
{
	struct kx022_data *data = dev->data;
	uint8_t rd_data[2];

	data->hw_tf->read_reg(data, KX022_REG_TSCP, &rd_data[0]);
	data->hw_tf->read_reg(data, KX022_REG_TSPP, &rd_data[1]);

	data->sample_tscp = rd_data[0];
	data->sample_tspp = rd_data[1];

	return 0;
}

static int kx022_motion_direction(const struct device *dev)
{
	struct kx022_data *data = dev->data;
	uint8_t rd_data;

	data->hw_tf->read_reg(data, KX022_REG_INS3, &rd_data);

	data->sample_motion_dir = rd_data;

	return 0;
}

static int kx022_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	switch (chan) {
	case SENSOR_CHAN_ACCEL_X:
		kx022_sample_fetch_accel_x(dev);
		break;
	case SENSOR_CHAN_ACCEL_Y:
		kx022_sample_fetch_accel_y(dev);
		break;
	case SENSOR_CHAN_ACCEL_Z:
		kx022_sample_fetch_accel_z(dev);
		break;
	case SENSOR_CHAN_ACCEL_XYZ:
		kx022_sample_fetch_accel_xyz(dev);
		break;
	case SENSOR_CHAN_FREE_FALL:
		kx022_motion_direction(dev);
		break;
	case SENSOR_CHAN_ALL:
		kx022_sample_fetch_accel_xyz(dev);
		kx022_tilt_pos(dev);
		kx022_motion_direction(dev);
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

static inline void kx022_convert(struct sensor_value *val, int raw_val, float gain)
{
	int64_t dval;

	/* Gain is in mg/LSB */
	/* Convert to m/s^2 */
	dval = ((int64_t)raw_val * gain * SENSOR_G) / 1000;
	val->val1 = dval / 1000000LL;
	val->val2 = dval % 1000000LL;
}
static inline void kx022_tilt_pos_get(struct sensor_value *val, int raw_val)
{
	uint16_t dval;

	dval = (int16_t)raw_val;
	val->val1 = dval;
}

static inline void kx022_motion_dir_get(struct sensor_value *val, int raw_val)
{
	uint16_t dval;

	dval = (int16_t)raw_val;
	val->val1 = dval;
}
static inline int kx022_get_channel(enum sensor_channel chan, struct sensor_value *val,
				    struct kx022_data *data, float gain)
{
	switch (chan) {
	case SENSOR_CHAN_ACCEL_X:
		kx022_convert(val, data->sample_x, gain);
		break;
	case SENSOR_CHAN_ACCEL_Y:
		kx022_convert(val, data->sample_y, gain);
		break;
	case SENSOR_CHAN_ACCEL_Z:
		kx022_convert(val, data->sample_z, gain);
		break;
	case SENSOR_CHAN_ACCEL_XYZ:
		kx022_convert(val, data->sample_x, gain);
		kx022_convert(val + 1, data->sample_y, gain);
		kx022_convert(val + 2, data->sample_z, gain);
		break;
	case SENSOR_CHAN_FREE_FALL:
		kx022_motion_dir_get(val, data->sample_motion_dir);
		break;
	case SENSOR_CHAN_NEAR_FAR:
		kx022_tilt_pos_get(val, data->sample_tspp);
		kx022_tilt_pos_get(val + 1, data->sample_tscp);
		break;
	case SENSOR_CHAN_ALL:
		kx022_convert(val, data->sample_x, gain);
		kx022_convert(val + 1, data->sample_y, gain);
		kx022_convert(val + 2, data->sample_z, gain);
		kx022_tilt_pos_get(val + 3, data->sample_tspp);
		kx022_tilt_pos_get(val + 4, data->sample_tscp);
		kx022_motion_dir_get(val + 5, data->sample_motion_dir);

		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

static int kx022_channel_get(const struct device *dev, enum sensor_channel chan,
			     struct sensor_value *val)
{
	struct kx022_data *data = dev->data;

	return kx022_get_channel(chan, val, data, data->gain);
}

static const struct sensor_driver_api kx022_api_funcs = {
	.attr_set = kx022_attr_set,
/* khshi dignostic mode */
#ifdef CONFIG_KX022_DIAGNOSTIC_MODE
	.attr_get = kx022_attr_get,		/* attr get */
#endif 
/* khshi dignostic mode */	
#ifdef CONFIG_KX022_TRIGGER
	.trigger_set = kx022_trigger_set,
#endif
	.sample_fetch = kx022_sample_fetch,
	.channel_get = kx022_channel_get,
};

static int kx022_init(const struct device *dev)
{
	const struct kx022_config *const config = dev->config;
	struct kx022_data *data = dev->data;
	uint8_t chip_id;
	uint8_t val;

	data->comm_master = device_get_binding(config->comm_master_dev_name);
	if (!data->comm_master) {
		LOG_DBG("KX022 : Master not found: %s", config->comm_master_dev_name);
		return -EINVAL;
	}
	k_msleep(2000);

	if (config->bus_init(dev) < 0) {
		return -EINVAL;
	}


	if (data->hw_tf->read_reg(data, KX022_REG_WHO_AM_I, &chip_id) < 0) {
		LOG_DBG("KX022 : Failed reading chip id");
		return -EIO;
	}

	if (chip_id != KX022_VAL_WHO_AM_I) {
		LOG_DBG("KX022 : Invalid chip id 0x%x", chip_id);
		return -EIO;
	}

	LOG_DBG("KX022 : Chip id 0x%x", chip_id);

	/* s/w reset the sensor */
	if (data->hw_tf->update_reg(data, KX022_REG_CNTL2, KX022_MASK_CNTL2_SRST, KX022_MASK_CNTL2_SRST) < 0) {
		LOG_DBG("s/w reset fail");
		return -EIO;
	}
	k_msleep(2000);

	/* Make sure the KX022 is stopped before we configure */
	val = (KX022_DEFAULT_RES << KX022_CNTL1_RES_SHIFT) | (KX022_DEFAULT_FS << KX022_CNTL1_GSEL_SHIFT);
	if (data->hw_tf->write_reg(data, KX022_REG_CNTL1, 
						val) < 0) {
		LOG_DBG("KX022 : Failed CNTL1");
		return -EIO;
	}

	/* Set KX022 default odr */
	if (data->hw_tf->update_reg(data, KX022_REG_ODCNTL, KX022_MASK_ODCNTL_OSA,
				    	KX022_DEFAULT_ODR) < 0) {
		LOG_DBG("KX022 : Failed setting odr");
		return -EIO;
	}

#ifdef CONFIG_KX022_TRIGGER
	if (kx022_trigger_init(dev) < 0) {
		LOG_ERR("KX022 : Failed to initialize triggers.");
		return -EIO;
	}
#endif

	/* Set Kx022 to Operating Mode */
	if (kx022_mode(dev, KX022_OPERATING_MODE) < 0){
		return -EIO;
	}

	k_msleep(2000);

	data->gain = KX022_DEFAULT_GAIN;

	return 0;
}

DEVICE_DT_INST_DEFINE(0, kx022_init, NULL, &dev_data, &dev_config, POST_KERNEL,
		      CONFIG_SENSOR_INIT_PRIORITY, &kx022_api_funcs);
