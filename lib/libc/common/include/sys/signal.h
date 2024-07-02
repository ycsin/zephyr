/*
 * Copyright (c) 2023, Meta
 * Copyright (c) 2024, Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* This had the same guard as /* zephyr/lib/libc/common/include/signal.h */
#ifndef ZEPHYR_LIB_LIBC_COMMON_INCLUDE_SYS_SIGNAL_H_
#define ZEPHYR_LIB_LIBC_COMMON_INCLUDE_SYS_SIGNAL_H_

/* exclude external sys/signal.h */
#define _SYS_SIGNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/features.h>
#include <sys/_sigset.h>
#include <zephyr/posix/sys/_pthreadtypes.h>

#if defined(_POSIX_REALTIME_SIGNALS) || (_POSIX_C_SOURCE >= 199309L) || defined(__DOXYGEN__)

#define SIGEV_NONE   1
#define SIGEV_SIGNAL 2
#define SIGEV_THREAD 3

union sigval {
	void *sival_ptr;
	int sival_int;
};

struct sigevent {
#if defined(_POSIX_THREADS) || defined(__DOXYGEN__)
	void (*sigev_notify_function)(union sigval val);
	pthread_attr_t *sigev_notify_attributes;
#endif
	union sigval sigev_value;
	int sigev_notify;
	int sigev_signo;
};

#define SI_USER    1
#define SI_QUEUE   2
#define SI_TIMER   3
#define SI_ASYNCIO 4
#define SI_MESGQ   5

typedef struct {
	int si_signo;
	int si_code;
	union sigval si_value;
} siginfo_t;

#endif

struct sigaction {
	void (*sa_handler)(int signno);
#if defined(_POSIX_REALTIME_SIGNALS)
	void (*sa_sigaction)(int signo, void *info, void *context);
#endif
	sigset_t sa_mask;
	int sa_flags;
};

#if defined(_POSIX_C_SOURCE) || defined(__DOXYGEN__)
#define SIG_BLOCK   0
#define SIG_SETMASK 1
#define SIG_UNBLOCK 2

int sigprocmask(int how, const sigset_t *ZRESTRICT set, sigset_t *ZRESTRICT oset);
#endif

#if (_POSIX_C_SOURCE >= 199506L) || defined(__DOXYGEN__)
int pthread_sigmask(int how, const sigset_t *ZRESTRICT set, sigset_t *ZRESTRICT oset);
#endif

#if defined(_POSIX_C_SOURCE) || defined(__DOXYGEN__)
int kill(pid_t pid, int sig);
int pause(void);
int sigaction(int sig, const struct sigaction *ZRESTRICT act, struct sigaction *ZRESTRICT oact);
int sigpending(sigset_t *set);
int sigsuspend(const sigset_t *sigmask);
int sigwait(const sigset_t *ZRESTRICT set, int *ZRESTRICT signo);
char *strsignal(int signum);
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signo);
int sigdelset(sigset_t *set, int signo);
int sigismember(const sigset_t *set, int signo);
#endif

#define SIGHUP    1  /**< Hangup */
#define SIGINT    2  /**< Interrupt */
#define SIGQUIT   3  /**< Quit */
#define SIGILL    4  /**< Illegal instruction */
#define SIGTRAP   5  /**< Trace/breakpoint trap */
#define SIGABRT   6  /**< Aborted */
#define SIGBUS    7  /**< Bus error */
#define SIGFPE    8  /**< Arithmetic exception */
#define SIGKILL   9  /**< Killed */
#define SIGUSR1   10 /**< User-defined signal 1 */
#define SIGSEGV   11 /**< Invalid memory reference */
#define SIGUSR2   12 /**< User-defined signal 2 */
#define SIGPIPE   13 /**< Broken pipe */
#define SIGALRM   14 /**< Alarm clock */
#define SIGTERM   15 /**< Terminated */
/* 16 not used */
#define SIGCHLD   17 /**< Child status changed */
#define SIGCONT   18 /**< Continued */
#define SIGSTOP   19 /**< Stop executing */
#define SIGTSTP   20 /**< Stopped */
#define SIGTTIN   21 /**< Stopped (read) */
#define SIGTTOU   22 /**< Stopped (write) */
#define SIGURG    23 /**< Urgent I/O condition */
#define SIGXCPU   24 /**< CPU time limit exceeded */
#define SIGXFSZ   25 /**< File size limit exceeded */
#define SIGVTALRM 26 /**< Virtual timer expired */
#define SIGPROF   27 /**< Profiling timer expired */
/* 28 not used */
#define SIGPOLL   29 /**< Pollable event occurred */
/* 30 not used */
#define SIGSYS    31 /**< Bad system call */

#define SIGRTMIN 32
#define SIGRTMAX (SIGRTMIN + RTSIG_MAX)

BUILD_ASSERT(RTSIG_MAX >= 0);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_LIB_LIBC_COMMON_INCLUDE_SYS_SIGNAL_H_ */
