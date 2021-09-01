/*
 * Copyright (c) 2020 Intel Corporation
 * Copyright (c) 2021 G-Technologies Sdn. Bhd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT gsm_ppp

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
#include <drivers/console/uart_mux.h>

#include "modem_context.h"
#include "modem_iface_uart.h"
#include "modem_cmd_handler.h"
#include "../console/gsm_mux.h"

#include <stdio.h>

#ifdef CONFIG_NEWLIB_LIBC
#include <time.h>
#endif

#if (CONFIG_MODEM_GSM_QUECTEL)
#include "quectel-mdm.h"
#endif

#define GSM_UART_DEV_ID                 DT_INST_BUS(0)
#define GSM_UART_DEV_NAME               DT_INST_BUS_LABEL(0)
#define GSM_CMD_READ_BUF                128
#define GSM_CMD_AT_TIMEOUT              K_SECONDS(2)
#define GSM_CMD_SETUP_TIMEOUT           K_SECONDS(6)
#define GSM_RX_STACK_SIZE               CONFIG_MODEM_GSM_RX_STACK_SIZE
#define GSM_RECV_MAX_BUF                30
#define GSM_RECV_BUF_SIZE               128
#define GSM_REGISTER_DELAY_MSEC         1000
#define GSM_ATTACH_RETRY_DELAY_MSEC     1000

#define GSM_RSSI_RETRY_DELAY_MSEC       2000
#define GSM_RSSI_RETRIES                10
#define GSM_RSSI_INVALID                -1000

#if defined(CONFIG_MODEM_GSM_ENABLE_CESQ_RSSI)
	#define GSM_RSSI_MAXVAL          0
#else
	#define GSM_RSSI_MAXVAL         -51
#endif

#ifdef CONFIG_NEWLIB_LIBC
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
#endif

#if CONFIG_MODEM_GSM_QUECTEL_GNSS
static const char quectel_nmea_str[5][16] = {
	"gpsnmeatype\0",
	"glonassnmeatype\0",
	"galileonmeatype\0",
	"beidounmeatype\0",
	"gsvextnmeatype\0",
};
#endif

/* Modem network registration state */
enum network_state {
	GSM_NOT_REGISTERED = 0,
	GSM_HOME_NETWORK,
	GSM_SEARCHING,
	GSM_REGISTRATION_DENIED,
	GSM_UNKNOWN,
	GSM_ROAMING,
};

/* During the modem setup, we first create DLCI control channel and then
 * PPP and AT channels. Currently the modem does not create possible GNSS
 * channel.
 */
enum setup_state {
	STATE_INIT = 0,
	STATE_CONTROL_CHANNEL = 0,
	STATE_PPP_CHANNEL,
	STATE_AT_CHANNEL,
	STATE_DONE
};

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

	enum network_state net_state;
	int register_retries;

	enum setup_state state;
	const struct device *ppp_dev;
	const struct device *at_dev;
	const struct device *control_dev;

#ifdef CONFIG_NEWLIB_LIBC
	bool local_time_valid : 1;
	int32_t local_time_offset;
	struct tm local_time;
#endif

	struct net_if *iface;

	int rssi_retries;
	int attach_retries;
	bool powered_on : 1;
	bool mux_enabled : 1;
	bool mux_setup_done : 1;
	bool setup_done : 1;
	bool attached : 1;
} gsm;

NET_BUF_POOL_DEFINE(gsm_recv_pool, GSM_RECV_MAX_BUF, GSM_RECV_BUF_SIZE,
		    0, NULL);
K_KERNEL_STACK_DEFINE(gsm_rx_stack, GSM_RX_STACK_SIZE);

struct k_thread gsm_rx_thread;
static struct k_work_delayable rssi_work_handle;

#if defined(CONFIG_MODEM_GSM_ENABLE_CESQ_RSSI)
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
#endif

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

MODEM_CMD_DEFINE(gsm_cmd_cme_error)
{
	modem_cmd_handler_set_error(data, -EINVAL);
	LOG_DBG("cme error: %d", atoi(argv[0]));
	k_sem_give(&gsm.sem_response);
	return 0;
}

#if CONFIG_MODEM_GSM_QUECTEL
/* Handler: Modem ready. */
MODEM_CMD_DEFINE(on_cmd_unsol_rdy)
{
	k_sem_give(&gsm.sem_response);
	return 0;
}
#endif

