/*
 * Copyright (c) 2021 G-Technologies Sdn. Bhd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* TOOD
 * - Maybe move the mqtt send part to a thread or work that consumes the ringbuffer?
 * 	Currently it seems to send to fast and caused the MQTT to be disconnected after a burst
 * - Maybe change the 512 bytes line buffer to ring buffer on data item mode?
 * - Filter those terminal colour code
 * - Filter leading '\r\n'
 * - TLS
 */

#include <shell/shell_mqtt.h>
#include <init.h>
#include <logging/log.h>
#include <random/rand32.h>

SHELL_MQTT_DEFINE(shell_transport_mqtt);
SHELL_DEFINE(shell_mqtt, NULL, &shell_transport_mqtt,
	     CONFIG_SHELL_BACKEND_MQTT_LOG_MESSAGE_QUEUE_SIZE,
	     CONFIG_SHELL_BACKEND_MQTT_LOG_MESSAGE_QUEUE_TIMEOUT, SHELL_FLAG_OLF_CRLF);

LOG_MODULE_REGISTER(shell_mqtt, CONFIG_SHELL_MQTT_LOG_LEVEL);

#define EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)
#define APP_CONNECT_TIMEOUT_MS 2000
#define APP_SLEEP_MSECS 500
#define MQTT_PROCESS_INTERVAL 2
#define MQTT_CLIENTID "mqtt_shell"
#define SUB_TOPIC "cmd_tx"
#define PUB_TOPIC "cmd_rx"
#define SERVER_ADDR CONFIG_SHELL_MQTT_SERVER_ADDR
#define SERVER_PORT CONFIG_SHELL_MQTT_SERVER_PORT

#ifdef CONFIG_SHELL_MQTT_SERVER_USERNAME
#define MQTT_USERNAME CONFIG_SHELL_MQTT_SERVER_USERNAME
#else
#define MQTT_USERNAME NULL
#endif /* CONFIG_SHELL_MQTT_SERVER_USERNAME */

#ifdef SHELL_MQTT_SERVER_PASSWORD
#define MQTT_PASSWORD CONFIG_SHELL_MQTT_SERVER_USERNAME
#else
#define MQTT_PASSWORD NULL
#endif /*SHELL_MQTT_SERVER_PASSWORD */

struct shell_mqtt *sh_mqtt;

RING_BUF_DECLARE(rx_rb, RX_RB_SIZE);
uint8_t *rx_buf_ptr;

static void prepare_fds(struct shell_mqtt *ctx)
{
	if (ctx->client_ctx.transport.type == MQTT_TRANSPORT_NON_SECURE) {
		ctx->fds[0].fd = ctx->client_ctx.transport.tcp.sock;
	}
#if defined(CONFIG_MQTT_LIB_TLS)
	else if (ctx->client_ctx.transport.type == MQTT_TRANSPORT_SECURE) {
		ctx->fds[0].fd = ctx->client_ctx.transport.tls.sock;
	}
#endif

	ctx->fds[0].events = ZSOCK_POLLIN;
	ctx->nfds = 1;
}

static void clear_fds(struct shell_mqtt *ctx)
{
	ctx->nfds = 0;
}

static int wait(int timeout)
{
	int ret = 0;

	if (sh_mqtt->nfds > 0) {
		ret = zsock_poll(sh_mqtt->fds, sh_mqtt->nfds, timeout);
		if (ret < 0) {
			LOG_ERR("poll error: %d", errno);
		}
	}

	return ret;
}

static void mqtt_connect_work(struct k_work *work);

/*
 * Work routine to process MQTT packet and keep alive MQTT connection
 */
