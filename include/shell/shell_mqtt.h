/*
 * Copyright (c) 2021 G-Technologies Sdn. Bhd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SHELL_MQTT_H__
#define SHELL_MQTT_H__

#include <shell/shell.h>

#include <net/socket.h>
#include <net/net_mgmt.h>
#include <net/net_event.h>
#include <net/net_conn_mgr.h>
#include <net/mqtt.h>
#include <sys/ring_buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RX_RB_SIZE CONFIG_SHELL_MQTT_RX_BUF_SIZE
#define TX_BUF_SIZE CONFIG_SHELL_MQTT_TX_BUF_SIZE
#define APP_MQTT_BUFFER_SIZE 64
#define DEVICE_ID_BIN_MAX_SIZE 3
#define DEVICE_ID_HEX_MAX_SIZE ((DEVICE_ID_BIN_MAX_SIZE * 2) + 1)
#define MQTT_TOPIC_MAX_SIZE DEVICE_ID_HEX_MAX_SIZE + 3

#ifdef CONFIG_MQTT_LIB_WEBSOCKET
/* Making RX buffer large enough that the full IPv6 packet can fit into it */
#define MQTT_LIB_WEBSOCKET_RECV_BUF_LEN 1280
#endif

extern const struct shell_transport_api shell_mqtt_transport_api;

/** Line buffer structure. */
struct shell_mqtt_line_buf {
	/** Line buffer. */
	char buf[TX_BUF_SIZE];

	/* Buffer offset */
	uint16_t off;

	/** Current line length. */
	uint16_t len;
};

/** MQTT-based shell transport. */
struct shell_mqtt {
	char device_id[DEVICE_ID_HEX_MAX_SIZE];
	char sub_topic[MQTT_TOPIC_MAX_SIZE];
	char pub_topic[MQTT_TOPIC_MAX_SIZE];

	/** Handler function registered by shell. */
	shell_transport_handler_t shell_handler;

	struct shell_mqtt_line_buf line_out;

	struct ring_buf rx_rb;
	uint8_t rx_rb_buf[RX_RB_SIZE];
	uint8_t *rx_rb_ptr;

	/** Context registered by shell. */
	void *shell_context;

	/* The mqtt client struct */
	struct mqtt_client client_ctx;

	/* Buffers for MQTT client. */
	struct buffer {
		uint8_t rx[APP_MQTT_BUFFER_SIZE];
		uint8_t tx[APP_MQTT_BUFFER_SIZE];
	} buf;

	/* MQTT Broker details. */
	struct sockaddr_storage broker;

#ifdef CONFIG_SOCKS
	struct sockaddr socks5_proxy;
#endif
#ifdef CONFIG_MQTT_LIB_WEBSOCKET
	uint8_t temp_ws_rx_buf[MQTT_LIB_WEBSOCKET_RECV_BUF_LEN];
#endif

	struct zsock_pollfd fds[1];
	int nfds;

	struct mqtt_publish_param pub_data;

	struct net_mgmt_event_callback mgmt_cb;

	/* work */
	struct k_work_delayable mqtt_work;
	struct k_work_delayable send_work;
	struct k_work_sync work_sync;

	enum op_state {
		/* Waiting for internet connection */
		SHELL_MQTT_OP_WAIT_NET,
		/* Connecting to MQTT broker */
		SHELL_MQTT_OP_CONNECTING,
		/* Connected to MQTT broker */
		SHELL_MQTT_OP_CONNECTED,
		/* Subscribed to MQTT broker */
		SHELL_MQTT_OP_SUBSCRIBED,
	} state;
};

#define SHELL_MQTT_DEFINE(_name)                                                                   \
	static struct shell_mqtt _name##_shell_mqtt;                                               \
	struct shell_transport _name = { .api = &shell_mqtt_transport_api,                         \
					 .ctx = (struct shell_mqtt *)&_name##_shell_mqtt }

/**
 * @brief This function provides pointer to shell mqtt backend instance.
 *
 * Function returns pointer to the shell mqtt instance. This instance can be
 * next used with shell_execute_cmd function in order to test commands behavior.
 *
 * @returns Pointer to the shell instance.
 */
const struct shell *shell_backend_mqtt_get_ptr(void);

#ifdef __cplusplus
}
#endif

#endif /* SHELL_MQTT_H__ */