static const struct modem_cmd response_cmds[] = {
	MODEM_CMD("OK", gsm_cmd_ok, 0U, ""),
	MODEM_CMD("ERROR", gsm_cmd_error, 0U, ""),
	MODEM_CMD("+CME ERROR: ", gsm_cmd_cme_error, 1U, ""),
	MODEM_CMD("CONNECT", gsm_cmd_ok, 0U, ""),
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
	if (argc >= 3) {
#if defined(CONFIG_MODEM_CELL_INFO)
		gsm.context.data_operator = unquoted_atoi(argv[2], 10);
		LOG_INF("operator: %u",
			gsm.context.data_operator);
#endif
		if (unquoted_atoi(argv[0], 10) == 0) {
			gsm.context.is_automatic_oper = true;
		} else {
			gsm.context.is_automatic_oper = false;
		}
	}

	return 0;
}

#if CONFIG_MODEM_GSM_QUECTEL
static const struct modem_cmd unsol_cmds[] = {
	MODEM_CMD("RDY", on_cmd_unsol_rdy, 0U, ""),
};
#endif

#if defined(CONFIG_MODEM_INFO)
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
#if defined(CONFIG_MODEM_SIM_NUMBERS)
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

#if defined(CONFIG_MODEM_SIM_NUMBERS)
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

#if defined(CONFIG_MODEM_CELL_INFO)

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
#endif /* CONFIG_MODEM_INFO */

#if defined(CONFIG_MODEM_GSM_ENABLE_CESQ_RSSI)
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
#endif

#if defined(CONFIG_MODEM_GSM_ENABLE_CESQ_RSSI)
static const struct modem_cmd read_rssi_cmd =
	MODEM_CMD("+CESQ:", on_cmd_atcmdinfo_rssi_cesq, 6U, ",");
#else
static const struct modem_cmd read_rssi_cmd =
	MODEM_CMD("+CSQ:", on_cmd_atcmdinfo_rssi_csq, 2U, ",");
#endif

static const struct setup_cmd setup_cmds[] = {
	/* no echo */
	SETUP_CMD_NOHANDLE("ATE0"),
	/* hang up */
	SETUP_CMD_NOHANDLE("ATH"),
	/* extender errors in numeric form */
	SETUP_CMD_NOHANDLE("AT+CMEE=1"),

#if defined(CONFIG_MODEM_INFO)
	/* query modem info */
	SETUP_CMD("AT+CGMI", "", on_cmd_atcmdinfo_manufacturer, 0U, ""),
	SETUP_CMD("AT+CGMM", "", on_cmd_atcmdinfo_model, 0U, ""),
	SETUP_CMD("AT+CGMR", "", on_cmd_atcmdinfo_revision, 0U, ""),
# if defined(CONFIG_MODEM_SIM_NUMBERS)
	SETUP_CMD("AT+CIMI", "", on_cmd_atcmdinfo_imsi, 0U, ""),
	SETUP_CMD("AT+CCID", "", on_cmd_atcmdinfo_iccid, 0U, ""),
# endif
	SETUP_CMD("AT+CGSN", "", on_cmd_atcmdinfo_imei, 0U, ""),
#endif

	/* disable unsolicited network registration codes */
	SETUP_CMD_NOHANDLE("AT+CREG=0"),

	/* create PDP context */
	SETUP_CMD_NOHANDLE("AT+CGDCONT=1,\"IP\",\"" CONFIG_MODEM_GSM_APN "\""),
};

#ifdef CONFIG_MODEM_GSM_QUECTEL_GNSS
int quectel_gnss_cfg_outport(const struct device *dev, const char* outport)
{
	int  ret;
	char buf[sizeof("AT+QGPSCFG=\"outport\",\"#########\"")] = {0};
	struct gsm_modem *gsm = dev->data;

	snprintk(buf, sizeof(buf), "AT+QGPSCFG=\"outport\",\"%s\"", outport);

	ret = modem_cmd_send(&gsm->context.iface, &gsm->context.cmd_handler,
			NULL, 0U, buf, &gsm->sem_response,
			GSM_CMD_AT_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("%s ret:%d", log_strdup(buf), ret);
		errno = -ret;
		return -1;
	}

	LOG_INF("Configured GNSS outport to %s", outport);

	return ret;
}

