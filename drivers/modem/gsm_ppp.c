/*
 * Copyright (c) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zephyr_gsm_ppp

#include <logging/log.h>
LOG_MODULE_REGISTER(modem_gsm, CONFIG_MODEM_LOG_LEVEL);

#include <stdlib.h>
#include <kernel.h>
#include <device.h>
#include <sys/ring_buffer.h>
#include <sys/util.h>
#include <net/ppp.h>
#include <drivers/gsm_ppp.h>
#include <drivers/uart.h>

#include "gsm_ppp.h"
#include "modem_context.h"
#include "modem_iface_uart.h"
#include "modem_cmd_handler.h"

#include "drivers/modem/quectel.h"

#if IS_ENABLED(CONFIG_GSM_MUX)
#include <drivers/console/uart_mux.h>
#include "../console/gsm_mux.h"
#endif

#include <stdio.h>
#include <time.h>

#define GSM_UART_NODE                   DT_INST_BUS(0)
#define GSM_CMD_READ_BUF                128
#define GSM_CMD_AT_TIMEOUT              K_SECONDS(2)
#define GSM_CMD_SETUP_TIMEOUT           K_SECONDS(6)
#define GSM_RX_STACK_SIZE               CONFIG_MODEM_GSM_RX_STACK_SIZE
#define GSM_WORKQ_STACK_SIZE            2048
#define GSM_RECV_MAX_BUF                30
#define GSM_RECV_BUF_SIZE               128
#define GSM_ATTACH_RETRY_DELAY_MSEC     1000

#define GSM_RSSI_RETRY_DELAY_MSEC       2000
#define GSM_RSSI_RETRIES                10
#define GSM_RSSI_INVALID                -1000

#if IS_ENABLED(CONFIG_MODEM_GSM_ENABLE_CESQ_RSSI)
	#define GSM_RSSI_MAXVAL          0
#else
	#define GSM_RSSI_MAXVAL         -51
#endif

/* The ? can be a + or - */
static const char TIME_STRING_FORMAT[] = "\"yy/MM/dd,hh:mm:ss?zz\"";
#define TIME_STRING_DIGIT_STRLEN 2
#define TIME_STRING_SEPARATOR_STRLEN 1
#define TIME_STRING_PLUS_MINUS_INDEX (6 * 3)
#define TIME_STRING_FIRST_SEPARATOR_INDEX 0
#define TIME_STRING_FIRST_DIGIT_INDEX 1
#define TIME_STRING_TO_TM_STRUCT_YEAR_OFFSET (2000 - 1900)

/* Time structure min, max */
#define TM_YEAR_RANGE 0, 99
#define TM_MONTH_RANGE_PLUS_1 1, 12
#define TM_DAY_RANGE 1, 31
#define TM_HOUR_RANGE 0, 23
#define TM_MIN_RANGE 0, 59
#define TM_SEC_RANGE 0, 60 /* leap second */
#define QUARTER_HOUR_RANGE 0, 96
#define SECONDS_PER_QUARTER_HOUR (15 * 60)
#define SIZE_OF_NUL 1

static struct gsm_modem {
	struct modem_context context;

	struct modem_cmd_handler_data cmd_handler_data;
	uint8_t cmd_match_buf[GSM_CMD_READ_BUF];
	struct k_sem sem_response;

	struct modem_iface_uart_data gsm_data;
	struct k_work_delayable gsm_configure_work;
	char gsm_rx_rb_buf[PPP_MRU * 3];

	uint8_t *ppp_recv_buf;
	size_t ppp_recv_buf_len;

	enum gsm_ppp_state {
		GSM_PPP_START,
		GSM_PPP_PWR_SRC_OFF,
		GSM_PPP_PWR_SRC_ON,
		GSM_PPP_WAIT_AT,
		GSM_PPP_AT_RDY,
		GSM_PPP_MUX_ENABLED,
		GSM_PPP_STATE_INIT,
		GSM_PPP_STATE_CONTROL_CHANNEL = GSM_PPP_STATE_INIT,
		GSM_PPP_STATE_PPP_CHANNEL,
		GSM_PPP_STATE_AT_CHANNEL,
		GSM_PPP_STATE_DONE,
		GSM_PPP_SETUP = GSM_PPP_STATE_DONE,
		GSM_PPP_ATTACHING,
		GSM_PPP_ATTACHED,
		GSM_PPP_SETUP_DONE,
		GSM_PPP_STOP,
		GSM_PPP_STATE_ERROR,
	} state;

	gsm_modem_gnss_cb gnss_on_cb;

	const struct device *ppp_dev;
	const struct device *at_dev;
	const struct device *control_dev;

	bool local_time_valid : 1;
	int32_t local_time_offset;
	struct tm local_time;

	struct net_if *iface;

	int rssi_retries;
	int attach_retries;
} gsm;

NET_BUF_POOL_DEFINE(gsm_recv_pool, GSM_RECV_MAX_BUF, GSM_RECV_BUF_SIZE,
		    0, NULL);
K_KERNEL_STACK_DEFINE(gsm_rx_stack, GSM_RX_STACK_SIZE);
K_KERNEL_STACK_DEFINE(gsm_workq_stack, GSM_WORKQ_STACK_SIZE);

struct k_thread gsm_rx_thread;
static struct k_work_q gsm_workq;
static struct k_work_delayable rssi_work_handle;

static struct k_sem gnss_ready_sem;
static struct k_work_delayable gnss_configure_work;
static bool gnss_enabled;
enum ppp_gnss_state {
	PPP_GNSS_OFF,
	PPP_GNSS_STARTING,
	PPP_GNSS_CFG_OUTPORT,
	PPP_GNSS_CFG_CONSTELLATION,
	PPP_GNSS_CFG_NMEA,
	PPP_GNSS_CFG_SUPLVER,
	PPP_GNSS_CFG_PLANE,
	PPP_GNSS_CFG_SUPLURL,
	PPP_GNSS_CFG_TOKEN,
	PPP_GNSS_CFG_LATORDER,
	PPP_GNSS_CFG_TURN_ON,
	PPP_GNSS_READY,
};

static struct qlbs_coordinates gnss_coordinates;

static enum ppp_gnss_state gnss_state;

static int gsm_work_reschedule(struct k_work_delayable *dwork, k_timeout_t delay)
{
	return k_work_reschedule_for_queue(&gsm_workq, dwork, delay);
}

#if IS_ENABLED(CONFIG_MODEM_GSM_ENABLE_CESQ_RSSI)
	/* helper macro to keep readability */
#define ATOI(s_, value_, desc_) modem_atoi(s_, value_, desc_, __func__)

/**
 * @brief  Convert string to long integer, but handle errors
 *
 * @param  s: string with representation of integer number
 * @param  err_value: on error return this value instead
 * @param  desc: name the string being converted
 * @param  func: function where this is called (typically __func__)
 *
 * @retval return integer conversion on success, or err_value on error
 */
static int modem_atoi(const char *s, const int err_value,
				const char *desc, const char *func)
{
	int ret;
	char *endptr;

	ret = (int)strtol(s, &endptr, 10);
	if (!endptr || *endptr != '\0') {
		LOG_ERR("bad %s '%s' in %s", log_strdup(s),
			 log_strdup(desc), log_strdup(func));
		return err_value;
	}

	return ret;
}
#endif /* IS_ENABLED(CONFIG_MODEM_GSM_ENABLE_CESQ_RSSI) */

static void gsm_rx(struct gsm_modem *gsm)
{
	LOG_DBG("starting");

	while (true) {
		(void)k_sem_take(&gsm->gsm_data.rx_sem, K_FOREVER);

		/* The handler will listen AT channel */
		gsm->context.cmd_handler.process(&gsm->context.cmd_handler,
						 &gsm->context.iface);
	}
}

MODEM_CMD_DEFINE(gsm_cmd_ok)
{
	modem_cmd_handler_set_error(data, 0);
	LOG_DBG("ok");
	k_sem_give(&gsm.sem_response);
	return 0;
}

MODEM_CMD_DEFINE(gsm_cmd_error)
{
	modem_cmd_handler_set_error(data, -EINVAL);
	LOG_DBG("error");
	k_sem_give(&gsm.sem_response);
	return 0;
}

/* Handler: Modem initialization ready. */
MODEM_CMD_DEFINE(on_cmd_unsol_rdy)
{
	LOG_WRN("Modem %s", "AT RDY");

	return 0;
}

static const struct modem_cmd response_cmds[] = {
	MODEM_CMD("OK", gsm_cmd_ok, 0U, ""),
	MODEM_CMD("ERROR", gsm_cmd_error, 0U, ""),
	MODEM_CMD("CONNECT", gsm_cmd_ok, 0U, ""),
};

