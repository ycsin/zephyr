/*
 * Copyright (c) 2021, floras shi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Extended public API for Kionix's KX022 sensors
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_KX022_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_KX022_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <drivers/sensor.h>
#include <device.h>

enum sensor_channel_kx022 {
	/* TILT */
	SENSOR_CHAN_KX022_TILT = SENSOR_CHAN_PRIV_START,

	/* MOTION */
	SENSOR_CHAN_KX022_MOTION,

	/* configure kx022*/
	SENSOR_CHAN_KX022_CFG
};

enum sensor_trigger_type_kx022 {
	/* TILT */
	SENSOR_TRIG_KX022_TILT = SENSOR_TRIG_PRIV_START,

	/* MOTION DETECT */
	SENSOR_TRIG_KX022_MOTION
};

enum sensor_attribute_kx022 {
	/* Configure the resolution of a sensor */
	SENSOR_ATTR_KX022_RESOLUTION = SENSOR_ATTR_PRIV_START,

	/* Configure odr of a sensor */
	SENSOR_ATTR_KX022_ODR,

	/* motion detection timer */
	SENSOR_ATTR_KX022_MOTION_DETECTION_TIMER,

	/* motion detect threshold */
	SENSOR_ATTR_KX022_MOTION_DETECT_THRESHOLD,

	/* tilt timer */
	SENSOR_ATTR_KX022_TILT_TIMER,

	/* tilt angle ll */
	SENSOR_ATTR_KX022_TILT_ANGLE_LL
};

#ifdef __cplusplus
}
#endif

/**
 * @brief Callback API for disable kx022 trigger
 *
 * @param dev Pointer to the sensor device
 *
 * @param trig Pointer to the sensor trigger type
 *
 * @return 0 if successful, negative errno code if failure.
 */
int kx022_restore_default_trigger_setup(const struct device *dev,
					const struct sensor_trigger *trig);

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_SHT4X_H_ */