int quectel_gnss_cfg_nmea(const struct device *dev,
			  const quectel_nmea_types_t gnss,
			  const quectel_nmea_type_t *cfg)
{
	int  ret;
	uint8_t val;
	char buf[sizeof("AT+QGPSCFG=\"glonassnmeatype\",##")] = {0};
	struct gsm_modem *gsm = dev->data;

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
			errno = -EINVAL;
			return -1;
	}

	snprintk(buf, sizeof(buf), "AT+QGPSCFG=\"%s\",%u",
				quectel_nmea_str[gnss], val);

	ret = modem_cmd_send(&gsm->context.iface, &gsm->context.cmd_handler,
				NULL, 0U, buf, &gsm->sem_response,
				GSM_CMD_AT_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("%s ret:%d", log_strdup(buf), ret);
		errno = -ret;
		return -1;
	}

	return ret;
}

int quectel_gnss_cfg_gnss(const struct device *dev, const quectel_gnss_conf_t cfg)
{
	int  ret;
	char buf[sizeof("AT+QGPSCFG=\"gnssconfig\",#")] = {0};
	struct gsm_modem *gsm = dev->data;

	snprintk(buf, sizeof(buf), "AT+QGPSCFG=\"gnssconfig\",%u", cfg);

	ret = modem_cmd_send(&gsm->context.iface, &gsm->context.cmd_handler,
			NULL, 0U, buf, &gsm->sem_response,
			GSM_CMD_AT_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("%s ret:%d", log_strdup(buf), ret);
		errno = -ret;
		return -1;
	}

	LOG_INF("Configured GNSS config: %d", (int)cfg);

	return ret;
}

int quectel_gnss_enable(const struct device *dev, const uint8_t fixmaxtime,
			const uint16_t fixmaxdist, const uint16_t fixcount,
			const uint16_t fixrate)
{
	int  ret;
	char buf[sizeof("AT+QGPS=1,###,####,####,#####")] = {0};
	struct gsm_modem *gsm = dev->data;
	if(!gsm->powered_on) {
		return 0;
	}

	if ((fixmaxtime == 0) ||
		(fixmaxtime == 0) || (fixmaxtime > 1000) ||
		(fixcount > 1000) ||
		(fixrate == 0))
	{
		errno = -EINVAL;
		return -1;
	}

	snprintk(buf, sizeof(buf), "AT+QGPS=1,%u,%u,%u,%u", fixmaxtime,
							    fixmaxdist,
							    fixcount,
							    fixrate);

	ret = modem_cmd_send(&gsm->context.iface, &gsm->context.cmd_handler,
			NULL, 0U, buf, &gsm->sem_response,
			GSM_CMD_AT_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("%s ret:%d", log_strdup(buf), ret);
		errno = -ret;
		return -1;
	}

	LOG_INF("Enabled Quectel GNSS");

	return ret;
}

int quectel_gnss_disable(const struct device *dev)
{
	int  ret;
	struct gsm_modem *gsm = dev->data;
	if(!gsm->powered_on) {
		return 0;
	}

	ret = modem_cmd_send(&gsm->context.iface, &gsm->context.cmd_handler,
			NULL, 0U, "AT+QGPSEND", &gsm->sem_response,
			GSM_CMD_AT_TIMEOUT);
	if (ret < 0) {
	LOG_ERR("AT+QGPSEND ret:%d", ret);
	errno = -ret;
	return -1;
	}

	LOG_INF("Disabled Quectel GNSS");

	return ret;
}
#endif /* #if CONFIG_MODEM_QUECTEL_GNSS */

#ifdef CONFIG_NEWLIB_LIBC
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
	if ((time_string[i] == '+' || time_string[i] == '-') &&
	    (time_string[i + offset] == '"')) {
		return true;
	}
	return false;
}

int get_next_time_string_digit(int *failure_cnt, char **pp, int min, int max)
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

