/* Kionix KX022 3-axis accelerometer driver
 *
 * Copyright (c) 2021 G-Technologies Sdn. Bhd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <device.h>
#include <drivers/i2c.h>
#include <sys/__assert.h>
#include <sys/util.h>
#include <kernel.h>
#include <drivers/sensor.h>
#include <logging/log.h>
#include "kx022.h"

LOG_MODULE_DECLARE(KX022, CONFIG_SENSOR_LOG_LEVEL);

static void kx022_gpio_callback(const struct device * dev,
                                struct gpio_callback *cb,
                                uint32_t              pins)
{
//     struct kx022_data *data = CONTAINER_OF(cb, struct kx022_data, gpio_cb);
//     const struct kx022_config *cfg = data->dev->config;

//     ARG_UNUSED(pins);

//     gpio_pin_interrupt_configure(data->gpio, cfg->int2_pin, GPIO_INT_DISABLE);

// #if defined(CONFIG_KX022_TRIGGER_OWN_THREAD)
//     k_sem_give(&data->trig_sem);
// #elif defined(CONFIG_KX022_TRIGGER_GLOBAL_THREAD)
//     k_work_submit(&data->work);
// #endif
}

static void kx022_handle_drdy_int(const struct device *dev)
{
    // struct kx022_data *data = dev->data;

    // if (data->data_ready_handler != NULL)
    // {
    //     data->data_ready_handler(dev, &data->data_ready_trigger);
    // }
}

static void kx022_handle_int(const struct device *dev)
{
    // struct kx022_data *        data = dev->data;
    // const struct kx022_config *cfg  = dev->config;
    // uint8_t                    status;

    // if (data->hw_tf->read_reg(data, KX022_REG_STATUS, &status) < 0)
    // {
    //     LOG_ERR("status reading error");
    //     return;
    // }

    // if (status & KX022_INT_DRDY)
    // {
    //     kx022_handle_drdy_int(dev);
    // }

    // gpio_pin_interrupt_configure(data->gpio,
    //                              cfg->int2_pin,
    //                              GPIO_INT_EDGE_TO_ACTIVE);
}

#ifdef CONFIG_KX022_TRIGGER_OWN_THREAD
static void kx022_thread(struct kx022_data *data)
{
    // while (1)
    // {
    //     k_sem_take(&data->trig_sem, K_FOREVER);
    //     kx022_handle_int(data->dev);
    // }
}
#endif

#ifdef CONFIG_KX022_TRIGGER_GLOBAL_THREAD
static void kx022_work_cb(struct k_work *work)
{
    // struct kx022_data *data = CONTAINER_OF(work, struct kx022_data, work);

    // kx022_handle_int(data->dev);
}
#endif

static int kx022_init_interrupt(const struct device *dev)
{
    // struct kx022_data *data = dev->data;

    // /* Enable latched mode */
    // if (data->hw_tf->update_reg(data,
    //                             KX022_REG_CNTL3,
    //                             KX022_MASK_LIR,
    //                             (1 << KX022_SHIFT_LIR)) < 0)
    // {
    //     LOG_ERR("Could not enable LIR mode.");
    //     return -EIO;
    // }

    // /* enable data-ready interrupt */
    // if (data->hw_tf->update_reg(data,
    //                             KX022_REG_CNTL1,
    //                             KX022_MASK_CNTL1_DRDYE,
    //                             (uint8_t) true) < 0)
    // {
    //     LOG_ERR("Could not enable data-ready interrupt.");
    //     return -EIO;
    // }

    // return 0;
}

int kx022_trigger_init(const struct device *dev)
{
//     struct kx022_data *        data = dev->data;
//     const struct kx022_config *cfg  = dev->config;

//     /* setup data ready gpio interrupt */
//     data->gpio = device_get_binding(cfg->int2_port);
//     if (data->gpio == NULL)
//     {
//         LOG_ERR("Cannot get pointer to %s device.", cfg->int2_port);
//         return -EINVAL;
//     }

//     gpio_pin_configure(data->gpio, cfg->int2_pin, GPIO_INPUT | cfg->int2_flags);

//     gpio_init_callback(&data->gpio_cb, kx022_gpio_callback, BIT(cfg->int2_pin));

//     if (gpio_add_callback(data->gpio, &data->gpio_cb) < 0)
//     {
//         LOG_ERR("Could not set gpio callback.");
//         return -EIO;
//     }
//     data->dev = dev;

// #if defined(CONFIG_KX022_TRIGGER_OWN_THREAD)
//     k_sem_init(&data->trig_sem, 0, UINT_MAX);

//     k_thread_create(&data->thread,
//                     data->thread_stack,
//                     CONFIG_KX022_THREAD_STACK_SIZE,
//                     (k_thread_entry_t)kx022_thread,
//                     data,
//                     NULL,
//                     NULL,
//                     K_PRIO_COOP(CONFIG_KX022_THREAD_PRIORITY),
//                     0,
//                     K_NO_WAIT);
// #elif defined(CONFIG_KX022_TRIGGER_GLOBAL_THREAD)
//     data->work.handler = kx022_work_cb;
// #endif

//     gpio_pin_interrupt_configure(data->gpio,
//                                  cfg->int2_pin,
//                                  GPIO_INT_EDGE_TO_ACTIVE);

//     return 0;
}

int kx022_trigger_set(const struct device *        dev,
                      const struct sensor_trigger *trig,
                      sensor_trigger_handler_t     handler)
{
    // struct kx022_data *        data = dev->data;
    // const struct kx022_config *cfg  = dev->config;
    // uint8_t                    buf[6];

    // __ASSERT_NO_MSG(trig->type == SENSOR_TRIG_DATA_READY);

    // gpio_pin_interrupt_configure(data->gpio, cfg->int2_pin, GPIO_INT_DISABLE);

    // data->data_ready_handler = handler;
    // if (handler == NULL)
    // {
    //     LOG_WRN("kx022: no handler");
    //     return 0;
    // }

    // /* re-trigger lost interrupt */
    // if (data->hw_tf->read_data(data, KX022_REG_OUTX_L, buf, sizeof(buf)) < 0)
    // {
    //     LOG_ERR("status reading error");
    //     return -EIO;
    // }

    // data->data_ready_trigger = *trig;

    // kx022_init_interrupt(dev);
    // gpio_pin_interrupt_configure(data->gpio,
    //                              cfg->int2_pin,
    //                              GPIO_INT_EDGE_TO_ACTIVE);

    // return 0;
}
