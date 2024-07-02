/*
 * Copyright (c) 2018 Intel Corporation
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* This had the same guard as /* zephyr/lib/libc/common/include/sys/signal.h */
#ifndef ZEPHYR_LIB_LIBC_COMMON_INCLUDE_SIGNAL_H_
#define ZEPHYR_LIB_LIBC_COMMON_INCLUDE_SIGNAL_H_

#include <sys/features.h>
#include <sys/signal.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SIG_DFL ((void *)0)
#define SIG_IGN ((void *)1)
#define SIG_ERR ((void *)-1)

typedef int	sig_atomic_t;		/* Atomic entity type (ANSI) */

int raise(int signo);
void (*signal(int signo, void (*)(int signo)))(int signo);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_LIB_LIBC_COMMON_INCLUDE_SIGNAL_H_ */