static const struct modem_cmd unsol_cmds[] = {
	MODEM_CMD("RDY", on_cmd_unsol_rdy, 0U, ""),
};

static int unquoted_atoi(const char *s, int base)
{
	if (*s == '"') {
		s++;
	}

	return strtol(s, NULL, base);
}

/*
 * Handler: +COPS: <mode>[0],<format>[1],<oper>[2]
 */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_cops)
{
	if (argc >= 1) {
#if defined(CONFIG_MODEM_CELL_INFO)
		if (argc >= 3) {
			gsm.context.data_operator = unquoted_atoi(argv[2], 10);
			LOG_INF("operator: %u",
				gsm.context.data_operator);
		}
#endif
		if (unquoted_atoi(argv[0], 10) == 0) {
			gsm.context.is_automatic_oper = true;
		} else {
			gsm.context.is_automatic_oper = false;
		}
	}

	return 0;
}

#if IS_ENABLED(CONFIG_MODEM_GSM_INFO)
#define MDM_MANUFACTURER_LENGTH  10
#define MDM_MODEL_LENGTH         16
#define MDM_REVISION_LENGTH      64
#define MDM_IMEI_LENGTH          16
#define MDM_IMSI_LENGTH          16
#define MDM_ICCID_LENGTH         32

struct modem_info {
	char mdm_manufacturer[MDM_MANUFACTURER_LENGTH];
	char mdm_model[MDM_MODEL_LENGTH];
	char mdm_revision[MDM_REVISION_LENGTH];
	char mdm_imei[MDM_IMEI_LENGTH];
#if IS_ENABLED(CONFIG_MODEM_SIM_NUMBERS)
	char mdm_imsi[MDM_IMSI_LENGTH];
	char mdm_iccid[MDM_ICCID_LENGTH];
#endif
};

static struct modem_info minfo;

/*
 * Provide modem info if modem shell is enabled. This can be shown with
 * "modem list" shell command.
 */

/* Handler: <manufacturer> */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_manufacturer)
{
	size_t out_len;

	out_len = net_buf_linearize(minfo.mdm_manufacturer,
				    sizeof(minfo.mdm_manufacturer) - 1,
				    data->rx_buf, 0, len);
	minfo.mdm_manufacturer[out_len] = '\0';
	LOG_INF("Manufacturer: %s", log_strdup(minfo.mdm_manufacturer));

	return 0;
}

/* Handler: <model> */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_model)
{
	size_t out_len;

	out_len = net_buf_linearize(minfo.mdm_model,
				    sizeof(minfo.mdm_model) - 1,
				    data->rx_buf, 0, len);
	minfo.mdm_model[out_len] = '\0';
	LOG_INF("Model: %s", log_strdup(minfo.mdm_model));

	return 0;
}

/* Handler: <rev> */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_revision)
{
	size_t out_len;

	out_len = net_buf_linearize(minfo.mdm_revision,
				    sizeof(minfo.mdm_revision) - 1,
				    data->rx_buf, 0, len);
	minfo.mdm_revision[out_len] = '\0';
	LOG_INF("Revision: %s", log_strdup(minfo.mdm_revision));

	return 0;
}

/* Handler: <IMEI> */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_imei)
{
	size_t out_len;

	out_len = net_buf_linearize(minfo.mdm_imei, sizeof(minfo.mdm_imei) - 1,
				    data->rx_buf, 0, len);
	minfo.mdm_imei[out_len] = '\0';
	LOG_INF("IMEI: %s", log_strdup(minfo.mdm_imei));

	return 0;
}

#if IS_ENABLED(CONFIG_MODEM_SIM_NUMBERS)
/* Handler: <IMSI> */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_imsi)
{
	size_t out_len;

	out_len = net_buf_linearize(minfo.mdm_imsi, sizeof(minfo.mdm_imsi) - 1,
				    data->rx_buf, 0, len);
	minfo.mdm_imsi[out_len] = '\0';
	LOG_INF("IMSI: %s", log_strdup(minfo.mdm_imsi));

	return 0;
}

/* Handler: <ICCID> */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_iccid)
{
	size_t out_len;

	out_len = net_buf_linearize(minfo.mdm_iccid, sizeof(minfo.mdm_iccid) - 1,
				    data->rx_buf, 0, len);
	minfo.mdm_iccid[out_len] = '\0';
	if (minfo.mdm_iccid[0] == '+') {
		/* Seen on U-blox SARA: "+CCID: nnnnnnnnnnnnnnnnnnnn".
		 * Skip over the +CCID bit, which other modems omit.
		 */
		char *p = strchr(minfo.mdm_iccid, ' ');

		if (p) {
			size_t len = strlen(p+1);

			memmove(minfo.mdm_iccid, p+1, len+1);
		}
	}
	LOG_INF("ICCID: %s", log_strdup(minfo.mdm_iccid));

	return 0;
}
#endif /* CONFIG_MODEM_SIM_NUMBERS */

#if IS_ENABLED(CONFIG_MODEM_CELL_INFO)
/*
 * Handler: +CEREG: <n>[0],<stat>[1],<tac>[2],<ci>[3],<AcT>[4]
 */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_cereg)
{
	if (argc >= 4) {
		gsm.context.data_lac = unquoted_atoi(argv[2], 16);
		gsm.context.data_cellid = unquoted_atoi(argv[3], 16);
		LOG_INF("lac: %u, cellid: %u",
			gsm.context.data_lac,
			gsm.context.data_cellid);
	}

	return 0;
}

static const struct setup_cmd query_cellinfo_cmds[] = {
	SETUP_CMD_NOHANDLE("AT+CEREG=2"),
	SETUP_CMD("AT+CEREG?", "", on_cmd_atcmdinfo_cereg, 5U, ","),
	SETUP_CMD_NOHANDLE("AT+COPS=3,2"),
	SETUP_CMD("AT+COPS?", "", on_cmd_atcmdinfo_cops, 3U, ","),
};

static int gsm_query_cellinfo(struct gsm_modem *gsm)
{
	int ret;

	ret = modem_cmd_handler_setup_cmds_nolock(&gsm->context.iface,
						  &gsm->context.cmd_handler,
						  query_cellinfo_cmds,
						  ARRAY_SIZE(query_cellinfo_cmds),
						  &gsm->sem_response,
						  GSM_CMD_SETUP_TIMEOUT);
	if (ret < 0) {
		LOG_WRN("modem query for cell info returned %d", ret);
	}

	return ret;
}
#endif /* CONFIG_MODEM_CELL_INFO */
#endif /* CONFIG_MODEM_GSM_INFO */

#if IS_ENABLED(CONFIG_MODEM_GSM_ENABLE_CESQ_RSSI)
/*
 * Handler: +CESQ: <rxlev>[0],<ber>[1],<rscp>[2],<ecn0>[3],<rsrq>[4],<rsrp>[5]
 */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_rssi_cesq)
{
	int rsrp, rscp, rxlev;

	rsrp = ATOI(argv[5], 0, "rsrp");
	rscp = ATOI(argv[2], 0, "rscp");
	rxlev = ATOI(argv[0], 0, "rxlev");

	if (rsrp >= 0 && rsrp <= 97) {
		gsm.context.data_rssi = -140 + (rsrp - 1);
		LOG_INF("RSRP: %d", gsm.context.data_rssi);
	} else if (rscp >= 0 && rscp <= 96) {
		gsm.context.data_rssi = -120 + (rscp - 1);
		LOG_INF("RSCP: %d", gsm.context.data_rssi);
	} else if (rxlev >= 0 && rxlev <= 63) {
		gsm.context.data_rssi = -110 + (rxlev - 1);
		LOG_INF("RSSI: %d", gsm.context.data_rssi);
	} else {
		gsm.context.data_rssi = GSM_RSSI_INVALID;
		LOG_INF("RSRP/RSCP/RSSI not known");
	}

	return 0;
}
#else
/* Handler: +CSQ: <signal_power>[0],<qual>[1] */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_rssi_csq)
{
	/* Expected response is "+CSQ: <signal_power>,<qual>" */
	if (argc) {
		int rssi = atoi(argv[0]);

		if (rssi >= 0 && rssi <= 31) {
			rssi = -113 + (rssi * 2);
		} else {
			rssi = GSM_RSSI_INVALID;
		}

		gsm.context.data_rssi = rssi;
		LOG_INF("RSSI: %d", rssi);
	}

	k_sem_give(&gsm.sem_response);

	return 0;
}
#endif /* CONFIG_MODEM_GSM_ENABLE_CESQ_RSSI */

