/*
 * Copyright (c) 2023, Meta
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <threads.h>
#include <pthread.h>

#include <zephyr/kernel.h>

void call_once(once_flag *flag, void (*func)(void))
{
	(void)pthread_once((pthread_once_t *)flag, func);
}