static bool convert_time_string_to_struct(struct tm *tm, int32_t *offset,
					  char *time_string)
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
	tm->tm_mon =
		get_next_time_string_digit(&fc, &ptr, TM_MONTH_RANGE_PLUS_1) -
		1;
	tm->tm_mday = get_next_time_string_digit(&fc, &ptr, TM_DAY_RANGE);
	tm->tm_hour = get_next_time_string_digit(&fc, &ptr, TM_HOUR_RANGE);
	tm->tm_min = get_next_time_string_digit(&fc, &ptr, TM_MIN_RANGE);
	tm->tm_sec = get_next_time_string_digit(&fc, &ptr, TM_SEC_RANGE);
	tm->tm_isdst = 0;
	*offset = (int32_t)get_next_time_string_digit(&fc, &ptr,
						      QUARTER_HOUR_RANGE) *
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
	if (!gsm->powered_on)
	{
		return -EIO;
	}
	gsm->local_time_valid = false;

	ret = modem_cmd_send(&gsm->context.iface, &gsm->context.cmd_handler,
			     &cmd, 1U, "AT+CCLK?", &gsm->sem_response,
			     GSM_CMD_AT_TIMEOUT);

	if (gsm->local_time_valid) {
		memcpy(tm, &gsm->local_time, sizeof(struct tm));
		memcpy(offset, &gsm->local_time_offset, sizeof(*offset));
	} else {
		ret = -EIO;
	}
	return ret;
}
#endif /* CONFIG_NEWLIB_LIBC */

MODEM_CMD_DEFINE(on_cmd_net_reg_sts)
{
	enum network_state net_sts = (enum network_state)atoi(argv[1]);

	switch (net_sts) {
	case GSM_NOT_REGISTERED:
		LOG_INF("Network not registered.");
		break;
	case GSM_HOME_NETWORK:
		LOG_INF("Network registered, home network.");
		break;
	case GSM_SEARCHING:
		LOG_INF("Searching for network...");
		break;
	case GSM_REGISTRATION_DENIED:
		LOG_INF("Network registration denied.");
		break;
	case GSM_UNKNOWN:
		LOG_INF("Network unknown.");
		break;
	case GSM_ROAMING:
		LOG_INF("Network registered, roaming.");
		break;
	}

	gsm.net_state = net_sts;

	return 0;
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
	/* Just return, sem_response will be signaled by the following OK */

	return 0;
}


static const struct modem_cmd read_cops_cmd =
	MODEM_CMD("+COPS", on_cmd_atcmdinfo_cops, 3U, ",");

static const struct modem_cmd check_net_reg_cmd =
	MODEM_CMD("+CREG: ", on_cmd_net_reg_sts, 2U, ",");

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
	const struct device *ppp_dev =
		device_get_binding(CONFIG_NET_PPP_DRV_NAME);
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
#if defined(CONFIG_MODEM_GSM_ENABLE_CESQ_RSSI)
	ret = modem_cmd_send_nolock(&gsm.context.iface, &gsm.context.cmd_handler,
		&read_rssi_cmd, 1, "AT+CESQ", &gsm.sem_response, GSM_CMD_SETUP_TIMEOUT);
#else
	ret = modem_cmd_send_nolock(&gsm.context.iface, &gsm.context.cmd_handler,
		&read_rssi_cmd, 1, "AT+CSQ", &gsm.sem_response, GSM_CMD_SETUP_TIMEOUT);
#endif

	if (ret < 0) {
		LOG_DBG("No answer to RSSI readout, %s", "ignoring...");
	}

#if defined(CONFIG_GSM_MUX)
#if defined(CONFIG_MODEM_CELL_INFO)
	(void) gsm_query_cellinfo(&gsm);
#endif
	k_work_reschedule(&rssi_work_handle, K_SECONDS(CONFIG_MODEM_GSM_RSSI_POLLING_PERIOD));
#endif

}

