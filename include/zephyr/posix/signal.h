/*
 * Copyright (c) 2018 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_POSIX_SIGNAL_H_
#define ZEPHYR_INCLUDE_POSIX_SIGNAL_H_

#include "posix_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_POSIX_SIGNAL
#define _NSIG     CONFIG_POSIX_SIGNAL_NSIG

#define SIGHUP    1
#define SIGINT    2
#define SIGQUIT   3
#define SIGILL    4
#define SIGTRAP   5
#define SIGABRT   6
#define SIGBUS    7
#define SIGFPE    8
#define SIGKILL   9
#define SIGUSR1   10
#define SIGSEGV   11
#define SIGUSR2   12
#define SIGPIPE   13
#define SIGALRM   14
#define SIGTERM   15
/* 16 not used */
#define SIGCHLD   17
#define SIGCONT   18
#define SIGSTOP   19
#define SIGTSTP   20
#define SIGTTIN   21
#define SIGTTOU   22
#define SIGURG    23
#define SIGXCPU   24
#define SIGXFSZ   25
#define SIGVTALRM 26
#define SIGPROF   27
/* 28 not used */
#define SIGPOLL   29
/* 30 not used */
#define SIGSYS    31
#define SIGUNUSED 31

#define SIGRTMIN 32
#define SIGRTMAX _NSIG

BUILD_ASSERT(CONFIG_POSIX_SIGNAL_NSIG > SIGRTMIN);

typedef struct {
	unsigned long sig[DIV_ROUND_UP(_NSIG, BITS_PER_LONG)];
} sigset_t;
#endif /* CONFIG_POSIX_SIGNAL */

#ifndef SIGEV_NONE
#define SIGEV_NONE 1
#endif

#ifndef SIGEV_SIGNAL
#define SIGEV_SIGNAL 2
#endif

#ifndef SIGEV_THREAD
#define SIGEV_THREAD 3
#endif

typedef int	sig_atomic_t;		/* Atomic entity type (ANSI) */

typedef union sigval {
	int sival_int;
	void *sival_ptr;
} sigval;

typedef struct sigevent {
	int sigev_notify;
	int sigev_signo;
	sigval sigev_value;
	void (*sigev_notify_function)(sigval val);
	#ifdef CONFIG_PTHREAD_IPC
	pthread_attr_t *sigev_notify_attributes;
	#endif
} sigevent;

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_POSIX_SIGNAL_H_ */