static void mqtt_process_work(struct k_work *work)
{
	int64_t remaining = APP_SLEEP_MSECS;
	int64_t start_time = k_uptime_get();
	struct shell_mqtt *ctx = NULL;
	int rc;

	ctx = CONTAINER_OF(work, struct shell_mqtt, mqtt_work);

	/* Listen to the port for 500 ms as defined by APP_SLEEP_MSECS */
	while (remaining > 0) {
		if (wait(remaining)) {
			rc = mqtt_input(&ctx->client_ctx);
			if (rc != 0) {
				LOG_ERR("mqtt_input error: %d", rc);
			}
		}

		rc = mqtt_live(&ctx->client_ctx);
		if (rc != 0 && rc != -EAGAIN) {
			LOG_ERR("mqtt_live error: %d", rc);
			return;
		} else if (rc == -ENOTCONN) {
			mqtt_disconnect(&ctx->client_ctx);
			k_work_init_delayable(&ctx->mqtt_work, mqtt_connect_work);
			(void)k_work_reschedule(&ctx->mqtt_work, K_SECONDS(MQTT_PROCESS_INTERVAL));
			return;
		} else if (rc == 0) {
			rc = mqtt_input(&ctx->client_ctx);
			if (rc != 0) {
				LOG_ERR("mqtt_input error: %d", rc);
			}
		}

		remaining = APP_SLEEP_MSECS + start_time - k_uptime_get();
	}

	/* Reschedule the work */
	k_work_init_delayable(&ctx->mqtt_work, mqtt_process_work);
	(void)k_work_reschedule(&ctx->mqtt_work, K_SECONDS(MQTT_PROCESS_INTERVAL));
}

static int mqtt_subscribe_config(struct mqtt_client *const client)
{
	/* subscribe to config information */
	struct mqtt_topic subs_topic = { .topic = { .utf8 = SUB_TOPIC, .size = strlen(SUB_TOPIC) },
					 .qos = MQTT_QOS_1_AT_LEAST_ONCE };
	const struct mqtt_subscription_list subs_list = { .list = &subs_topic,
							  .list_count = 1U,
							  .message_id = 1U };
	int ret;

	ret = mqtt_subscribe(client, &subs_list);
	if (ret) {
		LOG_ERR("Failed to subscribe to %s item, error %d", subs_topic.topic.utf8, ret);
	}

	return ret;
}

static void mqtt_subscribe_work(struct k_work *work)
{
	struct shell_mqtt *ctx = NULL;
	int ret;

	ctx = CONTAINER_OF(work, struct shell_mqtt, mqtt_work);

	ret = mqtt_subscribe_config(&ctx->client_ctx);
	if (ret == 0) {
		/* Change the work to process/keepalive */
		LOG_DBG("Scheduling MQTT process & keepalive work");
		k_work_init_delayable(&ctx->mqtt_work, mqtt_process_work);
		(void)k_work_reschedule(&ctx->mqtt_work, K_SECONDS(MQTT_PROCESS_INTERVAL));
	} else {
		k_work_init_delayable(&ctx->mqtt_work, mqtt_subscribe_work);
		(void)k_work_reschedule(&ctx->mqtt_work, K_SECONDS(MQTT_PROCESS_INTERVAL));
	}
}

static void mqtt_connect_work(struct k_work *work)
{
	int rc;
	struct shell_mqtt *ctx = NULL;

	ctx = CONTAINER_OF(work, struct shell_mqtt, mqtt_work);

	/* Try to connect to mqtt */
	rc = mqtt_connect(&ctx->client_ctx);
	if (rc != 0) {
		goto fail;
	}

	/* Prepare port config */
	prepare_fds(ctx);

	/* Wait for mqtt's connack */
	if (wait(APP_CONNECT_TIMEOUT_MS)) {
		ctx->state = SHELL_MQTT_OP_CONNECTING;
		mqtt_input(&ctx->client_ctx);
	}

	/* No connack, fail */
	if (ctx->state != SHELL_MQTT_OP_CONNECTED) {
		mqtt_disconnect(&ctx->client_ctx);
		goto fail;
	}

	LOG_DBG("MQTT connected successfully");
	ctx->state = SHELL_MQTT_OP_CONNECTED;

	/* Change the work to process/keepalive */
	LOG_DBG("Scheduling MQTT process & keepalive work");
	k_work_init_delayable(&ctx->mqtt_work, mqtt_subscribe_work);
	(void)k_work_reschedule(&ctx->mqtt_work, K_SECONDS(1));
	return;

fail:
	LOG_WRN("Retrying MQTT connect work after %d ms if network connected", APP_SLEEP_MSECS);
	ctx->state = SHELL_MQTT_OP_WAIT_NET;
	net_conn_mgr_resend_status();
}

/*
 * Network connection event handler
 */
static void event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event,
			  struct net_if *iface)
{
	static uint8_t interval = 1;
	if ((mgmt_event & EVENT_MASK) != mgmt_event) {
		return;
	}

