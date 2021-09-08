/*
 * Copyright (c) 2021 G-Technologies Sdn. Bhd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <shell/shell_mqtt.h>
#include <init.h>
#include <logging/log.h>
#include <random/rand32.h>
#include <string.h>
#include <stdio.h>
#include <drivers/hwinfo.h>

SHELL_MQTT_DEFINE(shell_transport_mqtt);
SHELL_DEFINE(shell_mqtt, NULL, &shell_transport_mqtt,
	     CONFIG_SHELL_BACKEND_MQTT_LOG_MESSAGE_QUEUE_SIZE,
	     CONFIG_SHELL_BACKEND_MQTT_LOG_MESSAGE_QUEUE_TIMEOUT, SHELL_FLAG_OLF_CRLF);

LOG_MODULE_REGISTER(shell_mqtt, CONFIG_SHELL_MQTT_LOG_LEVEL);

#define EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)
#define APP_CONNECT_TIMEOUT_MS 2000
#define APP_SLEEP_MSECS 500
#define MQTT_PROCESS_INTERVAL 2
#define SERVER_ADDR CONFIG_SHELL_MQTT_SERVER_ADDR
#define SERVER_PORT CONFIG_SHELL_MQTT_SERVER_PORT

#ifdef CONFIG_SHELL_MQTT_SERVER_USERNAME
#define MQTT_USERNAME CONFIG_SHELL_MQTT_SERVER_USERNAME
#else
#define MQTT_USERNAME NULL
#endif /* CONFIG_SHELL_MQTT_SERVER_USERNAME */

#ifdef SHELL_MQTT_SERVER_PASSWORD
#define MQTT_PASSWORD CONFIG_SHELL_MQTT_SERVER_PASSWORD
#else
#define MQTT_PASSWORD NULL
#endif /*SHELL_MQTT_SERVER_PASSWORD */

struct shell_mqtt *sh_mqtt;

static bool get_device_identity(char *id, int id_max_len)
{
	uint8_t hwinfo_id[DEVICE_ID_BIN_MAX_SIZE];
	ssize_t length;

	length = hwinfo_get_device_id(hwinfo_id, DEVICE_ID_BIN_MAX_SIZE);
	if (length <= 0) {
		return false;
	}

	memset(id, 0, id_max_len);
	length = bin2hex(hwinfo_id, (size_t)length, id, id_max_len - 1);

	return length > 0;
}

static void prepare_fds(struct shell_mqtt *ctx)
{
	if (ctx->client_ctx.transport.type == MQTT_TRANSPORT_NON_SECURE) {
		ctx->fds[0].fd = ctx->client_ctx.transport.tcp.sock;
	}

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
			(void)mqtt_disconnect(&ctx->client_ctx);
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
	struct mqtt_topic subs_topic = { .topic = { .utf8 = sh_mqtt->sub_topic,
						    .size = strlen(sh_mqtt->sub_topic) },
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
		LOG_ERR("mqtt_connect error: %d", rc);
		goto fail;
	}

	/* Prepare port config */
	prepare_fds(ctx);

	/* Wait for mqtt's connack */
	if (wait(APP_CONNECT_TIMEOUT_MS)) {
		ctx->state = SHELL_MQTT_OP_CONNECTING;
		rc = mqtt_input(&ctx->client_ctx);
		if (rc != 0) {
			LOG_ERR("mqtt_input error: %d", rc);
			goto fail;
		}
	}

	/* No connack, fail */
	if (ctx->state != SHELL_MQTT_OP_CONNECTED) {
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
	(void)mqtt_abort(&ctx->client_ctx);
	LOG_WRN("Retrying MQTT connect work after %d ms if network connected", APP_SLEEP_MSECS);
	ctx->state = SHELL_MQTT_OP_WAIT_NET;
	net_conn_mgr_resend_status();
}

static int shell_mqtt_send(bool is_work)
{
	int ret = 0;

	if (sh_mqtt->line_out.buf[0] == '\r' && sh_mqtt->line_out.buf[1] == '\n') {
		sh_mqtt->line_out.off = 2;
	}

	sh_mqtt->pub_data.message.payload.data = &sh_mqtt->line_out.buf[sh_mqtt->line_out.off];
	sh_mqtt->pub_data.message.payload.len = sh_mqtt->line_out.len - sh_mqtt->line_out.off;
	sh_mqtt->pub_data.message_id = sys_rand32_get();

	ret = mqtt_publish(&sh_mqtt->client_ctx, &sh_mqtt->pub_data);
	if (ret != 0) {
		LOG_ERR("MQTT publish error: %d", ret);
	}

	memset(&sh_mqtt->line_out, 0, sizeof(sh_mqtt->line_out));

	/* Arbitrary delay to not kill the session */
	if (!is_work) {
		k_msleep(100);
	}

	return ret;
}

static void mqtt_send_work(struct k_work *work)
{
	(void)shell_mqtt_send(true);
}

static void client_init(struct shell_mqtt *ctx);
static void broker_init(struct shell_mqtt *ctx);

/*
 * Network connection event handler
 */
static void event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event,
			  struct net_if *iface)
{
	if ((mgmt_event & EVENT_MASK) != mgmt_event) {
		return;
	}