static void gsm_finalize_connection(struct gsm_modem *gsm)
{
	int ret = 0;

	/* If already attached, jump right to RSSI readout */
	if (gsm->attached) {
		goto attached;
	}

	/* If modem is searching for network, we should skip the setup step */
	if ((gsm->net_state == GSM_SEARCHING) && gsm->register_retries) {
		goto registering;
	}

	/* If attach check failed, we should not redo every setup step */
	if (gsm->attach_retries) {
		goto attaching;
	}

	if (IS_ENABLED(CONFIG_GSM_MUX) && gsm->mux_enabled) {
		ret = modem_cmd_send_nolock(&gsm->context.iface,
					    &gsm->context.cmd_handler,
					    &response_cmds[0],
					    ARRAY_SIZE(response_cmds),
					    "AT", &gsm->sem_response,
					    GSM_CMD_AT_TIMEOUT);
		if (ret < 0) {
			LOG_ERR("modem setup returned %d, %s",
				ret, "retrying...");
			(void)k_work_reschedule(&gsm->gsm_configure_work,
						K_SECONDS(1));
			return;
		}
	}

	if (IS_ENABLED(CONFIG_MODEM_GSM_FACTORY_RESET_AT_BOOT)) {
		(void)modem_cmd_send_nolock(&gsm->context.iface,
					    &gsm->context.cmd_handler,
					    &response_cmds[0],
					    ARRAY_SIZE(response_cmds),
					    "AT&F", &gsm->sem_response,
					    GSM_CMD_AT_TIMEOUT);
		k_sleep(K_SECONDS(1));
	}

	ret = gsm_setup_mccmno(gsm);

	if (ret < 0) {
		LOG_ERR("modem setup returned %d, %s",
				ret, "retrying...");

		(void)k_work_reschedule(&gsm->gsm_configure_work,
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
		LOG_DBG("modem setup returned %d, %s",
			ret, "retrying...");
		(void)k_work_reschedule(&gsm->gsm_configure_work, K_SECONDS(1));
		return;
	}

registering:
	/* Wait for cell tower registration */
	ret = modem_cmd_send_nolock(&gsm->context.iface,
				    &gsm->context.cmd_handler,
				    &check_net_reg_cmd, 1,
				    "AT+CREG?",
				    &gsm->sem_response,
				    GSM_CMD_SETUP_TIMEOUT);
	if ((ret < 0) || ((gsm->net_state != GSM_ROAMING) &&
			 (gsm->net_state != GSM_HOME_NETWORK))) {
		if (!gsm->register_retries) {
			gsm->register_retries = CONFIG_MODEM_GSM_REGISTER_TIMEOUT *
				MSEC_PER_SEC / GSM_REGISTER_DELAY_MSEC;
		} else {
			gsm->register_retries--;

			/* Reset RF if timed out */
			if (!gsm->register_retries) {
				(void)modem_cmd_send_nolock(
					&gsm->context.iface,
					&gsm->context.cmd_handler,
					&response_cmds[0],
					ARRAY_SIZE(response_cmds),
					"AT+CFUN=0", &gsm->sem_response,
					GSM_CMD_AT_TIMEOUT);

				k_sleep(K_SECONDS(1));

				(void)modem_cmd_send_nolock(
					&gsm->context.iface,
					&gsm->context.cmd_handler,
					&response_cmds[0],
					ARRAY_SIZE(response_cmds),
					"AT+CFUN=1", &gsm->sem_response,
					GSM_CMD_AT_TIMEOUT);
			}
		}

		(void)k_work_reschedule(&gsm->gsm_configure_work,
					K_MSEC(GSM_REGISTER_DELAY_MSEC));
		return;
	}

attaching:
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

		(void)k_work_reschedule(&gsm->gsm_configure_work,
					K_MSEC(GSM_ATTACH_RETRY_DELAY_MSEC));
		return;
	}

	/* Attached, clear retry counter */
	gsm->attached = true;
	gsm->attach_retries = 0;

	LOG_DBG("modem attach returned %d, %s", ret, "read RSSI");
	gsm->rssi_retries = GSM_RSSI_RETRIES;

 attached:

	if (!IS_ENABLED(CONFIG_GSM_MUX)) {
		/* Read connection quality (RSSI) before PPP carrier is ON */
		rssi_handler(NULL);

		if (!(gsm->context.data_rssi && gsm->context.data_rssi != GSM_RSSI_INVALID &&
			gsm->context.data_rssi < GSM_RSSI_MAXVAL)) {

			LOG_DBG("Not valid RSSI, %s", "retrying...");
			if (gsm->rssi_retries-- > 0) {
				(void)k_work_reschedule(&gsm->gsm_configure_work,
							K_MSEC(GSM_RSSI_RETRY_DELAY_MSEC));
				return;
			}
		}
#if defined(CONFIG_MODEM_CELL_INFO)
		(void) gsm_query_cellinfo(gsm);
#endif
	}

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
		(void)k_work_reschedule(&gsm->gsm_configure_work, K_SECONDS(1));
		return;
	}

	gsm->setup_done = true;

	set_ppp_carrier_on(gsm);

	if (IS_ENABLED(CONFIG_GSM_MUX) && gsm->mux_enabled) {
		/* Re-use the original iface for AT channel */
		ret = modem_iface_uart_init_dev(&gsm->context.iface,
						gsm->at_dev);
		if (ret < 0) {
			LOG_DBG("iface %suart error %d", "AT ", ret);
		} else {
			/* Do a test and try to send AT command to modem */
			ret = modem_cmd_send_nolock(
				&gsm->context.iface,
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
	}
}

static int mux_enable(struct gsm_modem *gsm)
{
	int ret;

	/* Turn on muxing */
#if CONFIG_MODEM_GSM_SIMCOM
	ret = modem_cmd_send_nolock(
		&gsm->context.iface,
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
#endif
		"+CMUX=0,0,5,"
		STRINGIFY(CONFIG_GSM_MUX_MRU_DEFAULT_LEN),
		&gsm->sem_response,
		GSM_CMD_AT_TIMEOUT);
#elif CONFIG_MODEM_GSM_QUECTEL
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
#else
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
	(void)k_work_reschedule(&gsm->gsm_configure_work, K_MSEC(1));
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
	struct gsm_modem *gsm = CONTAINER_OF(work, struct gsm_modem,
					     gsm_configure_work);
	const struct device *uart = DEVICE_DT_GET(GSM_UART_DEV_ID);
	int ret;

	/* We need to call this to reactivate mux ISR. Note: This is only called
	 * after re-initing gsm_ppp.
	 */
	if (IS_ENABLED(CONFIG_GSM_MUX) &&
	    gsm->ppp_dev && gsm->state == STATE_CONTROL_CHANNEL) {
		uart_mux_enable(gsm->ppp_dev);
	}

	switch (gsm->state) {
	case STATE_CONTROL_CHANNEL:
		/* Get UART device. There is one dev / DLCI */
		if (gsm->control_dev == NULL) {
			gsm->control_dev = uart_mux_alloc();
			if (gsm->control_dev == NULL) {
				LOG_DBG("Cannot get UART mux for %s channel",
					"control");
				goto fail;
			}
		}

		gsm->state = STATE_PPP_CHANNEL;

		ret = mux_attach(gsm->control_dev, uart, DLCI_CONTROL, gsm);
		if (ret < 0) {
			goto fail;
		}

		break;

	case STATE_PPP_CHANNEL:
		if (gsm->ppp_dev == NULL) {
			gsm->ppp_dev = uart_mux_alloc();
			if (gsm->ppp_dev == NULL) {
				LOG_DBG("Cannot get UART mux for %s channel",
					"PPP");
				goto fail;
			}
		}

		gsm->state = STATE_AT_CHANNEL;

		ret = mux_attach(gsm->ppp_dev, uart, DLCI_PPP, gsm);
		if (ret < 0) {
			goto fail;
		}

		break;

	case STATE_AT_CHANNEL:
		if (gsm->at_dev == NULL) {
			gsm->at_dev = uart_mux_alloc();
			if (gsm->at_dev == NULL) {
				LOG_DBG("Cannot get UART mux for %s channel",
					"AT");
				goto fail;
			}
		}

		gsm->state = STATE_DONE;

		ret = mux_attach(gsm->at_dev, uart, DLCI_AT, gsm);
		if (ret < 0) {
			goto fail;
		}

		break;

	case STATE_DONE:
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
			gsm->mux_enabled = false;
			goto fail;
		}

		LOG_INF("PPP channel %d connected to %s",
			DLCI_PPP, gsm->ppp_dev->name);

		gsm_finalize_connection(gsm);
		break;
	}

	return;

fail:
	gsm->state = STATE_INIT;
	gsm->mux_enabled = false;
}

