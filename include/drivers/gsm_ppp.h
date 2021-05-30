/*
 * Copyright (c) 2020 Endian Technologies AB
 * Copyright (c) 2021 G-Technologies Sdn. Bhd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GSM_PPP_H_
#define GSM_PPP_H_

#ifdef CONFIG_NEWLIB_LIBC
#include <time.h>
#endif

#define GSM_MODEM_DEVICE_NAME "modem_gsm"

#ifdef CONFIG_MODEM_GSM_QUECTEL_GNSS
typedef enum quectel_gnss_modes
{
    QUECTEL_GNSS_STANDALONE = 0,
    QUECTEL_GNSS_MS_BASED,
    QUECTEL_GNSS_MS_ASSISTED,
    QUECTEL_GNSS_SPEED_OPTIMAL,
    QUECTEL_GNSS_MODE_MAX,
} quectel_gnss_modes_t;

typedef enum quectel_nmea_types
{
    QUECTEL_NMEA_GPS = 0,
    QUECTEL_NMEA_GLONASS,
    QUECTEL_NMEA_GALILEO,
    QUECTEL_NMEA_BEIDOU,
    QUECTEL_NMEA_GSVEXT,
} quectel_nmea_types_t;

typedef enum quectel_gnss_conf
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
} quectel_gnss_conf_t;

typedef union quectel_nmea_type
{
    struct quectel_nmea_type_gps
    {
        bool gga;
        bool rmc;
        bool gsv;
        bool gsa;
        bool vtg;
    } gps;

    struct quectel_nmea_type_glonass
    {
        bool gsv;
        bool gsa;
        bool gns;
    } glonass;

    struct quectel_nmea_type_galileo
    {
        bool gsv;
    } galileo;

    struct quectel_nmea_type_beidou
    {
        bool gsa;
        bool gsv;
    } beidou;

    struct quectel_nmea_type_gsvext
    {
        bool enable;
    } gsvext;
} quectel_nmea_type_t;

/* GNSS output disabled */
#define QUECTEL_GNSS_OP_NONE "none"
/* GNSS output to USB port */
#define QUECTEL_GNSS_OP_USB "usbnmea"
/* GNSS output to UART debug port */
#define QUECTEL_GNSS_OP_UART "uartdebug"
#endif

/** @cond INTERNAL_HIDDEN */
struct device;
void gsm_ppp_start(const struct device *dev);
void gsm_ppp_stop(const struct device *dev);

#ifdef CONFIG_NEWLIB_LIBC
/**
 * @brief Get the local time from the modem's real time clock.
 *
 * @param[inout] tm time structure
 * @param[inout] offset The amount the local time is offset from GMT/UTC in seconds.
 *
 * @retval 0 if successful
 * @retval -EIO if RTC time invalid
 */
int32_t gsm_ppp_get_local_time(const struct device *dev,
			       struct tm *tm,
			       int32_t *offset);
#endif

#ifdef CONFIG_MODEM_GSM_QUECTEL_GNSS
/**
 * @brief Configure the outport of the GNSS
 *
 * @param[in] dev Pointer to the gsm_ppp device structure
 * @param[in] outport Outport of the NMEA sentences, can be either
 * QUECTEL_GNSS_OP_NONE, QUECTEL_GNSS_OP_USB or QUECTEL_GNSS_OP_UART.
 * @retval 0 on success, negative on failure.
 */
int quectel_gnss_cfg_outport(const struct device *dev, char* outport);

/**
 * @brief Selectively configures the NMEA sentences
 *
 * @param[in] dev Pointer to the gsm_ppp device structure
 * @param[in] gnss Type of the GNSS to configure, see quectel_nmea_types_t.
 * @param[in] cfg The configuration itself, see quectel_nmea_type_t.
 * @retval 0 on success, negative on failure.
 */
int quectel_gnss_cfg_nmea(const struct device *dev,
			  quectel_nmea_types_t gnss,
			  quectel_nmea_type_t cfg);

/**
 * @brief Configures supported GNSS constellation
 *
 * @param[in] dev Pointer to the gsm_ppp device structure
 * @param[in] cfg The configuration of the constellation, see
 * quectel_gnss_conf_t.
 * @retval 0 on success, negative on failure.
 */
int quectel_gnss_cfg_gnss(const struct device *dev,
			  quectel_gnss_conf_t cfg);

/**
 * @brief Enables the GNSS, for more info please read the Quectel GNSS manual.
 *
 * @param[in] dev Pointer to the gsm_ppp device structure
 * @param[in] fixmaxtime Maximum positioning time in second. [1 - 255 : 30]
 * @param[in] fixmaxdist Accuracy threshold of positioning in meter.
 * [1 - 1000 : 50]
 * @param[in] fixcount Number of attempts for positioning. 0 for continuous,
 * maximum 1000. [0 - 1000 : 0]
 * @param[in] fixrate Interval time between positioning in second.
 * [1 - 65535 : 1]
 *
 * @retval 0 on success, negative on failure.
 */
int quectel_gnss_enable(const struct device *dev, uint8_t fixmaxtime,
			uint16_t fixmaxdist, uint16_t fixcount,
			uint16_t fixrate);

/**
 * @brief Disables the GNSS.
 *
 * @param[in] dev Pointer to the gsm_ppp device structure
 *
 * @retval 0 on success, negative on failure.
 */
int quectel_gnss_disable(const struct device *dev);
#endif
/** @endcond */

#endif /* GSM_PPP_H_ */
