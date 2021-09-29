/*
 * Copyright (c) 2021 G-Technologies Sdn. Bhd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <device.h>

#ifndef ZEPHYR_INCLUDE_DRIVERS_MODEM_QUECTEL_H_
#define ZEPHYR_INCLUDE_DRIVERS_MODEM_QUECTEL_H_

#ifdef __cplusplus
extern "C" {
#endif

#if IS_ENABLED(CONFIG_MODEM_GSM_QUECTEL_GNSS)

/* Maximum GNSS positioning time in seconds */
#define FIX_MAX_TIME CONFIG_MODEM_GSM_QUECTEL_GNSS_FIX_MAX_TIME_S
/* Accuracy threshold of positioning in meter */
#define FIX_MAX_DIST CONFIG_MODEM_GSM_QUECTEL_GNSS_FIX_MAX_DIST_M
/* Number of attempts for positioning. */
#define FIX_COUNT CONFIG_MODEM_GSM_QUECTEL_GNSS_FIX_COUNT
/* Interval between each GNSS fix in seconds */
#define FIX_INTERVAL CONFIG_MODEM_GSM_QUECTEL_GNSS_FIX_INTERVAL_S
/* GNSS constellation configuration */
#define GNSS_CONSTELLATION_CFG CONFIG_MODEM_GSM_QUECTEL_GNSS_CONSTELLATION

#define QUECTEL_NMEA_GPS_STR "gpsnmeatype"
#define QUECTEL_NMEA_GLONASS_STR "glonassnmeatype"
#define QUECTEL_NMEA_GALILEO_STR "galileonmeatype"
#define QUECTEL_NMEA_BEIDOU_STR "beidounmeatype"
#define QUECTEL_NMEA_GSVEXT_STR "gsvextnmeatype"

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

#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_MODEM_QUECTEL_H_ */