static void gsm_configure(struct k_work *work)
{
	struct gsm_modem *gsm = CONTAINER_OF(work, struct gsm_modem,
					     gsm_configure_work);
	int ret = -1;

#if CONFIG_MODEM_GSM_QUECTEL
	enable_power(&gsm->context);
	power_key_on(&gsm->context);

	LOG_INF("Waiting for modem to boot up");

	ret = k_sem_take(&gsm->sem_response, K_SECONDS(50));

	if (ret == 0) {
		LOG_INF("Modem RDY");
	} else {
		LOG_INF("Time out waiting for modem RDY");
		(void)k_work_reschedule(&gsm->gsm_configure_work, K_NO_WAIT);

		return;
	}
#endif

	LOG_DBG("Starting modem %p configuration", gsm);

	ret = modem_cmd_send_nolock(&gsm->context.iface,
				    &gsm->context.cmd_handler,
				    &response_cmds[0],
				    ARRAY_SIZE(response_cmds),
				    "AT", &gsm->sem_response,
				    GSM_CMD_AT_TIMEOUT);
	if (ret < 0) {
		LOG_DBG("modem not ready %d", ret);

		(void)k_work_reschedule(&gsm->gsm_configure_work, K_NO_WAIT);

		return;
	}

	if (IS_ENABLED(CONFIG_GSM_MUX) && ret == 0 &&
	    gsm->mux_enabled == false) {
		gsm->mux_setup_done = false;

		ret = mux_enable(gsm);
		if (ret == 0) {
			gsm->mux_enabled = true;
		} else {
			gsm->mux_enabled = false;
			(void)k_work_reschedule(&gsm->gsm_configure_work,
						K_NO_WAIT);
			return;
		}

		LOG_DBG("GSM muxing %s", gsm->mux_enabled ? "enabled" :
							    "disabled");

		if (gsm->mux_enabled) {
			gsm->state = STATE_INIT;

			k_work_init_delayable(&gsm->gsm_configure_work,
					      mux_setup);

			(void)k_work_reschedule(&gsm->gsm_configure_work,
						K_NO_WAIT);
			return;
		}
	}

	gsm_finalize_connection(gsm);
}