/* Handler: +QLBS: <?>[0],<lat>[1],<lon>[2] */
MODEM_CMD_DEFINE(on_cmd_gnss_qlbs)
{
	int lat, lon;

	gnss_coordinates.error = atoi(argv[0]);
	gnss_coordinates.latitude = strtod(argv[1], NULL);
	gnss_coordinates.longitude = strtod(argv[2], NULL);

	lat = gnss_coordinates.latitude * 1000000;
	lon = gnss_coordinates.longitude * 1000000;

	LOG_WRN("[%d] %d.%d, %d.%d", gnss_coordinates.error, lat / 1000000, lat % 1000000,
		lon / 1000000, lon % 1000000);

	return 0;
}

#if IS_ENABLED(CONFIG_MODEM_GSM_ENABLE_CESQ_RSSI)
static const struct modem_cmd read_rssi_cmd =
	MODEM_CMD("+CESQ:", on_cmd_atcmdinfo_rssi_cesq, 6U, ",");
#else
static const struct modem_cmd read_rssi_cmd =
	MODEM_CMD("+CSQ:", on_cmd_atcmdinfo_rssi_csq, 2U, ",");
#endif /* CONFIG_MODEM_GSM_ENABLE_CESQ_RSSI */

static const struct setup_cmd setup_cmds[] = {
	/* no echo */
	SETUP_CMD_NOHANDLE("ATE0"),
	/* hang up */
	SETUP_CMD_NOHANDLE("ATH"),
	/* extender errors in numeric form */
	SETUP_CMD_NOHANDLE("AT+CMEE=1"),

#if IS_ENABLED(CONFIG_MODEM_GSM_INFO)
	/* query modem info */
	SETUP_CMD("AT+CGMI", "", on_cmd_atcmdinfo_manufacturer, 0U, ""),
	SETUP_CMD("AT+CGMM", "", on_cmd_atcmdinfo_model, 0U, ""),
	SETUP_CMD("AT+CGMR", "", on_cmd_atcmdinfo_revision, 0U, ""),
# if IS_ENABLED(CONFIG_MODEM_SIM_NUMBERS)
	SETUP_CMD("AT+CIMI", "", on_cmd_atcmdinfo_imsi, 0U, ""),
	SETUP_CMD("AT+CCID", "", on_cmd_atcmdinfo_iccid, 0U, ""),
# endif
	SETUP_CMD("AT+CGSN", "", on_cmd_atcmdinfo_imei, 0U, ""),
#endif /* CONFIG_MODEM_GSM_INFO */

	/* disable unsolicited network registration codes */
	SETUP_CMD_NOHANDLE("AT+CREG=0"),

	/* disable xtradata */
	SETUP_CMD_NOHANDLE("AT+QGPSXTRA=0"),

	/* create PDP context */
	SETUP_CMD_NOHANDLE("AT+CGDCONT=1,\"IP\",\"" CONFIG_MODEM_GSM_APN "\""),
};

static bool valid_time_string(const char *time_string)
{
	size_t offset, i;

	/* Ensure that all the expected delimiters are present */
	offset = TIME_STRING_DIGIT_STRLEN + TIME_STRING_SEPARATOR_STRLEN;
	i = TIME_STRING_FIRST_SEPARATOR_INDEX;

	for (; i < TIME_STRING_PLUS_MINUS_INDEX; i += offset) {
		if (time_string[i] != TIME_STRING_FORMAT[i]) {
			return false;
		}
	}
	/* The last character is the offset from UTC and can be either
	 * positive or negative.  The last " is also handled here.
	 */
	if ((time_string[i] == '+' || time_string[i] == '-') && (time_string[i + offset] == '"')) {
		return true;
	}
	return false;
}

static int get_next_time_string_digit(int *failure_cnt, char **pp, int min, int max)
{
	char digits[TIME_STRING_DIGIT_STRLEN + SIZE_OF_NUL];
	int result;

	memset(digits, 0, sizeof(digits));
	memcpy(digits, *pp, TIME_STRING_DIGIT_STRLEN);
	*pp += TIME_STRING_DIGIT_STRLEN + TIME_STRING_SEPARATOR_STRLEN;
	result = strtol(digits, NULL, 10);
	if (result > max) {
		*failure_cnt += 1;
		return max;
	} else if (result < min) {
		*failure_cnt += 1;
		return min;
	} else {
		return result;
	}
}

static bool convert_time_string_to_struct(struct tm *tm, int32_t *offset, char *time_string)
{
	int fc = 0;
	char *ptr = time_string;

	if (!valid_time_string(ptr)) {
		LOG_INF("Invalid timestring");
		return false;
	}
	ptr = &ptr[TIME_STRING_FIRST_DIGIT_INDEX];
	tm->tm_year = TIME_STRING_TO_TM_STRUCT_YEAR_OFFSET +
		      get_next_time_string_digit(&fc, &ptr, TM_YEAR_RANGE);
	tm->tm_mon = get_next_time_string_digit(&fc, &ptr, TM_MONTH_RANGE_PLUS_1) - 1;
	tm->tm_mday = get_next_time_string_digit(&fc, &ptr, TM_DAY_RANGE);
	tm->tm_hour = get_next_time_string_digit(&fc, &ptr, TM_HOUR_RANGE);
	tm->tm_min = get_next_time_string_digit(&fc, &ptr, TM_MIN_RANGE);
	tm->tm_sec = get_next_time_string_digit(&fc, &ptr, TM_SEC_RANGE);
	tm->tm_isdst = 0;
	*offset = (int32_t)get_next_time_string_digit(&fc, &ptr, QUARTER_HOUR_RANGE) *
		  SECONDS_PER_QUARTER_HOUR;
	if (time_string[TIME_STRING_PLUS_MINUS_INDEX] == '-') {
		*offset *= -1;
	}

	return (fc == 0);
}

MODEM_CMD_DEFINE(on_cmd_rtc_query)
{
	size_t str_len = sizeof(TIME_STRING_FORMAT) - 1;
	char rtc_string[sizeof(TIME_STRING_FORMAT)];

	memset(rtc_string, 0, sizeof(rtc_string));
	gsm.local_time_valid = false;

	if (len != str_len) {
		LOG_WRN("Unexpected length for RTC string %d (expected:%d)",
			len, str_len);
	} else {
		net_buf_linearize(rtc_string, str_len, data->rx_buf, 0, str_len);
		LOG_INF("RTC string: '%s'", log_strdup(rtc_string));
		gsm.local_time_valid = convert_time_string_to_struct(
			&gsm.local_time, &gsm.local_time_offset, rtc_string);
	}

	return true;
}

int32_t gsm_ppp_get_local_time(const struct device *dev, struct tm *tm, int32_t *offset)
{
	int ret;

	struct modem_cmd cmd  = MODEM_CMD("+CCLK: ", on_cmd_rtc_query, 0U, "");
	struct gsm_modem *gsm = dev->data;
	gsm->local_time_valid = false;

	ret = modem_cmd_send(&gsm->context.iface, &gsm->context.cmd_handler, &cmd, 1U, "AT+CCLK?",
			     &gsm->sem_response, GSM_CMD_AT_TIMEOUT);

	if (gsm->local_time_valid) {
		memcpy(tm, &gsm->local_time, sizeof(struct tm));
		memcpy(offset, &gsm->local_time_offset, sizeof(*offset));
	} else {
		ret = -EIO;
	}
	return ret;
}

int gsm_ppp_get_qlbs(const struct device *dev, struct qlbs_coordinates *coordinates,
		     k_timeout_t timeout)
{
	int ret;
	struct modem_cmd cmd  = MODEM_CMD("+QLBS: ", on_cmd_gnss_qlbs, 3U, ",");
	struct gsm_modem *gsm = dev->data;

	if (!IS_ENABLED(CONFIG_MODEM_GSM_QUECTELL_GNSS_QLOC)) {
		return -EACCES;
	}

	if (gnss_state != PPP_GNSS_READY)
	{
		LOG_WRN("GNSS not ready");
		return -EIO;
	}

	if (gsm->state != GSM_PPP_ATTACHED && gsm->state != GSM_PPP_SETUP_DONE)
	{
		LOG_WRN("Not attached");
		return -EIO;
	}

	ret = modem_cmd_send(&gsm->context.iface, &gsm->context.cmd_handler, &cmd, 1U, "AT+QLBS",
			     &gsm->sem_response, timeout);

	if (!ret)
	{
		(void)memcpy(coordinates, &gnss_coordinates, sizeof(struct qlbs_coordinates));
	}

	return ret;
}

MODEM_CMD_DEFINE(on_cmd_atcmdinfo_attached)
{
	int error = -EAGAIN;

	/* Expected response is "+CGATT: 0|1" so simply look for '1' */
	if (argc && atoi(argv[0]) == 1) {
		error = 0;
		LOG_INF("Attached to packet service!");
	}

	modem_cmd_handler_set_error(data, error);
	// k_sem_give(&gsm.sem_response);

	return 0;
}