	if (mgmt_event == NET_EVENT_L4_CONNECTED) {
		if (sh_mqtt->state == SHELL_MQTT_OP_WAIT_NET) {
			LOG_DBG("Network connected, connecting to MQTT broker");
			LOG_DBG("Initializing shell MQTT broker & client");
			broker_init(sh_mqtt);
			client_init(sh_mqtt);
			k_work_init_delayable(&sh_mqtt->mqtt_work, mqtt_connect_work);
			(void)k_work_reschedule(&sh_mqtt->mqtt_work, K_SECONDS(2));
			sh_mqtt->state = SHELL_MQTT_OP_CONNECTING;
		}
	} else if (mgmt_event == NET_EVENT_L4_DISCONNECTED) {
		LOG_DBG("No network connection");
		sh_mqtt->state = SHELL_MQTT_OP_WAIT_NET;

		if (sh_mqtt->state >= SHELL_MQTT_OP_CONNECTED) {
			(void)mqtt_disconnect(&sh_mqtt->client_ctx);
		}

		(void)k_work_cancel_delayable_sync(&sh_mqtt->mqtt_work, &sh_mqtt->work_sync);
		clear_fds(sh_mqtt);
	}
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
		(void)k_work_cancel_delayable_sync(&sh_mqtt->mqtt_work, &sh_mqtt->work_sync);
		(void)k_work_cancel_delayable_sync(&sh_mqtt->send_work, &sh_mqtt->work_sync);
		k_work_init_delayable(&sh_mqtt->mqtt_work, mqtt_connect_work);
		(void)k_work_reschedule(&sh_mqtt->mqtt_work, K_SECONDS(MQTT_PROCESS_INTERVAL));
		break;

	case MQTT_EVT_PUBLISH: {
		const struct mqtt_publish_param *pub = &evt->param.publish;
		uint32_t rb_free_space = ring_buf_space_get(&sh_mqtt->rx_rb);
		int bytes_read, ret;
		uint32_t size;

		LOG_DBG("MQTT publish received %d, %d bytes", evt->result,
			pub->message.payload.len);
		LOG_DBG("   id: %d, qos: %d", pub->message_id, pub->message.topic.qos);
		LOG_DBG("   item: %s", log_strdup(pub->message.topic.topic.utf8));

		/* for MQTT_QOS_0_AT_MOST_ONCE no acknowledgment needed */
		if (pub->message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE) {
			struct mqtt_puback_param puback = { .message_id = pub->message_id };

			(void)mqtt_publish_qos1_ack(client, &puback);
		}

		if (rb_free_space >= (pub->message.payload.len + sizeof("\r\n"))) {
			size = ring_buf_put_claim(&sh_mqtt->rx_rb, &sh_mqtt->rx_rb_ptr,
						  pub->message.payload.len);

			bytes_read = mqtt_read_publish_payload(client, sh_mqtt->rx_rb_ptr, size);
			if (bytes_read < 0 && bytes_read != -EAGAIN) {
				LOG_ERR("failure to read payload");
				break;
			}
			ret = ring_buf_put_finish(&sh_mqtt->rx_rb, bytes_read);
			(void)ring_buf_put(&sh_mqtt->rx_rb, "\r\n", sizeof("\r\n"));
		} else {
			LOG_ERR("RB free space: %d bytes, incoming packet: %d bytes", rb_free_space,
				pub->message.payload.len);
		}

		sh_mqtt->shell_handler(SHELL_TRANSPORT_EVT_RX_RDY, sh_mqtt->shell_context);
		break;
	}

	case MQTT_EVT_PUBACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBACK error %d", evt->result);
			break;
		}
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
#ifdef CONFIG_NET_IPV6
	struct sockaddr_in6 *broker6 = (struct sockaddr_in6 *)&ctx->broker;

	broker6->sin6_family = AF_INET6;
	broker6->sin6_port = htons(SERVER_PORT);
	(void)zsock_inet_pton(AF_INET6, SERVER_ADDR, &broker6->sin6_addr);

