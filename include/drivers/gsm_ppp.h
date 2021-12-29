/*
 * Copyright (c) 2020 Endian Technologies AB
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GSM_PPP_H_
#define GSM_PPP_H_

#include <time.h>

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

/** @cond INTERNAL_HIDDEN */
struct device;
typedef void (*gsm_modem_gnss_cb)(void);
void gsm_ppp_start(const struct device *dev);
void gsm_ppp_stop(const struct device *dev);
int gsm_ppp_gnss_enable(void);
int gsm_ppp_gnss_disable(void);
int gsm_ppp_gnss_wait_until_ready(int s);
/** @endcond */

/**
 * @brief Get the local time from the modem's real time clock.
 *
 * @param dev Pointer to the GSM modem
 * @param[inout] tm time structure
 * @param[inout] offset The amount the local time is offset from GMT/UTC in seconds.
 *
 * @retval 0 if successful
 * @retval -EIO if RTC time invalid
 */
int32_t gsm_ppp_get_local_time(const struct device *dev, struct tm *tm, int32_t *offset);

/**
 * @brief Get the location from QuecLocator.
 *
 * @param dev Pointer to the GSM modem
 * @param coordinates Pointer to the struct qlbs_coordinates
 *
 * @retval 0 if successful
 * @retval errno.h values otherwise
 */
int gsm_ppp_get_qlbs(const struct device *dev, struct qlbs_coordinates *coordinates,
		     k_timeout_t timeout);

void gsm_ppp_register_gnss_callback(const struct device *dev, gsm_modem_gnss_cb cb);

#endif /* GSM_PPP_H_ */