static const struct modem_cmd read_cops_cmd =
	MODEM_CMD_ARGS_MAX("+COPS:", on_cmd_atcmdinfo_cops, 1U, 4U, ",");

static const struct modem_cmd check_attached_cmd =
	MODEM_CMD("+CGATT:", on_cmd_atcmdinfo_attached, 1U, ",");

static const struct setup_cmd connect_cmds[] = {
	/* connect to network */
	SETUP_CMD_NOHANDLE("ATD*99#"),
};

static int gsm_setup_mccmno(struct gsm_modem *gsm)
{
	int ret = 0;

	if (CONFIG_MODEM_GSM_MANUAL_MCCMNO[0]) {
		/* use manual MCC/MNO entry */
		ret = modem_cmd_send_nolock(&gsm->context.iface,
					    &gsm->context.cmd_handler,
					    NULL, 0,
					    "AT+COPS=1,2,\""
					    CONFIG_MODEM_GSM_MANUAL_MCCMNO
					    "\"",
					    &gsm->sem_response,
					    GSM_CMD_AT_TIMEOUT);
	} else {

/* First AT+COPS? is sent to check if automatic selection for operator
 * is already enabled, if yes we do not send the command AT+COPS= 0,0.
 */

		ret = modem_cmd_send_nolock(&gsm->context.iface,
					    &gsm->context.cmd_handler,
					    &read_cops_cmd,
					    1, "AT+COPS?",
					    &gsm->sem_response,
					    GSM_CMD_SETUP_TIMEOUT);

		if (ret < 0) {
			return ret;
		}

		if (!gsm->context.is_automatic_oper) {
			/* register operator automatically */
			ret = modem_cmd_send_nolock(&gsm->context.iface,
						    &gsm->context.cmd_handler,
						    NULL, 0, "AT+COPS=0,0",
						    &gsm->sem_response,
						    GSM_CMD_AT_TIMEOUT);
		}
	}

	if (ret < 0) {
		LOG_ERR("AT+COPS ret:%d", ret);
	}

	return ret;
}

static struct net_if *ppp_net_if(void)
{
	return net_if_get_first_by_type(&NET_L2_GET_NAME(PPP));
}

static void set_ppp_carrier_on(struct gsm_modem *gsm)
{
	static const struct ppp_api *api;
	const struct device *ppp_dev = device_get_binding(CONFIG_NET_PPP_DRV_NAME);
	struct net_if *iface = gsm->iface;
	int ret;

	if (!ppp_dev) {
		LOG_ERR("Cannot find PPP %s!", CONFIG_NET_PPP_DRV_NAME);
		return;
	}

	if (!api) {
		api = (const struct ppp_api *)ppp_dev->api;

		/* For the first call, we want to call ppp_start()... */
		ret = api->start(ppp_dev);
		if (ret) {
			LOG_ERR("ppp start returned %d", ret);
		}
	} else {
		/* ...but subsequent calls should be to ppp_enable() */
		ret = net_if_l2(iface)->enable(iface, true);
		if (ret) {
			LOG_ERR("ppp l2 enable returned %d", ret);
		}
	}
}

static void rssi_handler(struct k_work *work)
{
	int ret;
#if IS_ENABLED(CONFIG_MODEM_GSM_ENABLE_CESQ_RSSI)
	ret = modem_cmd_send_nolock(&gsm.context.iface, &gsm.context.cmd_handler,
		&read_rssi_cmd, 1, "AT+CESQ", &gsm.sem_response, GSM_CMD_SETUP_TIMEOUT);
#else
	ret = modem_cmd_send_nolock(&gsm.context.iface, &gsm.context.cmd_handler,
		&read_rssi_cmd, 1, "AT+CSQ", &gsm.sem_response, GSM_CMD_SETUP_TIMEOUT);
#endif /* CONFIG_MODEM_GSM_ENABLE_CESQ_RSSI */

	if (ret < 0) {
		LOG_DBG("No answer to RSSI readout, %s", "ignoring...");
	}

#if IS_ENABLED(CONFIG_GSM_MUX)
#if IS_ENABLED(CONFIG_MODEM_CELL_INFO)
	(void) gsm_query_cellinfo(&gsm);
#endif
	gsm_work_reschedule(&rssi_work_handle, K_SECONDS(CONFIG_MODEM_GSM_RSSI_POLLING_PERIOD));
#endif /* CONFIG_GSM_MUX */

}

static void gsm_finalize_connection(struct gsm_modem *gsm)
{
	int ret = 0;

	/* If already attached, jump right to RSSI readout */
	if (gsm->state == GSM_PPP_ATTACHED) {
		goto attached;
	}

	/* If attach check failed, we should not redo every setup step */
	if (gsm->state == GSM_PPP_ATTACHING) {
		goto attaching;
	}

	gsm->state = GSM_PPP_SETUP;

#if IS_ENABLED(CONFIG_GSM_MUX)
	ret = modem_cmd_send_nolock(&gsm->context.iface,
				    &gsm->context.cmd_handler,
				    &response_cmds[0],
				    ARRAY_SIZE(response_cmds),
				    "AT", &gsm->sem_response,
				    GSM_CMD_AT_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("AT returned %d, %s",
			ret, "retrying...");
		(void)gsm_work_reschedule(&gsm->gsm_configure_work,
					K_SECONDS(1));
		return;
	}
#endif

#if IS_ENABLED(CONFIG_MODEM_GSM_FACTORY_RESET_AT_BOOT)
	(void)modem_cmd_send_nolock(&gsm->context.iface,
				    &gsm->context.cmd_handler,
				    &response_cmds[0],
				    ARRAY_SIZE(response_cmds),
				    "AT&F", &gsm->sem_response,
				    GSM_CMD_AT_TIMEOUT);
	k_sleep(K_SECONDS(1));
#endif

	ret = gsm_setup_mccmno(gsm);

	if (ret < 0) {
		LOG_ERR("gsm_setup_mccmno returned %d, %s",
				ret, "retrying...");

		(void)gsm_work_reschedule(&gsm->gsm_configure_work,
							K_SECONDS(1));
		return;
	}

	ret = modem_cmd_handler_setup_cmds_nolock(&gsm->context.iface,
						  &gsm->context.cmd_handler,
						  setup_cmds,
						  ARRAY_SIZE(setup_cmds),
						  &gsm->sem_response,
						  GSM_CMD_SETUP_TIMEOUT);
	if (ret < 0) {
		LOG_DBG("setup_cmds returned %d, %s",
			ret, "retrying...");
		(void)gsm_work_reschedule(&gsm->gsm_configure_work, K_SECONDS(1));
		return;
	}

attaching:
	gsm->state = GSM_PPP_ATTACHING;
	/* Don't initialize PPP until we're attached to packet service */
	ret = modem_cmd_send_nolock(&gsm->context.iface,
				    &gsm->context.cmd_handler,
				    &check_attached_cmd, 1,
				    "AT+CGATT?",
				    &gsm->sem_response,
				    GSM_CMD_SETUP_TIMEOUT);
	if (ret < 0) {
		/*
		 * attach_retries not set        -> trigger N attach retries
		 * attach_retries set            -> decrement and retry
		 * attach_retries set, becomes 0 -> trigger full retry
		 */
		if (!gsm->attach_retries) {
			gsm->attach_retries = CONFIG_MODEM_GSM_ATTACH_TIMEOUT *
				MSEC_PER_SEC / GSM_ATTACH_RETRY_DELAY_MSEC;
		} else {
			gsm->attach_retries--;
		}

		LOG_DBG("Not attached, %s", "retrying...");

		(void)gsm_work_reschedule(&gsm->gsm_configure_work,
					K_MSEC(GSM_ATTACH_RETRY_DELAY_MSEC));
		return;
	}

	/* Attached, clear retry counter */
	gsm->state = GSM_PPP_ATTACHED;
	gsm->attach_retries = 0;

	LOG_DBG("modem attach returned %d, %s", ret, "read RSSI");
	gsm->rssi_retries = GSM_RSSI_RETRIES;

 attached:

#if !IS_ENABLED(CONFIG_GSM_MUX)
	/* Read connection quality (RSSI) before PPP carrier is ON */
	rssi_handler(NULL);

	if (!(gsm->context.data_rssi && gsm->context.data_rssi != GSM_RSSI_INVALID &&
		gsm->context.data_rssi < GSM_RSSI_MAXVAL)) {

		LOG_DBG("Not valid RSSI, %s", "retrying...");
		if (gsm->rssi_retries-- > 0) {
			(void)gsm_work_reschedule(&gsm->gsm_configure_work,
						K_MSEC(GSM_RSSI_RETRY_DELAY_MSEC));
			return;
		}
	}
#if IS_ENABLED(CONFIG_MODEM_CELL_INFO)
	(void) gsm_query_cellinfo(gsm);
#endif
#endif /* !IS_ENABLED(CONFIG_GSM_MUX) */

	LOG_DBG("modem setup returned %d, %s", ret, "enable PPP");

	ret = modem_cmd_handler_setup_cmds_nolock(&gsm->context.iface,
						  &gsm->context.cmd_handler,
						  connect_cmds,
						  ARRAY_SIZE(connect_cmds),
						  &gsm->sem_response,
						  GSM_CMD_SETUP_TIMEOUT);
	if (ret < 0) {
		LOG_DBG("modem setup returned %d, %s",
			ret, "retrying...");
		(void)gsm_work_reschedule(&gsm->gsm_configure_work, K_SECONDS(1));
		return;
	}

	gsm->state = GSM_PPP_SETUP_DONE;
	set_ppp_carrier_on(gsm);

#if IS_ENABLED(CONFIG_GSM_MUX)
	/* Re-use the original iface for AT channel */
	ret = modem_iface_uart_init_dev(&gsm->context.iface,
					gsm->at_dev);
	if (ret < 0) {
		LOG_DBG("iface %suart error %d", "AT ", ret);
	} else {
		/* Do a test and try to send AT command to modem */
		ret = modem_cmd_send_nolock(&gsm->context.iface,
					    &gsm->context.cmd_handler,
					    &response_cmds[0],
					    ARRAY_SIZE(response_cmds),
					    "AT", &gsm->sem_response,
					    GSM_CMD_AT_TIMEOUT);
		if (ret < 0) {
			LOG_WRN("modem setup returned %d, %s",
				ret, "AT cmds failed");
		} else {
			LOG_INF("AT channel %d connected to %s",
				DLCI_AT, gsm->at_dev->name);
		}
	}
	modem_cmd_handler_tx_unlock(&gsm->context.cmd_handler);
	k_work_schedule(&rssi_work_handle, K_SECONDS(CONFIG_MODEM_GSM_RSSI_POLLING_PERIOD));
#endif /* CONFIG_GSM_MUX */
}