#ifdef CONFIG_SOCKS
	struct sockaddr_in6 *proxy6 = (struct sockaddr_in6 *)&ctx->socks5_proxy;

	proxy6->sin6_family = AF_INET6;
	proxy6->sin6_port = htons(SOCKS5_PROXY_PORT);
	(void)zsock_inet_pton(AF_INET6, SOCKS5_PROXY_ADDR, &proxy6->sin6_addr);
#endif
#else /* IPV4 */
	struct sockaddr_in *broker4 = (struct sockaddr_in *)&ctx->broker;

	broker4->sin_family = AF_INET;
	broker4->sin_port = htons(SERVER_PORT);
	(void)zsock_inet_pton(AF_INET, SERVER_ADDR, &broker4->sin_addr);
#ifdef CONFIG_SOCKS
	struct sockaddr_in *proxy4 = (struct sockaddr_in *)&ctx->socks5_proxy;

	proxy4->sin_family = AF_INET;
	proxy4->sin_port = htons(SOCKS5_PROXY_PORT);
	(void)zsock_inet_pton(AF_INET, SOCKS5_PROXY_ADDR, &proxy4->sin_addr);
#endif
#endif
}

static struct mqtt_utf8 password = { .utf8 = MQTT_PASSWORD, .size = sizeof(MQTT_PASSWORD) };

static struct mqtt_utf8 username = { .utf8 = MQTT_USERNAME, .size = sizeof(MQTT_USERNAME) };

static void client_init(struct shell_mqtt *ctx)
{
	mqtt_client_init(&ctx->client_ctx);

	/* MQTT client configuration */
	ctx->client_ctx.broker = &ctx->broker;
	ctx->client_ctx.evt_cb = mqtt_evt_handler;
	ctx->client_ctx.client_id.utf8 = (uint8_t *)sh_mqtt->device_id;
	ctx->client_ctx.client_id.size = strlen(sh_mqtt->device_id);
	ctx->client_ctx.password = &password;
	ctx->client_ctx.user_name = &username;
	ctx->client_ctx.protocol_version = MQTT_VERSION_3_1_1;

	/* MQTT buffers configuration */
	ctx->client_ctx.rx_buf = ctx->buf.rx;
	ctx->client_ctx.rx_buf_size = sizeof(ctx->buf.rx);
	ctx->client_ctx.tx_buf = ctx->buf.tx;
	ctx->client_ctx.tx_buf_size = sizeof(ctx->buf.tx);

	/* MQTT transport configuration */
#ifdef CONFIG_MQTT_LIB_WEBSOCKET
	ctx->client_ctx.transport.type = MQTT_TRANSPORT_NON_SECURE_WEBSOCKET;
#else
	ctx->client_ctx.transport.type = MQTT_TRANSPORT_NON_SECURE;
#endif

#ifdef CONFIG_MQTT_LIB_WEBSOCKET
	ctx->client_ctx.transport.websocket.config.host = SERVER_ADDR;
	ctx->client_ctx.transport.websocket.config.url = "/shell_mqtt";
	ctx->client_ctx.transport.websocket.config.tmp_buf = ctx->temp_ws_rx_buf;
	ctx->client_ctx.transport.websocket.config.tmp_buf_len = MQTT_LIB_WEBSOCKET_RECV_BUF_LEN;
	ctx->client_ctx.transport.websocket.timeout = 5 * MSEC_PER_SEC;
#endif

#ifdef CONFIG_SOCKS
	uint32_t sockaddr_size;

	if (ctx->socks5_proxy.sa_family == AF_INET) {
		sockaddr_size = sizeof(struct sockaddr_in);
	} else {
		sockaddr_size = sizeof(struct sockaddr_in6);
	}

	mqtt_client_set_proxy(ctx->client_ctx., &ctx->socks5_proxy, sockaddr_size);
#endif
}

