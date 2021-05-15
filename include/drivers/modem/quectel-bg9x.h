/** @file
 * @brief Quectel BG9X modem public API header file.
 *
 * Allows an application to control the Quectel BG9X modem.
 *
 * Copyright (c) 2021 G-Technologies Sdn. Bhd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
// #if CONFIG_MODEM_QUECTEL_GNSS
#ifndef ZEPHYR_INCLUDE_DRIVERS_MODEM_QUECTEL_H_
#define ZEPHYR_INCLUDE_DRIVERS_MODEM_QUECTEL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#ifdef CONFIG_NEWLIB_LIBC
#include <time.h>
#endif

typedef enum mdm_quectel_gnss_modes
{
    MDM_QUECTEL_GNSS_STANDALONE = 0,
    MDM_QUECTEL_GNSS_MS_BASED,
    MDM_QUECTEL_GNSS_MS_ASSISTED,
    MDM_QUECTEL_GNSS_SPEED_OPTIMAL,
    MDM_QUECTEL_GNSS_MODE_MAX,
} mdm_quectel_gnss_modes_t;

typedef enum mdm_quectel_nmea_types
{
    MDM_QUECTEL_NMEA_GPS = 0,
    MDM_QUECTEL_NMEA_GLONASS,
    MDM_QUECTEL_NMEA_GALILEO,
    MDM_QUECTEL_NMEA_BEIDOU,
    MDM_QUECTEL_NMEA_GSVEXT,
} mdm_quectel_nmea_types_t;

typedef enum mdm_quectel_gnss_conf
{
    /* GLONASS off/BeiDou off/Galileo off */
    GN_OFF_BD_OFF_GL_OFF = 0,
    /* GLONASS on/BeiDou on/Galileo on */
    GN_ON_BD_ON_GL_ON,
    /* GLONASS on/BeiDou on/Galileo off */
    GN_ON_BD_ON_GL_OFF,
    /* GLONASS on/BeiDou off/Galileo on */
    GN_ON_BD_OFF_GL_ON,
    /* GLONASS on/BeiDou off/Galileo off */
    GN_ON_BD_OFF_GL_OFF,
    /* GLONASS off/BeiDou on/Galileo on */
    GN_OFF_BD_ON_GL_ON,
    /* GLONASS off/BeiDou off/Galileo on */
    GN_OFF_BD_OFF_GL_ON,
} mdm_quectel_gnss_conf_t;

typedef union mdm_quectel_nmea_type
{
    struct mdm_quectel_nmea_type_gps
    {
        bool gga;
        bool rmc;
        bool gsv;
        bool gsa;
        bool vtg;
    } gps;

    struct mdm_quectel_nmea_type_glonass
    {
        bool gsv;
        bool gsa;
        bool gns;
    } glonass;

    struct mdm_quectel_nmea_type_galileo
    {
        bool gsv;
    } galileo;

    struct mdm_quectel_nmea_type_beidou
    {
        bool gsa;
        bool gsv;
    } beidou;

    struct mdm_quectel_nmea_type_gsvext
    {
        bool enable;
    } gsvext;
} mdm_quectel_nmea_type_t;

#define MDM_GNSS_OP_NONE "none"
#define MDM_GNSS_OP_USBNMEA "usbnmea"
#define MDM_GNSS_OP_UARTDEBUG "uartdebug"

struct device;

/**
 * @brief Configure the outport of the GNSS NMEA sentences
 *
 * @param outport Outport of the NMEA sentences, can be either
 *                "none", "usbnmea" or "uartdebug".
 * @retval 0 on success, negative on failure.
 */
int mdm_quectel_gnss_cfg_outport(const struct device *dev, char* outport);

/**
 * @brief Selectively configures the NMEA sentences output
 *
 * @param gnss Type of the GNSS to configure,
 *             see mdm_quectel_nmea_types_t.
 * @param cfg The configuration, see mdm_quectel_nmea_type.
 * @retval 0 on success, negative on failure.
 */
int mdm_quectel_gnss_cfg_nmea(const struct device *dev,
			     mdm_quectel_nmea_types_t gnss,
			     mdm_quectel_nmea_type_t cfg);

/**
 * @brief Configures supported GNSS constellation
 *
 * @param cfg The configuration of the constellation,
 *            see mdm_quectel_gnss_conf_t.
 * @retval 0 on success, negative on failure.
 */
int mdm_quectel_gnss_cfg_gnss(const struct device *dev,
			      mdm_quectel_gnss_conf_t cfg);

/**
 * @brief Enables the GNSS, for more info please read the Quectel
 *        GNSS manual.
 *
 * @param fixmaxtime Maximum positioning time in second.
 * @param fixmaxdist Accuracy threshold of positioning in meter. [1 - 1000]
 * @param fixcount Number of attempts for positioning. 0 for continuous,
 *                 maximum 1000.
 * @param fixrate Interval time between positioning in second. [1 - 65535]
 *
 * @retval 0 on success, negative on failure.
 */
int mdm_quectel_gnss_enable(const struct device *dev,
			    uint8_t fixmaxtime, uint16_t fixmaxdist,
			    uint16_t fixcount, uint16_t fixrate);

// int mdm_quectel_get_network_time();

int32_t mdm_quectel_get_local_time(struct tm *tm, int32_t *offset);

/**
 * @brief Disables the GNSS
 *
 * @retval 0 on success, negative on failure.
 */
int mdm_quectel_gnss_disable(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_MODEM_QUECTEL_H_ */
// #endif /* CONFIG_MODEM_QUECTEL_GNSS */