#if IS_ENABLED(CONFIG_GSM_MUX)
static int mux_enable(struct gsm_modem *gsm)
{
	int ret = 0;

	/* Turn on muxing */
#if IS_ENABLED(CONFIG_MODEM_GSM_SIMCOM)
	ret = modem_cmd_send_nolock(&gsm->context.iface,
				    &gsm->context.cmd_handler,
				    &response_cmds[0],
				    ARRAY_SIZE(response_cmds),
#if defined(SIMCOM_LTE)
				    /* FIXME */
				    /* Some SIMCOM modems can set the channels */
				    /* Control channel always at DLCI 0 */
				    "AT+CMUXSRVPORT=0,0;"
				    /* PPP should be at DLCI 1 */
				    "+CMUXSRVPORT=" STRINGIFY(DLCI_PPP) ",1;"
				    /* AT should be at DLCI 2 */
				    "+CMUXSRVPORT=" STRINGIFY(DLCI_AT) ",1;"
#else
				    "AT"
#endif /* SIMCOM_LTE */
				    "+CMUX=0,0,5,"
				    STRINGIFY(CONFIG_GSM_MUX_MRU_DEFAULT_LEN),
				    &gsm->sem_response,
				    GSM_CMD_AT_TIMEOUT);
#elif IS_ENABLED(CONFIG_MODEM_GSM_QUECTEL)
	/* Quectel GSM modem */
	ret = modem_cmd_send_nolock(&gsm->context.iface,
				    &gsm->context.cmd_handler,
				    &response_cmds[0],
				    ARRAY_SIZE(response_cmds),
				    "AT+CMUX=0,0,5,"
				    STRINGIFY(CONFIG_GSM_MUX_MRU_DEFAULT_LEN),
				    &gsm->sem_response,
				    GSM_CMD_AT_TIMEOUT);

	/* Quectel requires some time to initialize the CMUX */
	k_sleep(K_SECONDS(1));
#else /* Generic modem */
	/* Generic GSM modem */
	ret = modem_cmd_send_nolock(&gsm->context.iface,
				    &gsm->context.cmd_handler,
				    &response_cmds[0],
				    ARRAY_SIZE(response_cmds),
				    "AT+CMUX=0", &gsm->sem_response,
				    GSM_CMD_AT_TIMEOUT);
#endif

	if (ret < 0) {
		LOG_ERR("AT+CMUX ret:%d", ret);
	}

	return ret;
}

static void mux_setup_next(struct gsm_modem *gsm)
{
	(void)gsm_work_reschedule(&gsm->gsm_configure_work, K_MSEC(1));
}

static void mux_attach_cb(const struct device *mux, int dlci_address,
			  bool connected, void *user_data)
{
	LOG_DBG("DLCI %d to %s %s", dlci_address, mux->name,
		connected ? "connected" : "disconnected");

	if (connected) {
		uart_irq_rx_enable(mux);
		uart_irq_tx_enable(mux);
	}

	mux_setup_next(user_data);
}

static int mux_attach(const struct device *mux, const struct device *uart,
		      int dlci_address, void *user_data)
{
	int ret = uart_mux_attach(mux, uart, dlci_address, mux_attach_cb,
				  user_data);
	if (ret < 0) {
		LOG_ERR("Cannot attach DLCI %d (%s) to %s (%d)", dlci_address,
			mux->name, uart->name, ret);
		return ret;
	}

	return 0;
}

static void mux_setup(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct gsm_modem *gsm = CONTAINER_OF(dwork, struct gsm_modem,
					     gsm_configure_work);
	const struct device *uart = DEVICE_DT_GET(GSM_UART_NODE);
	int ret;

	/* We need to call this to reactivate mux ISR. Note: This is only called
	 * after re-initing gsm_ppp.
	 */
	if (gsm->ppp_dev && gsm->state == GSM_PPP_STATE_CONTROL_CHANNEL) {
		uart_mux_enable(gsm->ppp_dev);
	}

	switch (gsm->state) {
	case GSM_PPP_STATE_CONTROL_CHANNEL:
		/* Get UART device. There is one dev / DLCI */
		if (gsm->control_dev == NULL) {
			gsm->control_dev = uart_mux_alloc();
			if (gsm->control_dev == NULL) {
				LOG_DBG("Cannot get UART mux for %s channel",
					"control");
				goto fail;
			}
		}

		gsm->state = GSM_PPP_STATE_PPP_CHANNEL;

		ret = mux_attach(gsm->control_dev, uart, DLCI_CONTROL, gsm);
		if (ret < 0) {
			goto fail;
		}

		break;

	case GSM_PPP_STATE_PPP_CHANNEL:
		if (gsm->ppp_dev == NULL) {
			gsm->ppp_dev = uart_mux_alloc();
			if (gsm->ppp_dev == NULL) {
				LOG_DBG("Cannot get UART mux for %s channel",
					"PPP");
				goto fail;
			}
		}

		gsm->state = GSM_PPP_STATE_AT_CHANNEL;

		ret = mux_attach(gsm->ppp_dev, uart, DLCI_PPP, gsm);
		if (ret < 0) {
			goto fail;
		}

		break;

	case GSM_PPP_STATE_AT_CHANNEL:
		if (gsm->at_dev == NULL) {
			gsm->at_dev = uart_mux_alloc();
			if (gsm->at_dev == NULL) {
				LOG_DBG("Cannot get UART mux for %s channel",
					"AT");
				goto fail;
			}
		}

		gsm->state = GSM_PPP_STATE_DONE;

		ret = mux_attach(gsm->at_dev, uart, DLCI_AT, gsm);
		if (ret < 0) {
			goto fail;
		}

		break;

	case GSM_PPP_STATE_DONE:
	case GSM_PPP_ATTACHING:
	case GSM_PPP_ATTACHED:
		/* At least the SIMCOM modem expects that the Internet
		 * connection is created in PPP channel. We will need
		 * to attach the AT channel to context iface after the
		 * PPP connection is established in order to give AT commands
		 * to the modem.
		 */
		ret = modem_iface_uart_init_dev(&gsm->context.iface,
						gsm->ppp_dev);
		if (ret < 0) {
			LOG_DBG("iface %suart error %d", "PPP ", ret);
			goto fail;
		}

		LOG_INF("PPP channel %d connected to %s",
			DLCI_PPP, gsm->ppp_dev->name);

		gsm_finalize_connection(gsm);
		break;
	default:
		LOG_ERR("mux_setup while in state: %d", gsm->state);
		goto fail;
	}

	return;

fail:
	gsm->state = GSM_PPP_STATE_ERROR;
}
#endif /* CONFIG_GSM_MUX */