void gsm_ppp_start(const struct device *dev)
{
	struct gsm_modem *gsm = dev->data;

	if (gsm->powered_on)
	{
		return;
	}

	/* Re-init underlying UART comms */
	int r = modem_iface_uart_init_dev(&gsm->context.iface,
					  DEVICE_DT_GET(GSM_UART_DEV_ID));
	if (r) {
		LOG_ERR("modem_iface_uart_init returned %d", r);
		return;
	}

	k_work_init_delayable(&gsm->gsm_configure_work, gsm_configure);
	(void)k_work_reschedule(&gsm->gsm_configure_work, K_NO_WAIT);

#if defined(CONFIG_GSM_MUX)
	k_work_init_delayable(&rssi_work_handle, rssi_handler);
#endif

	gsm->powered_on = true;
}

void gsm_ppp_stop(const struct device *dev)
{
	struct gsm_modem *gsm = dev->data;
	struct net_if *iface = gsm->iface;
	struct k_work_sync work_sync;

	if (!gsm->powered_on)
	{
		return;
	}

	net_if_l2(iface)->enable(iface, false);

	if (IS_ENABLED(CONFIG_GSM_MUX)) {
		/* Lower mux_enabled flag to trigger re-sending AT+CMUX etc */
		gsm->mux_enabled = false;

		if (gsm->ppp_dev) {
			uart_mux_disable(gsm->ppp_dev);
		}
	}

	if (modem_cmd_handler_tx_lock(&gsm->context.cmd_handler,
				      K_SECONDS(10))) {
		LOG_WRN("Failed locking modem cmds!");
	}

	k_work_cancel_delayable_sync(&gsm->gsm_configure_work, &work_sync);
	k_work_cancel_delayable_sync(&rssi_work_handle, &work_sync);

#if (CONFIG_MODEM_GSM_QUECTEL)

	int ret;

	LOG_INF("Turning off modem...");

	/* FIXME: According to EC21 DS, after sending AT+QPOWD, the modem will
	 * 	  respond "OK" followed by "POWERED DOWN", then only we can
	 * 	  turn off the power. However in my testing I didn't get the
	 * 	  "POWERED DOWN".
	 */
	ret = modem_cmd_send_nolock(&gsm->context.iface,
				    &gsm->context.cmd_handler,
				    &response_cmds[0],
				    ARRAY_SIZE(response_cmds),
				    "AT+QPOWD=0",
				    &gsm->sem_response,
				    GSM_CMD_AT_TIMEOUT);
	if (ret < 0) {
		LOG_WRN("Modem took too long to power down normally");
	}

	disable_power(&gsm->context);

	LOG_INF("Modem powered down!");
#endif

	gsm->powered_on = false;
}