	if ((mgmt_event == NET_EVENT_L4_CONNECTED)) {
		LOG_DBG("Network connected, connecting to MQTT broker");
		k_work_init_delayable(&sh_mqtt->mqtt_work, mqtt_connect_work);
		(void)k_work_reschedule(&sh_mqtt->mqtt_work,
					K_SECONDS(interval++ < 10 ? interval : 10));
		sh_mqtt->state = SHELL_MQTT_OP_CONNECTING;
		return;
	} else if (mgmt_event == NET_EVENT_L4_DISCONNECTED) {
		LOG_DBG("No network connection");
		sh_mqtt->state = SHELL_MQTT_OP_WAIT_NET;
		k_sem_reset(&sh_mqtt->tx_sem);
		k_sem_reset(&sh_mqtt->rx_sem);
		(void)k_work_cancel_delayable_sync(&sh_mqtt->mqtt_work, &sh_mqtt->work_sync);
	}
	return;
}

static void mqtt_evt_handler(struct mqtt_client *const client, const struct mqtt_evt *evt)
{
	switch (evt->type) {
	case MQTT_EVT_SUBACK:
		LOG_DBG("SUBACK packet id: %u", evt->param.suback.message_id);
		sh_mqtt->state = SHELL_MQTT_OP_SUBSCRIBED;
		break;

	case MQTT_EVT_UNSUBACK:
		LOG_DBG("UNSUBACK packet id: %u", evt->param.suback.message_id);
		sh_mqtt->state = SHELL_MQTT_OP_CONNECTED;
		(void)k_work_cancel_delayable_sync(&sh_mqtt->mqtt_work, &sh_mqtt->work_sync);
		k_work_init_delayable(&sh_mqtt->mqtt_work, mqtt_subscribe_work);
		(void)k_work_reschedule(&sh_mqtt->mqtt_work, K_SECONDS(MQTT_PROCESS_INTERVAL));
		break;

	case MQTT_EVT_CONNACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT connect failed %d", evt->result);
			break;
		}

		sh_mqtt->state = SHELL_MQTT_OP_CONNECTED;
		LOG_DBG("MQTT client connected!");

		break;

	case MQTT_EVT_DISCONNECT:
		LOG_DBG("MQTT client disconnected %d", evt->result);
		clear_fds(sh_mqtt);
		sh_mqtt->state = SHELL_MQTT_OP_CONNECTING;
		k_sem_reset(&sh_mqtt->tx_sem);
		k_sem_reset(&sh_mqtt->rx_sem);
		(void)k_work_cancel_delayable_sync(&sh_mqtt->mqtt_work, &sh_mqtt->work_sync);
		k_work_init_delayable(&sh_mqtt->mqtt_work, mqtt_connect_work);
		(void)k_work_reschedule(&sh_mqtt->mqtt_work, K_SECONDS(MQTT_PROCESS_INTERVAL));
		break;

	case MQTT_EVT_PUBLISH: {
		const struct mqtt_publish_param *pub = &evt->param.publish;
		int len = pub->message.payload.len;
		int bytes_read, ret;
		uint32_t size;

		LOG_DBG("MQTT publish received %d, %d bytes", evt->result, len);
		LOG_DBG("   id: %d, qos: %d", pub->message_id, pub->message.topic.qos);
		LOG_DBG("   item: %s", log_strdup(pub->message.topic.topic.utf8));
		/* assuming the config message is textual */
		while (len) {
			size = ring_buf_put_claim(&rx_rb, &rx_buf_ptr, len);

			bytes_read = mqtt_read_publish_payload(client, rx_buf_ptr, size);
			if (bytes_read < 0 && bytes_read != -EAGAIN) {
				LOG_ERR("failure to read payload");
				break;
			}
			ret = ring_buf_put_finish(&rx_rb, bytes_read);

			len -= bytes_read;
		}

		ring_buf_put(&rx_rb, "\r\n", sizeof("\r\n"));

		/* for MQTT_QOS_0_AT_MOST_ONCE no acknowledgment needed */
		if (pub->message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE) {
			struct mqtt_puback_param puback = { .message_id = pub->message_id };

			mqtt_publish_qos1_ack(client, &puback);
		}

		sh_mqtt->shell_handler(SHELL_TRANSPORT_EVT_RX_RDY, sh_mqtt->shell_context);

		k_sem_give(&sh_mqtt->rx_sem);
		break;
	}

	case MQTT_EVT_PUBACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBACK error %d", evt->result);
			break;
		}
		k_sem_give(&sh_mqtt->tx_sem);
		LOG_DBG("PUBACK packet id: %u", evt->param.puback.message_id);
		break;

	case MQTT_EVT_PINGRESP:
		LOG_DBG("PINGRESP packet");
		break;

	default:
		LOG_DBG("MQTT event received %d", evt->type);
		break;
	}
}