#if IS_ENABLED(CONFIG_MODEM_GSM_QUECTEL_GNSS)
/**
 * @brief Configure the outport of the GNSS
 *
 * @param[in] outport Outport of the NMEA sentences, can be either
 * QUECTEL_GNSS_OP_NONE, QUECTEL_GNSS_OP_USB or QUECTEL_GNSS_OP_UART.
 * @retval 0 on success, negative on failure.
 */
static int quectel_gnss_cfg_outport(const char* outport)
{
	int  ret;
	char buf[sizeof("AT+QGPSCFG=\"outport\",\"#########\"")] = {0};

	snprintk(buf, sizeof(buf), "AT+QGPSCFG=\"outport\",\"%s\"", outport);

	ret = modem_cmd_send(&gsm.context.iface, &gsm.context.cmd_handler,
			NULL, 0U, buf, &gsm.sem_response,
			GSM_CMD_AT_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("%s ret:%d", log_strdup(buf), ret);
		return ret;
	}

	LOG_INF("Configured GNSS outport to %s", outport);

	return ret;
}

/**
 * @brief Selectively configures the NMEA sentences
 *
 * @param[in] gnss Type of the GNSS to configure, see quectel_nmea_types_t.
 * @param[in] cfg The configuration itself, see quectel_nmea_type_t.
 * @retval 0 on success, negative on failure.
 */
static int quectel_gnss_cfg_nmea(const quectel_nmea_types_t gnss,
				 const char *gnss_str,
				 const quectel_nmea_type_t *cfg)
{
	int  ret;
	uint8_t val;
	char buf[sizeof("AT+QGPSCFG=\"glonassnmeatype\",##")] = {0};

	switch(gnss)
	{
		case QUECTEL_NMEA_GPS:
			val = (cfg->gps.vtg << 4 | cfg->gps.gsa << 3 |
				cfg->gps.gsv << 2 | cfg->gps.rmc << 1 |
				cfg->gps.gga);

			LOG_INF("Configuring GPS NMEA: %X", val);

			break;
		case QUECTEL_NMEA_GLONASS:
			val = (cfg->glonass.gns << 2 | cfg->glonass.gsa << 1 |
				cfg->glonass.gsv);

			LOG_INF("Configuring GLONASS NMEA: %X", val);

			break;
		case QUECTEL_NMEA_GALILEO:
			val = (cfg->galileo.gsv);

			LOG_INF("Configuring GALILEO NMEA: %X", val);

			break;
		case QUECTEL_NMEA_BEIDOU:
			val = (cfg->beidou.gsv << 1 | cfg->beidou.gsa);

			LOG_INF("Configuring BEIDOU NMEA: %X", val);

			break;
		case QUECTEL_NMEA_GSVEXT:
			val = cfg->gsvext.enable;

			LOG_INF("Configuring GSVEXT NMEA: %X", val);

			break;
		default:
			LOG_ERR("Invalid quectel_nmea_types_t");
			return -EINVAL;
	}

	snprintk(buf, sizeof(buf), "AT+QGPSCFG=\"%s\",%u",
				gnss_str, val);

	ret = modem_cmd_send(&gsm.context.iface, &gsm.context.cmd_handler,
				NULL, 0U, buf, &gsm.sem_response,
				GSM_CMD_AT_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("%s ret:%d", log_strdup(buf), ret);
		return ret;
	}

	return ret;
}

/**
 * @brief Configures supported GNSS constellation
 *
 * @param[in] cfg The configuration of the constellation, see
 * quectel_gnss_conf_t.
 * @retval 0 on success, negative on failure.
 */
static int quectel_gnss_cfg_constellation(const int cfg)
{
	int  ret;
	char buf[sizeof("AT+QGPSCFG=\"gnssconfig\",#")] = {0};

	snprintk(buf, sizeof(buf), "AT+QGPSCFG=\"gnssconfig\",%d", cfg);

	ret = modem_cmd_send(&gsm.context.iface, &gsm.context.cmd_handler,
			NULL, 0U, buf, &gsm.sem_response,
			GSM_CMD_AT_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("%s ret:%d", log_strdup(buf), ret);
		return ret;
	}

	LOG_INF("Configured GNSS config: %d", cfg);

	return ret;
}

static int quectel_gnss_cfg_suplver(void)
{
	int  ret;
	char buf[sizeof("AT+QGPSCFG=\"suplver\",2")] = {0};

	snprintk(buf, sizeof(buf), "AT+QGPSCFG=\"suplver\",2");

	ret = modem_cmd_send(&gsm.context.iface, &gsm.context.cmd_handler,
			NULL, 0U, buf, &gsm.sem_response,
			GSM_CMD_AT_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("%s ret:%d", log_strdup(buf), ret);
		return ret;
	}

	return ret;
}

static int quectel_gnss_cfg_plane(void)
{
	int  ret;
	char buf[sizeof("AT+QGPSCFG=\"plane\",0")] = {0};

	snprintk(buf, sizeof(buf), "AT+QGPSCFG=\"plane\",0");

	ret = modem_cmd_send(&gsm.context.iface, &gsm.context.cmd_handler,
			NULL, 0U, buf, &gsm.sem_response,
			GSM_CMD_AT_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("%s ret:%d", log_strdup(buf), ret);
		return ret;
	}

	return ret;
}

static int quectel_gnss_cfg_suplurl(void)
{
	int  ret;
	char buf[sizeof("AT+QGPSSUPLURL=\""CONFIG_MODEM_GSM_QUECTELL_GNSS_SUPL_URL"\"")] = {0};

	snprintk(buf, sizeof(buf), "AT+QGPSSUPLURL=\""CONFIG_MODEM_GSM_QUECTELL_GNSS_SUPL_URL"\"");

	ret = modem_cmd_send(&gsm.context.iface, &gsm.context.cmd_handler,
			NULL, 0U, buf, &gsm.sem_response,
			GSM_CMD_AT_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("%s ret:%d", log_strdup(buf), ret);
		return ret;
	}

	return ret;
}

static int quectel_gnss_cfg_token(void)
{
	int  ret;
	char buf[sizeof("AT+QLBSCFG=\"token\",\""CONFIG_MODEM_GSM_QUECTELL_GNSS_QLOC_TOK"\"")] = {
		0
	};

	snprintk(buf, sizeof(buf),
		 "AT+QLBSCFG=\"token\",\""CONFIG_MODEM_GSM_QUECTELL_GNSS_QLOC_TOK"\"");

	ret = modem_cmd_send(&gsm.context.iface, &gsm.context.cmd_handler,
			NULL, 0U, buf, &gsm.sem_response,
			GSM_CMD_AT_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("%s ret:%d", log_strdup(buf), ret);
		return ret;
	}

	return ret;
}

static int quectel_gnss_cfg_latorder(void)
{
	int  ret;
	char buf[sizeof("AT+QLBSCFG=\"latorder\",1")] = {0};

	snprintk(buf, sizeof(buf), "AT+QLBSCFG=\"latorder\",1");

	ret = modem_cmd_send(&gsm.context.iface, &gsm.context.cmd_handler,
			NULL, 0U, buf, &gsm.sem_response,
			GSM_CMD_AT_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("%s ret:%d", log_strdup(buf), ret);
		return ret;
	}

	return ret;
}

/**
 * @brief Enables the GNSS, for more info please read the Quectel GNSS manual.
 *
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
static int quectel_gnss_enable(const uint8_t fixmaxtime,
			const uint16_t fixmaxdist, const uint16_t fixcount, const uint16_t fixrate)
{
	int  ret;
	char buf[sizeof("AT+QGPS=#,###,####,####,#####")] = {0};

	if ((fixmaxtime == 0) ||
		(fixmaxtime == 0) || (fixmaxtime > 1000) ||
		(fixcount > 1000) ||
		(fixrate == 0))
	{
		return -EINVAL;
	}

	snprintk(buf, sizeof(buf), "AT+QGPS=%d,%u,%u,%u,%u", IS_ENABLED(CONFIG_MODEM_GSM_QUECTELL_GNSS_SUPL) ? 2 : 1,
							     fixmaxtime, fixmaxdist, fixcount, fixrate);

	ret = modem_cmd_send(&gsm.context.iface, &gsm.context.cmd_handler,
			NULL, 0U, buf, &gsm.sem_response,
			GSM_CMD_AT_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("%s ret:%d", log_strdup(buf), ret);
		return ret;
	}

	LOG_INF("Enabled %s", "Quectel GNSS");

	return ret;
}

/**
 * @brief Disables the GNSS.
 *
 * @retval 0 on success, negative on failure.
 */