static int gsm_init(const struct device *dev)
{
	struct gsm_modem *gsm = dev->data;
	int r;

	LOG_DBG("Generic GSM modem (%p)", gsm);

	gsm->cmd_handler_data.cmds[CMD_RESP] = response_cmds;
	gsm->cmd_handler_data.cmds_len[CMD_RESP] = ARRAY_SIZE(response_cmds);
#if (CONFIG_MODEM_GSM_QUECTEL)
	gsm->cmd_handler_data.cmds[CMD_UNSOL] = unsol_cmds;
	gsm->cmd_handler_data.cmds_len[CMD_UNSOL] = ARRAY_SIZE(unsol_cmds);
#endif
	gsm->cmd_handler_data.match_buf = &gsm->cmd_match_buf[0];
	gsm->cmd_handler_data.match_buf_len = sizeof(gsm->cmd_match_buf);
	gsm->cmd_handler_data.buf_pool = &gsm_recv_pool;
	gsm->cmd_handler_data.alloc_timeout = K_NO_WAIT;
	gsm->cmd_handler_data.eol = "\r";

	k_sem_init(&gsm->sem_response, 0, 1);

	r = modem_cmd_handler_init(&gsm->context.cmd_handler,
				   &gsm->cmd_handler_data);
	if (r < 0) {
		LOG_DBG("cmd handler error %d", r);
		return r;
	}

#if defined(CONFIG_MODEM_INFO)
	/* modem information storage */
	gsm->context.data_manufacturer = minfo.mdm_manufacturer;
	gsm->context.data_model = minfo.mdm_model;
	gsm->context.data_revision = minfo.mdm_revision;
	gsm->context.data_imei = minfo.mdm_imei;
#if defined(CONFIG_MODEM_SIM_NUMBERS)
	gsm->context.data_imsi = minfo.mdm_imsi;
	gsm->context.data_iccid = minfo.mdm_iccid;
#endif	/* CONFIG_MODEM_SIM_NUMBERS */
#endif	/* CONFIG_MODEM_INFO */

	gsm->context.is_automatic_oper = false;

#if (CONFIG_MODEM_GSM_QUECTEL)
	gsm->context.pins = modem_pins;
	gsm->context.pins_len = ARRAY_SIZE(modem_pins);
#endif

	gsm->gsm_data.rx_rb_buf = &gsm->gsm_rx_rb_buf[0];
	gsm->gsm_data.rx_rb_buf_len = sizeof(gsm->gsm_rx_rb_buf);

	r = modem_iface_uart_init(&gsm->context.iface, &gsm->gsm_data,
				DEVICE_DT_GET(GSM_UART_DEV_ID));
	if (r < 0) {
		LOG_DBG("iface uart error %d", r);
		return r;
	}

	r = modem_context_register(&gsm->context);
	if (r < 0) {
		LOG_DBG("context error %d", r);
		return r;
	}

	LOG_DBG("iface->read %p iface->write %p",
		gsm->context.iface.read, gsm->context.iface.write);

	k_thread_create(&gsm_rx_thread, gsm_rx_stack,
			K_KERNEL_STACK_SIZEOF(gsm_rx_stack),
			(k_thread_entry_t) gsm_rx,
			gsm, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);
	k_thread_name_set(&gsm_rx_thread, "gsm_rx");

	gsm->net_state = GSM_NOT_REGISTERED;

	gsm->iface = ppp_net_if();
	if (!gsm->iface) {
		LOG_ERR("Couldn't find ppp net_if!");
		return -ENODEV;
	}

	gsm->powered_on = false;

	if (IS_ENABLED(CONFIG_GSM_PPP_AUTOSTART)) {
		gsm_ppp_start(dev);
	}

	return 0;
}

DEVICE_DEFINE(gsm_ppp, GSM_MODEM_DEVICE_NAME, gsm_init, NULL, &gsm, NULL,
	      POST_KERNEL, CONFIG_MODEM_GSM_INIT_PRIORITY, NULL);