static void broker_init(struct shell_mqtt *ctx)
{
	struct sockaddr_in *broker4 = (struct sockaddr_in *)&ctx->broker;

	broker4->sin_family = AF_INET;
	broker4->sin_port = htons(SERVER_PORT);
	zsock_inet_pton(AF_INET, SERVER_ADDR, &broker4->sin_addr);
}

static struct mqtt_utf8 password = { .utf8 = MQTT_PASSWORD };

static struct mqtt_utf8 username = { .utf8 = MQTT_USERNAME, .size = sizeof(MQTT_USERNAME) };

static void client_init(struct shell_mqtt *ctx)
{
	mqtt_client_init(&ctx->client_ctx);

	/* MQTT client configuration */
	ctx->client_ctx.broker = &ctx->broker;
	ctx->client_ctx.evt_cb = mqtt_evt_handler;
	ctx->client_ctx.client_id.utf8 = (uint8_t *)MQTT_CLIENTID;
	ctx->client_ctx.client_id.size = strlen(MQTT_CLIENTID);
	ctx->client_ctx.password = &password;
	ctx->client_ctx.user_name = &username;
	ctx->client_ctx.protocol_version = MQTT_VERSION_3_1_1;

	/* MQTT buffers configuration */
	ctx->client_ctx.rx_buf = ctx->buf.rx;
	ctx->client_ctx.rx_buf_size = sizeof(ctx->buf.rx);
	ctx->client_ctx.tx_buf = ctx->buf.tx;
	ctx->client_ctx.tx_buf_size = sizeof(ctx->buf.tx);

	/* MQTT transport configuration */
#if defined(CONFIG_MQTT_LIB_TLS)
	struct mqtt_sec_config *tls_config = &ctx->client_ctx.transport.tls.config;

	tls_config->peer_verify = TLS_PEER_VERIFY_REQUIRED;
	tls_config->cipher_list = NULL;
	tls_config->sec_tag_list = m_sec_tags;
	tls_config->sec_tag_count = ARRAY_SIZE(m_sec_tags);
#if defined(MBEDTLS_X509_CRT_PARSE_C) || defined(CONFIG_NET_SOCKETS_OFFLOAD)
	tls_config->hostname = TLS_SNI_HOSTNAME;
#else
	tls_config->hostname = NULL;
#endif /* defined(MBEDTLS_X509_CRT_PARSE_C) || \
		  defined(CONFIG_NET_SOCKETS_OFFLOAD) */
#endif /* defined(CONFIG_MQTT_LIB_TLS) */
}

static int init(const struct shell_transport *transport, const void *config,
		shell_transport_handler_t evt_handler, void *context)
{
	sh_mqtt = (struct shell_mqtt *)transport->ctx;

	memset(sh_mqtt, 0, sizeof(struct shell_mqtt));

	LOG_DBG("Initializing shell MQTT backend");

	sh_mqtt->shell_handler = evt_handler;
	sh_mqtt->shell_context = context;

	sh_mqtt->pub_data.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
	sh_mqtt->pub_data.message.topic.topic.utf8 = (uint8_t *)PUB_TOPIC;
	sh_mqtt->pub_data.message.topic.topic.size =
		strlen(sh_mqtt->pub_data.message.topic.topic.utf8);
	sh_mqtt->pub_data.dup_flag = 0U;
	sh_mqtt->pub_data.retain_flag = 0U;

	LOG_DBG("Initializing shell MQTT broker & client");
	broker_init(sh_mqtt);
	client_init(sh_mqtt);

	k_sem_init(&sh_mqtt->tx_sem, 0, 1);
	k_sem_init(&sh_mqtt->rx_sem, 0, 1);

	k_work_init_delayable(&sh_mqtt->mqtt_work, mqtt_connect_work);

	LOG_DBG("Initializing listening for network");
	net_mgmt_init_event_callback(&sh_mqtt->mgmt_cb, event_handler, EVENT_MASK);
	sh_mqtt->state = SHELL_MQTT_OP_WAIT_NET;

	return 0;
}

static int uninit(const struct shell_transport *transport)
{
	if (sh_mqtt == NULL) {
		return -ENODEV;
	}

	return 0;
}

