/*
 * Copyright (c) 2022 G-Technologies Sdn. Bhd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr.h>
#include <device.h>

#ifndef ZEPHYR_INCLUDE_DRIVERS_MODEM_QUECTEL_H_
#define ZEPHYR_INCLUDE_DRIVERS_MODEM_QUECTEL_H_

#ifdef __cplusplus
extern "C" {
#endif

#if IS_ENABLED(CONFIG_MODEM_GSM_QUECTEL_GNSS)

/* GNSS constellation configuration */
#define GNSS_CONSTELLATION_CFG CONFIG_MODEM_GSM_QUECTEL_GNSS_CONSTELLATION

#define QUECTEL_NMEA_GPS_STR "gpsnmeatype"
#define QUECTEL_NMEA_GLONASS_STR "glonassnmeatype"
#define QUECTEL_NMEA_GALILEO_STR "galileonmeatype"
#define QUECTEL_NMEA_BEIDOU_STR "beidounmeatype"
#define QUECTEL_NMEA_GSVEXT_STR "gsvextnmeatype"

typedef void (*quectel_gnss_cb)(void);

typedef enum quectel_gnss_modes {
	QUECTEL_GNSS_STANDALONE = 0,
	QUECTEL_GNSS_MS_BASED,
	QUECTEL_GNSS_MS_ASSISTED,
	QUECTEL_GNSS_SPEED_OPTIMAL,
	QUECTEL_GNSS_MODE_MAX,
} quectel_gnss_modes_t;

typedef enum quectel_nmea_types {
	QUECTEL_NMEA_GPS = 0,
	QUECTEL_NMEA_GLONASS,
	QUECTEL_NMEA_GALILEO,
	QUECTEL_NMEA_BEIDOU,
	QUECTEL_NMEA_GSVEXT,
} quectel_nmea_types_t;

typedef union quectel_nmea_type {
	struct quectel_nmea_type_gps {
		bool gga;
		bool rmc;
		bool gsv;
		bool gsa;
		bool vtg;
	} gps;

	struct quectel_nmea_type_glonass {
		bool gsv;
		bool gsa;
		bool gns;
	} glonass;

	struct quectel_nmea_type_galileo {
		bool gsv;
	} galileo;

	struct quectel_nmea_type_beidou {
		bool gsa;
		bool gsv;
	} beidou;

	struct quectel_nmea_type_gsvext {
		bool enable;
	} gsvext;
} quectel_nmea_type_t;

struct qlbs_coordinates
{
	/*
	 *     0	OK
	 * 10000	Location fails.
	 * 10001	IMEI number is illegal.
	 * 10002	The token does not exist.
	 * 10003	The token is bound to a device that exceeds the maximum value
	 * 10004	The device’s daily targeting exceeds the maximum number of visits.
	 * 10005	Token location exceeds the maximum number of visits.
	 * 10006	Token expired.
	 * 10007	The IMEI number cannot access the server.
	 * 10008	Token's daily targeting exceeds the maximum number of visits.
	 * 10009	The period of the token is more than the maximum number of visits.
	 *
	 * Other error codes might be HTTP error code
	 */
	int error;
	double latitude;
	double longitude;
};

/**
 * @brief Enable the Quectel GNSS module.
 *
 * @retval 0 if successful
 * @retval errno.h values otherwise
 */
int quectel_gnss_enable(void);

/**
 * @brief Disable the Quectel GNSS module.
 *
 * @retval 0 if successful
 * @retval errno.h values otherwise
 */
int quectel_gnss_disable(void);

/**
 * @brief Get the location from QuecLocator.
 *
 * @param dev Pointer to the GSM modem
 * @param coordinates Pointer to the struct qlbs_coordinates
 *
 * @retval 0 if successful
 * @retval errno.h values otherwise
 */
int quectel_get_qlbs(const struct device *dev, struct qlbs_coordinates *coordinates,
		     k_timeout_t timeout);

void quectel_register_gnss_callback(const struct device *dev, quectel_gnss_cb cb);

#endif /* IS_ENABLED(CONFIG_MODEM_GSM_QUECTEL_GNSS) */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_MODEM_QUECTEL_H_ */
