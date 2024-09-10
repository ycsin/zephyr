/*
 * Copyright (c) 2017 Oticon A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _NATIVE_POSIX_IRQ_CTRL_H
#define _NATIVE_POSIX_IRQ_CTRL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void hw_irq_ctrl_init(void);
void hw_irq_ctrl_cleanup(void);
void hw_irq_ctrl_timer_triggered(void);

void hw_irq_ctrl_set_cur_prio(int new);
int hw_irq_ctrl_get_cur_prio(void);

void hw_irq_ctrl_prio_set(uint32_t irq, unsigned int prio);
uint8_t hw_irq_ctrl_get_prio(uint32_t irq);

int hw_irq_ctrl_get_highest_prio_irq(void);
uint32_t hw_irq_ctrl_get_current_lock(void);
uint32_t hw_irq_ctrl_change_lock(uint32_t new_lock);
uint64_t hw_irq_ctrl_get_irq_status(void);
void hw_irq_ctrl_disable_irq(uint32_t irq);
int hw_irq_ctrl_is_irq_enabled(uint32_t irq);
void hw_irq_ctrl_clear_irq(uint32_t irq);
void hw_irq_ctrl_enable_irq(uint32_t irq);
void hw_irq_ctrl_set_irq(uint32_t irq);
void hw_irq_ctrl_raise_im(uint32_t irq);
void hw_irq_ctrl_raise_im_from_sw(uint32_t irq);


#define N_IRQS 32

#ifdef __cplusplus
}
#endif

#endif /* _NATIVE_POSIX_IRQ_CTRL_H */