static int init(const struct shell_transport *transport, const void *config,
		shell_transport_handler_t evt_handler, void *context)
{
	sh_mqtt = (struct shell_mqtt *)transport->ctx;

	memset(sh_mqtt, 0, sizeof(struct shell_mqtt));

	if (!get_device_identity(sh_mqtt->device_id, DEVICE_ID_HEX_MAX_SIZE)) {
		LOG_ERR("Unable to get device identity, using dummy value");
		(void)snprintf(sh_mqtt->device_id, sizeof("dummy"), "dummy");
	}

	LOG_DBG("Client ID is %s", log_strdup(sh_mqtt->device_id));

	(void)snprintf(sh_mqtt->pub_topic, MQTT_TOPIC_MAX_SIZE, "%s_tx", sh_mqtt->device_id);
	(void)snprintf(sh_mqtt->sub_topic, MQTT_TOPIC_MAX_SIZE, "%s_rx", sh_mqtt->device_id);
	LOG_DBG("Subscribing shell cmds from: %s", log_strdup(sh_mqtt->sub_topic));
	LOG_DBG("Logs will be published to: %s", log_strdup(sh_mqtt->pub_topic));

	ring_buf_init(&sh_mqtt->rx_rb, RX_RB_SIZE, sh_mqtt->rx_rb_buf);

	LOG_DBG("Initializing shell MQTT backend");

	sh_mqtt->shell_handler = evt_handler;
	sh_mqtt->shell_context = context;

	sh_mqtt->pub_data.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
	sh_mqtt->pub_data.message.topic.topic.utf8 = (uint8_t *)sh_mqtt->pub_topic;
	sh_mqtt->pub_data.message.topic.topic.size =
		strlen(sh_mqtt->pub_data.message.topic.topic.utf8);
	sh_mqtt->pub_data.dup_flag = 0U;
	sh_mqtt->pub_data.retain_flag = 0U;

	k_work_init_delayable(&sh_mqtt->mqtt_work, mqtt_connect_work);
	k_work_init_delayable(&sh_mqtt->send_work, mqtt_send_work);

	LOG_DBG("Initializing listener for network");
	net_mgmt_init_event_callback(&sh_mqtt->mgmt_cb, event_handler, EVENT_MASK);
	sh_mqtt->state = SHELL_MQTT_OP_WAIT_NET;

	return 0;
}

static int uninit(const struct shell_transport *transport)
{
	/* Not initialized yet */
	if (sh_mqtt == NULL) {
		return -ENODEV;
	}

	return 0;
}

static int enable(const struct shell_transport *transport, bool blocking)
{
	/* Not initialized yet */
	if (sh_mqtt == NULL) {
		return -ENODEV;
	}

	/* Listen for network connection status */
	net_mgmt_add_event_callback(&sh_mqtt->mgmt_cb);
	net_conn_mgr_resend_status();

	return 0;
}

static bool is_valid_char(const uint8_t *c)
{
	if (*c >= ' ' && *c <= '~') {
		return true;
	} else if (*c == '\r' || *c == '\n') {
		return true;
	} else {
		return false;
	}
}

static int write(const struct shell_transport *transport, const void *data, size_t length,
		 size_t *cnt)
{
	int ret = 0;
	struct shell_mqtt_line_buf *lb;
	size_t actual_send_len;
	bool was_running;
	lb = &sh_mqtt->line_out;

	/* Not initialized yet */
	if (sh_mqtt == NULL) {
		*cnt = 0;
		return -ENODEV;
	}

	/* Not connected to internet */
	if (sh_mqtt->state < SHELL_MQTT_OP_CONNECTED) {
		goto out;
	}

	/* Exceeds the max buffer length */
	if (length > TX_BUF_SIZE) {
		LOG_ERR("Trying to send %d bytes, exceeded %d bytes", length, TX_BUF_SIZE);
		goto out;
	}

	was_running = k_work_cancel_delayable_sync(&sh_mqtt->send_work, &sh_mqtt->work_sync);
	if (was_running && ((lb->len + length) > TX_BUF_SIZE)) {
		ret = shell_mqtt_send(false);
		if (ret != 0) {
			goto out;
		}
	}

	actual_send_len = length;

	/* This is to filter out undisplayable data for MQTT */
	for (int i = 0; i < length; i++) {
		if (is_valid_char((uint8_t *)data + i)) {
			memcpy(lb->buf + lb->len, (uint8_t *)data + i, 1);
			lb->len += 1;
		} else {
			actual_send_len--;
		}
	}

	if (lb->len) {
		k_work_reschedule(&sh_mqtt->send_work, K_MSEC(100));
	}

	/* Inform shell that it is ready for next TX */
	sh_mqtt->shell_handler(SHELL_TRANSPORT_EVT_TX_RDY, sh_mqtt->shell_context);

out:
	/* We will always assume that we sent everything */
	*cnt = length;
	return ret;
}

static int read(const struct shell_transport *transport, void *data, size_t length, size_t *cnt)
{
	/* Not initialized yet */
	if (sh_mqtt == NULL) {
		return -ENODEV;
	}

	/* Not subscribed yet */
	if (sh_mqtt->state < SHELL_MQTT_OP_SUBSCRIBED) {
		*cnt = 0;
		return 0;
	}

	*cnt = ring_buf_get(&sh_mqtt->rx_rb, data, length);

	/* Inform the shell if there are still data in the rb */
	if (ring_buf_size_get(&sh_mqtt->rx_rb) > 0) {
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

	return shell_init(&shell_mqtt, NULL, false, false, log_backend, level);
}

SYS_INIT(enable_shell_mqtt, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

/* Function is used for testing purposes */
const struct shell *shell_backend_mqtt_get_ptr(void)
{
	return &shell_mqtt;
}
