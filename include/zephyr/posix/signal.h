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
#define SIGRTMAX _NSIG

BUILD_ASSERT(CONFIG_POSIX_SIGNAL_NSIG > SIGRTMIN);

typedef struct {
	unsigned long sig[DIV_ROUND_UP(_NSIG, BITS_PER_LONG)];
} sigset_t;

typedef struct siginfo {
	uint8_t si_signo;
	/* Not implemented yet */
	/* uint8_t si_code; */
	/* uint8_t si_errno; */
	/* pid_t si_pid; */
	/* uid_t si_uid; */
	/* void *si_addr; */
	/* int si_status; */
	/* long si_band; */
	/* union sigval si_value; */

} siginfo_t;

struct sigaction {
	union {
		void (*sa_handler)(int);
		void (*sa_sigaction)(int, siginfo_t *, void *);
	} sa_fn;
	sigset_t sa_mask;
	int sa_flags;
};

#define SA_NOCLDSTOP BIT(0)
#define SA_ONSTACK   BIT(1)
#define SA_RESETHAND BIT(2)
#define SA_RESTART   BIT(3)
#define SA_SIGINFO   BIT(4)
#define SA_NOCLDWAIT BIT(5)
#define SA_NODEFER   BIT(6)

#define SIG_BLOCK   1
#define SIG_UNBLOCK 2
#define SIG_SETMASK 3

char *strsignal(int signum);
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signo);
int sigdelset(sigset_t *set, int signo);
int sigismember(const sigset_t *set, int signo);
int sigpending(sigset_t *set);
int sigaction(int sig, const struct sigaction *act, struct sigaction *oact);
int sigprocmask(int how, const sigset_t *ZRESTRICT set, sigset_t *ZRESTRICT oset);
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