static int quectel_gnss_disable(void)
{
    int  ret;

    ret = modem_cmd_send(&gsm.context.iface, &gsm.context.cmd_handler,
                    NULL, 0U, "AT+QGPSEND", &gsm.sem_response,
                    GSM_CMD_AT_TIMEOUT);
    if (ret < 0) {
        LOG_ERR("AT+QGPSEND ret:%d", ret);
        return ret;
    }

    LOG_INF("Disabled Quectel GNSS");

    return ret;
}
#endif /* #if CONFIG_MODEM_QUECTEL_GNSS */

static void gnss_configure(struct k_work *work)
{
	int ret;
	const quectel_nmea_type_t cfg = {
		.gps.rmc = 1,
		.gps.gga = 1,
		.gps.gsa = 0,
		.gps.gsv = 0,
		.gps.vtg = 0,
	};

	switch (gnss_state) {
		case PPP_GNSS_OFF:
			LOG_WRN("GNSS is off");
			break;
		case PPP_GNSS_STARTING:
			gnss_state = PPP_GNSS_CFG_OUTPORT;
			__fallthrough;
		case PPP_GNSS_CFG_OUTPORT:
			ret =  quectel_gnss_cfg_outport(CONFIG_MODEM_GSM_QUECTEL_GNSS_OP);
			if (ret < 0) {
				LOG_ERR("quectel_gnss_cfg_outport failed: %d", ret);
				gsm_work_reschedule(&gnss_configure_work, K_SECONDS(2));
				return;
			}

			gnss_state = PPP_GNSS_CFG_CONSTELLATION;
			gsm_work_reschedule(&gnss_configure_work, K_NO_WAIT);
			break;
		case PPP_GNSS_CFG_CONSTELLATION:
			ret =  quectel_gnss_cfg_constellation(GNSS_CONSTELLATION_CFG);
			if (ret < 0) {
				LOG_ERR("quectel_gnss_cfg_constellation: %d failed: %d",
					GNSS_CONSTELLATION_CFG, ret);
				gsm_work_reschedule(&gnss_configure_work, K_SECONDS(2));
				return;
			}

			gnss_state = PPP_GNSS_CFG_NMEA;
			gsm_work_reschedule(&gnss_configure_work, K_NO_WAIT);
			break;
		case PPP_GNSS_CFG_NMEA:
			ret =  quectel_gnss_cfg_nmea(QUECTEL_NMEA_GPS, QUECTEL_NMEA_GPS_STR, &cfg);
			if (ret < 0) {
				LOG_ERR("quectel_gnss_cfg_nmea failed: %d", ret);
				gsm_work_reschedule(&gnss_configure_work, K_SECONDS(2));
				return;
			}

			if (IS_ENABLED(CONFIG_MODEM_GSM_QUECTELL_GNSS_SUPL)) {
				gnss_state = PPP_GNSS_CFG_SUPLVER;
			} else if (IS_ENABLED(CONFIG_MODEM_GSM_QUECTELL_GNSS_QLOC)) {
				gnss_state = PPP_GNSS_CFG_TOKEN;
			} else {
				gnss_state = PPP_GNSS_CFG_TURN_ON;
			}

			gsm_work_reschedule(&gnss_configure_work, K_NO_WAIT);
			break;
		case PPP_GNSS_CFG_SUPLVER:
			ret =  quectel_gnss_cfg_suplver();
			if (ret < 0) {
				LOG_ERR("%s failed: %d", "quectel_gnss_cfg_suplver", ret);
				gsm_work_reschedule(&gnss_configure_work, K_SECONDS(2));
				return;
			}

			gnss_state = PPP_GNSS_CFG_PLANE;
			gsm_work_reschedule(&gnss_configure_work, K_NO_WAIT);
			break;
		case PPP_GNSS_CFG_PLANE:
			ret =  quectel_gnss_cfg_plane();
			if (ret < 0) {
				LOG_ERR("%s failed: %d", "quectel_gnss_cfg_plane", ret);
				gsm_work_reschedule(&gnss_configure_work, K_SECONDS(2));
				return;
			}

			gnss_state = PPP_GNSS_CFG_SUPLURL;
			gsm_work_reschedule(&gnss_configure_work, K_NO_WAIT);
			break;
		case PPP_GNSS_CFG_SUPLURL:
			ret =  quectel_gnss_cfg_suplurl();
			if (ret < 0) {
				LOG_ERR("%s failed: %d", "quectel_gnss_cfg_suplurl", ret);
				gsm_work_reschedule(&gnss_configure_work, K_SECONDS(2));
				return;
			}

			gnss_state = PPP_GNSS_CFG_TOKEN;

			if (IS_ENABLED(CONFIG_MODEM_GSM_QUECTELL_GNSS_QLOC)) {
				gnss_state = PPP_GNSS_CFG_TOKEN;
			} else {
				gnss_state = PPP_GNSS_CFG_TURN_ON;
			}

			gsm_work_reschedule(&gnss_configure_work, K_NO_WAIT);
			break;
		case PPP_GNSS_CFG_TOKEN:
			ret =  quectel_gnss_cfg_token();
			if (ret < 0) {
				LOG_ERR("%s failed: %d", "quectel_gnss_cfg_token", ret);
				gsm_work_reschedule(&gnss_configure_work, K_SECONDS(2));
				return;
			}

			gnss_state = PPP_GNSS_CFG_LATORDER;
			gsm_work_reschedule(&gnss_configure_work, K_NO_WAIT);
			break;
		case PPP_GNSS_CFG_LATORDER:
			ret =  quectel_gnss_cfg_latorder();
			if (ret < 0) {
				LOG_ERR("%s failed: %d", "quectel_gnss_cfg_latorder", ret);
				gsm_work_reschedule(&gnss_configure_work, K_SECONDS(2));
				return;
			}

			gnss_state = PPP_GNSS_CFG_TURN_ON;
			gsm_work_reschedule(&gnss_configure_work, K_NO_WAIT);
			break;
		case PPP_GNSS_CFG_TURN_ON:
			ret =  quectel_gnss_enable(FIX_MAX_TIME, FIX_MAX_DIST,
							FIX_COUNT, FIX_INTERVAL);
			if (ret < 0) {
				LOG_ERR("quectel_gnss_enable failed: %d", ret);
				gsm_work_reschedule(&gnss_configure_work, K_SECONDS(2));
				return;
			}

			gnss_state = PPP_GNSS_READY;
			__fallthrough;
		case PPP_GNSS_READY:
			k_sem_give(&gnss_ready_sem);
			if (gsm.gnss_on_cb) {
				gsm.gnss_on_cb();
			}
			LOG_WRN("GNSS is ready");
			break;
	}
}

int gsm_ppp_gnss_enable(void) {
	gnss_enabled = true;

	return 0;
}

int gsm_ppp_gnss_disable(void) {
	struct k_work_sync work_sync;

	gnss_enabled = false;
	(void)k_work_cancel_delayable_sync(&gnss_configure_work, &work_sync);

	if (gnss_state == PPP_GNSS_READY) {
		quectel_gnss_disable();
	}

	k_sem_reset(&gnss_ready_sem);

	return 0;
}

int gsm_ppp_gnss_wait_until_ready(int s) {
	int ret;

	k_sem_reset(&gnss_ready_sem);

	if (gnss_state == PPP_GNSS_READY) {
		return 0;
	}

	ret = k_sem_take(&gnss_ready_sem, K_SECONDS(s));
	if (ret)
	{
		LOG_WRN("%s error: %d", "k_sem_take", ret);
	}

	return ret;
}

