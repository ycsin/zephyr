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

#define RX_RB_SIZE 512
#define APP_MQTT_BUFFER_SIZE 64

extern const struct shell_transport_api shell_mqtt_transport_api;

/** Line buffer structure. */
struct shell_mqtt_line_buf {
	/** Line buffer. */
	char buf[RX_RB_SIZE];

	/** Current line length. */
	uint16_t len;
};

/** MQTT-based shell transport. */
struct shell_mqtt {
	/** Handler function registered by shell. */
	shell_transport_handler_t shell_handler;

	struct shell_mqtt_line_buf line_out;

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

	struct zsock_pollfd fds[1];
	int nfds;

	struct mqtt_publish_param pub_data;

	struct net_mgmt_event_callback mgmt_cb;

	struct k_sem tx_sem;
	struct k_sem rx_sem;

	/* work */
	struct k_work_delayable mqtt_work;
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
