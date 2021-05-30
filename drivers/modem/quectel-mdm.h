/*
 * Copyright (c) 2021 G-Technologies Sdn. Bhd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef QUECTEL_MDM_H
#define QUECTEL_MDM_H

#include <device.h>

/* pin settings */
enum mdm_control_pins {
#if DT_INST_NODE_HAS_PROP(0, power_gpios)
	MDM_PWR,
#endif
#if DT_INST_NODE_HAS_PROP(0, power_key_gpios)
	MDM_PWR_KEY,
#endif
#if DT_INST_NODE_HAS_PROP(0, reset_gpios)
	MDM_RST,
#endif
#if DT_INST_NODE_HAS_PROP(0, dtr_gpios)
	MDM_DTR,
#endif
};

/* Modem pins - Power, Reset & others. */
static struct modem_pin modem_pins[] = {
#if DT_INST_NODE_HAS_PROP(0, power_gpios)
	/* MDM_POWER SUPPLY */
	MODEM_PIN(DT_INST_GPIO_LABEL(0, power_gpios),
		  DT_INST_GPIO_PIN(0, power_gpios),
		  DT_INST_GPIO_FLAGS(0, power_gpios) | GPIO_OUTPUT_LOW),
#endif

#if DT_INST_NODE_HAS_PROP(0, power_key_gpios)
	/* MDM_POWER_KEY */
	MODEM_PIN(DT_INST_GPIO_LABEL(0, power_key_gpios),
		  DT_INST_GPIO_PIN(0, power_key_gpios),
		  DT_INST_GPIO_FLAGS(0, power_key_gpios) | GPIO_OUTPUT_LOW),
#endif

#if DT_INST_NODE_HAS_PROP(0, reset_gpios)
	/* MDM_RESET */
	MODEM_PIN(DT_INST_GPIO_LABEL(0, reset_gpios),
		  DT_INST_GPIO_PIN(0, reset_gpios),
		  DT_INST_GPIO_FLAGS(0, reset_gpios) | GPIO_OUTPUT_LOW),
#endif

#if DT_INST_NODE_HAS_PROP(0, dtr_gpios)
	/* MDM_DTR */
	MODEM_PIN(DT_INST_GPIO_LABEL(0, dtr_gpios),
		  DT_INST_GPIO_PIN(0, dtr_gpios),
		  DT_INST_GPIO_FLAGS(0, dtr_gpios) | GPIO_OUTPUT_LOW),
#endif
};

#if DT_INST_NODE_HAS_PROP(0, power_gpios)
static void enable_power(struct modem_context *mctx)
{
	/* Make sure the power is off before we turn it on */
	modem_pin_write(mctx, MDM_PWR, 0);
	k_sleep(K_SECONDS(2));

	modem_pin_write(mctx, MDM_PWR, 1);

	/* Wait for the power to stabilize */
	k_sleep(K_SECONDS(2));
}

static void disable_power(struct modem_context *mctx)
{
	modem_pin_write(mctx, MDM_PWR, 0);
}
#endif

#if DT_INST_NODE_HAS_PROP(0, power_key_gpios)
static void press_power_key(struct modem_context *mctx, k_timeout_t dur)
{
	modem_pin_write(mctx, MDM_PWR_KEY, 1);
	k_sleep(dur);
	modem_pin_write(mctx, MDM_PWR_KEY, 0);
}

#if DT_INST_NODE_HAS_PROP(0, power_key_on_ms)
#define power_key_on(mctx) press_power_key(mctx, K_MSEC(DT_INST_PROP(0, power_key_on_ms)))
#endif
#if DT_INST_NODE_HAS_PROP(0, power_key_off_ms)
#define power_key_off(mctx) press_power_key(mctx, K_MSEC(DT_INST_PROP(0, power_key_off_ms)))
#endif
#endif /* DT_INST_NODE_HAS_PROP(0, power_key_gpios) */

#if DT_INST_NODE_HAS_PROP(0, dtr_gpios)
#define hw_enable_sleep(mctx) modem_pin_write(mctx, MDM_DTR, 1)
#define hw_disable_sleep(mctx) modem_pin_write(mctx, MDM_DTR, 0)
#endif

#endif /* QUECTEL_MDM_H */