static void gsm_configure(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct gsm_modem *gsm = CONTAINER_OF(dwork, struct gsm_modem,
					     gsm_configure_work);
	int ret = -1;

	LOG_DBG("Starting modem %p configuration", gsm);

	if (gsm->state == GSM_PPP_START) {
		disable_power_source(&gsm->context);
		gsm->state = GSM_PPP_PWR_SRC_OFF;
		/* Arbitrary delay to drain the power */
		(void)gsm_work_reschedule(&gsm->gsm_configure_work, K_SECONDS(1));
	}

	if (gsm->state == GSM_PPP_PWR_SRC_OFF) {
		enable_power_source(&gsm->context);
		gsm->state = GSM_PPP_PWR_SRC_ON;
		/* Arbitrary delay for the power to stabilize */
		(void)gsm_work_reschedule(&gsm->gsm_configure_work, K_SECONDS(2));
	}

	if (gsm->state == GSM_PPP_PWR_SRC_ON) {
		power_on_ops(&gsm->context);
		gsm->state = GSM_PPP_WAIT_AT;
		(void)gsm_work_reschedule(&gsm->gsm_configure_work, K_SECONDS(15));
		return;
	}

	ret = modem_cmd_send_nolock(&gsm->context.iface,
				    &gsm->context.cmd_handler,
				    &response_cmds[0],
				    ARRAY_SIZE(response_cmds),
				    "AT", &gsm->sem_response,
				    GSM_CMD_AT_TIMEOUT);
	if (ret < 0) {
		LOG_DBG("modem not ready %d", ret);

		(void)gsm_work_reschedule(&gsm->gsm_configure_work, K_NO_WAIT);

		return;
	}

	gsm->state = GSM_PPP_AT_RDY;

#if IS_ENABLED(CONFIG_MODEM_GSM_QUECTEL_GNSS_AUTOSTART)
	LOG_WRN("Auto starting gnss configuration in %d seconds", 20);
	gnss_state = PPP_GNSS_STARTING;
	(void)gsm_work_reschedule(&gnss_configure_work, K_SECONDS(20));
#endif

#if IS_ENABLED(CONFIG_GSM_MUX)
	if (mux_enable(gsm) == 0) {
		gsm->state = GSM_PPP_MUX_ENABLED;
	} else {
		(void)gsm_work_reschedule(&gsm->gsm_configure_work,
					K_NO_WAIT);
		return;
	}

	LOG_DBG("GSM muxing enabled");
	gsm->state = GSM_PPP_STATE_INIT;

	k_work_init_delayable(&gsm->gsm_configure_work,
			      mux_setup);

	(void)gsm_work_reschedule(&gsm->gsm_configure_work,
				K_NO_WAIT);
#else
	gsm->state = GSM_PPP_SETUP;
	gsm_finalize_connection(gsm);
#endif
}

void gsm_ppp_start(const struct device *dev)
{
	struct gsm_modem *gsm = dev->data;

	if (gsm->state != GSM_PPP_STOP) {
		LOG_ERR("gsm_ppp is already started");
		return;
	}

#if DT_INST_NODE_HAS_PROP(0, power_src_gpios)
	gsm->state = GSM_PPP_START;
#elif DT_INST_NODE_HAS_PROP(0, power_key_gpios)
	gsm->state = GSM_PPP_PWR_SRC_ON;
#else
	gsm->state = GSM_PPP_WAIT_AT;
#endif

	/* Re-init underlying UART comms */
	int r = modem_iface_uart_init_dev(&gsm->context.iface,
				DEVICE_DT_GET(GSM_UART_NODE));
	if (r) {
		LOG_ERR("modem_iface_uart_init returned %d", r);
		return;
	}

	k_work_init_delayable(&gsm->gsm_configure_work, gsm_configure);
	(void)gsm_work_reschedule(&gsm->gsm_configure_work, K_NO_WAIT);

#if IS_ENABLED(CONFIG_GSM_MUX)
	k_work_init_delayable(&rssi_work_handle, rssi_handler);
#endif
}

void gsm_ppp_stop(const struct device *dev)
{
	struct gsm_modem *gsm = dev->data;
	struct net_if *iface = gsm->iface;
	struct k_work_sync work_sync;

	if (gsm->state == GSM_PPP_STOP) {
		LOG_ERR("gsm_ppp is already stopped");
		return;
	}

	(void)k_work_cancel_delayable_sync(&gsm->gsm_configure_work, &work_sync);
	(void)k_work_cancel_delayable_sync(&gnss_configure_work, &work_sync);
	(void)k_work_cancel_delayable_sync(&rssi_work_handle, &work_sync);

	net_if_l2(iface)->enable(iface, false);

#if IS_ENABLED(CONFIG_GSM_MUX)
	if (gsm->ppp_dev) {
		uart_mux_disable(gsm->ppp_dev);
	}
#endif

	if (modem_cmd_handler_tx_lock(&gsm->context.cmd_handler,
				      K_SECONDS(10))) {
		LOG_WRN("Failed locking modem cmds!");
	}

	gnss_state = PPP_GNSS_OFF;
	gsm->state = GSM_PPP_STOP;
	power_off_ops(&gsm->context);
	disable_power_source(&gsm->context);
}

void gsm_ppp_register_gnss_callback(const struct device *dev,
				    gsm_modem_gnss_cb cb)
{
	struct gsm_modem *gsm = dev->data;

	gsm->gnss_on_cb = cb;
}

static int gsm_init(const struct device *dev)
{
	struct gsm_modem *gsm = dev->data;
	int r;

	LOG_DBG("Generic GSM modem (%p)", gsm);

	gsm->cmd_handler_data.cmds[CMD_RESP] = response_cmds;
	gsm->cmd_handler_data.cmds_len[CMD_RESP] = ARRAY_SIZE(response_cmds);
	gsm->cmd_handler_data.cmds[CMD_UNSOL] = unsol_cmds;
	gsm->cmd_handler_data.cmds_len[CMD_UNSOL] = ARRAY_SIZE(unsol_cmds);
	gsm->cmd_handler_data.match_buf = &gsm->cmd_match_buf[0];
	gsm->cmd_handler_data.match_buf_len = sizeof(gsm->cmd_match_buf);
	gsm->cmd_handler_data.buf_pool = &gsm_recv_pool;
	gsm->cmd_handler_data.alloc_timeout = K_NO_WAIT;
	gsm->cmd_handler_data.eol = "\r";

	/* initialize the work queue */
	k_work_queue_init(&gsm_workq);
	k_work_queue_start(&gsm_workq, gsm_workq_stack, K_KERNEL_STACK_SIZEOF(gsm_workq_stack),
			   K_PRIO_COOP(7), NULL);
	k_thread_name_set(&gsm_workq.thread, "gsm_workq");

	k_sem_init(&gsm->sem_response, 0, 1);
	k_sem_init(&gnss_ready_sem, 0, 1);

	r = modem_cmd_handler_init(&gsm->context.cmd_handler,
				   &gsm->cmd_handler_data);
	if (r < 0) {
		LOG_DBG("cmd handler error %d", r);
		return r;
	}

#if IS_ENABLED(CONFIG_MODEM_GSM_INFO)
	/* modem information storage */
	gsm->context.data_manufacturer = minfo.mdm_manufacturer;
	gsm->context.data_model = minfo.mdm_model;
	gsm->context.data_revision = minfo.mdm_revision;
	gsm->context.data_imei = minfo.mdm_imei;
#if IS_ENABLED(CONFIG_MODEM_SIM_NUMBERS)
	gsm->context.data_imsi = minfo.mdm_imsi;
	gsm->context.data_iccid = minfo.mdm_iccid;
#endif	/* CONFIG_MODEM_SIM_NUMBERS */
#endif	/* CONFIG_MODEM_GSM_INFO */

	gsm->context.is_automatic_oper = false;
	gsm->gsm_data.rx_rb_buf = &gsm->gsm_rx_rb_buf[0];
	gsm->gsm_data.rx_rb_buf_len = sizeof(gsm->gsm_rx_rb_buf);

#if HAS_PWR_SRC || HAS_PWR_KEY
	gsm->context.pins = modem_pins;
	gsm->context.pins_len = ARRAY_SIZE(modem_pins);
#endif

	r = modem_iface_uart_init(&gsm->context.iface, &gsm->gsm_data,
				DEVICE_DT_GET(GSM_UART_NODE));
	if (r < 0) {
		LOG_DBG("iface uart error %d", r);
		return r;
	}

	r = modem_context_register(&gsm->context);
	if (r < 0) {
		LOG_DBG("context error %d", r);
		return r;
	}

	/* Initialize to stop state so that it can be started later */
	gsm->state = GSM_PPP_STOP;

	k_work_init_delayable(&gnss_configure_work, gnss_configure);
	gnss_enabled = IS_ENABLED(CONFIG_MODEM_GSM_QUECTEL_GNSS_AUTOSTART);
	gnss_state = PPP_GNSS_OFF;

	LOG_DBG("iface->read %p iface->write %p",
		gsm->context.iface.read, gsm->context.iface.write);

	k_thread_create(&gsm_rx_thread, gsm_rx_stack,
			K_KERNEL_STACK_SIZEOF(gsm_rx_stack),
			(k_thread_entry_t) gsm_rx,
			gsm, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);
	k_thread_name_set(&gsm_rx_thread, "gsm_rx");

	gsm->iface = ppp_net_if();
	if (!gsm->iface) {
		LOG_ERR("Couldn't find ppp net_if!");
		return -ENODEV;
	}

#if CONFIG_GSM_PPP_AUTOSTART
	gsm_ppp_start(dev);
#endif

	return 0;
}

DEVICE_DT_DEFINE(DT_INST(0, zephyr_gsm_ppp), gsm_init, NULL, &gsm, NULL,
		 POST_KERNEL, CONFIG_MODEM_GSM_INIT_PRIORITY, NULL);