static int enable(const struct shell_transport *transport, bool blocking)
{
	if (sh_mqtt == NULL) {
		return -ENODEV;
	}

	LOG_DBG("shell MQTT enable");

	/* Listen for network connection status */
	net_mgmt_add_event_callback(&sh_mqtt->mgmt_cb);
	net_conn_mgr_resend_status();

	return 0;
}

int shell_mqtt_send(void)
{
	int ret = 0;
	LOG_DBG("shell_mqtt_send");
	sh_mqtt->pub_data.message.payload.data = &sh_mqtt->line_out.buf[0];
	sh_mqtt->pub_data.message.payload.len = sh_mqtt->line_out.len;
	sh_mqtt->pub_data.message_id = sys_rand32_get();

	ret = mqtt_publish(&sh_mqtt->client_ctx, &sh_mqtt->pub_data);
	if (ret != 0) {
		LOG_ERR("shell_mqtt_send: %d", ret);
	}

	return ret;
}

bool is_valid_char(const uint8_t *c)
{
	if (*c >= ' ' && *c <= '~') {
		return true;
	} else if (*c == '\0' || *c == '\r' || *c == '\n') {
		return true;
	} else {
		return false;
	}
}

static int write(const struct shell_transport *transport, const void *data, size_t length,
		 size_t *cnt)
{
	int ret;
	struct shell_mqtt_line_buf *lb;

	if (sh_mqtt == NULL) {
		*cnt = 0;
		return -ENODEV;
	}

	if (sh_mqtt->state < SHELL_MQTT_OP_CONNECTED) {
		*cnt = length;
		return 0;
	}

	if (length > RX_RB_SIZE) {
		*cnt = length;
		return 0;
	}

	LOG_DBG("Write size: %d", length);

	*cnt = 0;
	lb = &sh_mqtt->line_out;

	for (int i = 0; i < length; i++) {
		if (is_valid_char((uint8_t *)data + i)) {
			memcpy(lb->buf + lb->len, (uint8_t *)data + i, 1);
			lb->len += 1;
			*cnt += 1;
		}
	}

	/* Send the data immediately if the buffer is full or line feed
	 * is recognized.
	 */
	if ((lb->buf[lb->len - 2] == '\r' && lb->buf[lb->len - 1] == '\n') ||
	    lb->len == RX_RB_SIZE) {
		if (lb->len > 2) {
			ret = shell_mqtt_send();
			if (ret != 0) {
				*cnt = length;
				return ret;
			}
		}

		sh_mqtt->line_out.len = 0;
		sh_mqtt->shell_handler(SHELL_TRANSPORT_EVT_TX_RDY, sh_mqtt->shell_context);
	}

	sh_mqtt->shell_handler(SHELL_TRANSPORT_EVT_TX_RDY, sh_mqtt->shell_context);

	return 0;
}

static int read(const struct shell_transport *transport, void *data, size_t length, size_t *cnt)
{
	if (sh_mqtt == NULL) {
		return -ENODEV;
	}

	if (sh_mqtt->state < SHELL_MQTT_OP_SUBSCRIBED) {
		*cnt = 0;
		return 0;
	}

	LOG_DBG("shell MQTT read len: %d", length);

	*cnt = ring_buf_get(&rx_rb, data, length);

	/* Inform the shell if there are still data in the rb */
	if (ring_buf_size_get(&rx_rb) > 0) {
		sh_mqtt->shell_handler(SHELL_TRANSPORT_EVT_RX_RDY, sh_mqtt->shell_context);
	}

	return 0;
}

const struct shell_transport_api shell_mqtt_transport_api = { .init = init,
							      .uninit = uninit,
							      .enable = enable,
							      .write = write,
							      .read = read };

static int enable_shell_mqtt(const struct device *arg)
{
	ARG_UNUSED(arg);

	bool log_backend = CONFIG_SHELL_MQTT_INIT_LOG_LEVEL > 0;
	uint32_t level = (CONFIG_SHELL_MQTT_INIT_LOG_LEVEL > LOG_LEVEL_DBG) ?
				       CONFIG_LOG_MAX_LEVEL :
				       CONFIG_SHELL_MQTT_INIT_LOG_LEVEL;

	return shell_init(&shell_mqtt, NULL, false, log_backend, level);
}

SYS_INIT(enable_shell_mqtt, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

/* Function is used for testing purposes */
const struct shell *shell_backend_mqtt_get_ptr(void)
{
	return &shell_mqtt;
}
