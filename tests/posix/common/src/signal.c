/*
 * Copyright (c) 2023 Meta
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "posix_internal.h"
#include "signal_internal.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

ZTEST(posix_apis, test_signal_emptyset)
{
	sigset_t set;

	for (int i = 0; i < ARRAY_SIZE(set.sig); i++) {
		set.sig[i] = -1;
	}

	zassert_ok(sigemptyset(&set));

	for (int i = 0; i < ARRAY_SIZE(set.sig); i++) {
		zassert_equal(set.sig[i], 0u, "set.sig[%d] is not empty: 0x%lx", i, set.sig[i]);
	}
}

ZTEST(posix_apis, test_signal_fillset)
{
	sigset_t set = (sigset_t){0};

	zassert_ok(sigfillset(&set));

	for (int i = 0; i < ARRAY_SIZE(set.sig); i++) {
		zassert_equal(set.sig[i], -1, "set.sig[%d] is not filled: 0x%lx", i, set.sig[i]);
	}
}

ZTEST(posix_apis, test_signal_addset_oor)
{
	sigset_t set = {0};

	zassert_equal(sigaddset(&set, -1), -1, "rc should be -1");
	zassert_equal(errno, EINVAL, "errno should be %s", "EINVAL");

	zassert_equal(sigaddset(&set, 0), -1, "rc should be -1");
	zassert_equal(errno, EINVAL, "errno should be %s", "EINVAL");

	zassert_equal(sigaddset(&set, _NSIG + 1), -1, "rc should be -1");
	zassert_equal(errno, EINVAL, "errno should be %s", "EINVAL");
}

ZTEST(posix_apis, test_signal_addset)
{
	int signo;
	sigset_t set = (sigset_t){0};
	sigset_t target = (sigset_t){0};

	signo = SIGHUP;
	zassert_ok(sigaddset(&set, signo));
	WRITE_BIT(target.sig[0], signo, 1);
	for (int i = 0; i < ARRAY_SIZE(set.sig); i++) {
		zassert_equal(set.sig[i], target.sig[i],
			      "set.sig[%d of %d] has content: %lx, expected %lx", i,
			      ARRAY_SIZE(set.sig), set.sig[i], target.sig[i]);
	}

	signo = SIGSYS;
	zassert_ok(sigaddset(&set, signo));
	WRITE_BIT(target.sig[0], signo, 1);
	for (int i = 0; i < ARRAY_SIZE(set.sig); i++) {
		zassert_equal(set.sig[i], target.sig[i],
			      "set.sig[%d of %d] has content: %lx, expected %lx", i,
			      ARRAY_SIZE(set.sig), set.sig[i], target.sig[i]);
	}

	signo = SIGRTMIN; /* >=32, will be in the second sig set for 32bit */
	zassert_ok(sigaddset(&set, signo));
#ifdef CONFIG_64BIT
	WRITE_BIT(target.sig[0], signo, 1);
#else /* 32BIT */
	WRITE_BIT(target.sig[1], (signo)-BITS_PER_LONG, 1);
#endif
	for (int i = 0; i < ARRAY_SIZE(set.sig); i++) {
		zassert_equal(set.sig[i], target.sig[i],
			      "set.sig[%d of %d] has content: %lx, expected %lx", i,
			      ARRAY_SIZE(set.sig), set.sig[i], target.sig[i]);
	}

	signo = SIGRTMAX; /* >=32, will be in the second sig set for 32bit */
	zassert_ok(sigaddset(&set, signo));
#ifdef CONFIG_64BIT
	WRITE_BIT(target.sig[0], signo, 1);
#else /* 32BIT */
	WRITE_BIT(target.sig[1], ((signo)-BITS_PER_LONG), 1);
#endif
	for (int i = 0; i < ARRAY_SIZE(set.sig); i++) {
		zassert_equal(set.sig[i], target.sig[i],
			      "set.sig[%d of %d] has content: %lx, expected %lx", i,
			      ARRAY_SIZE(set.sig), set.sig[i], target.sig[i]);
	}
}

ZTEST(posix_apis, test_signal_delset_oor)
{
	sigset_t set;

	zassert_equal(sigdelset(&set, -1), -1, "rc should be -1");
	zassert_equal(errno, EINVAL, "errno should be %s", "EINVAL");

	zassert_equal(sigdelset(&set, 0), -1, "rc should be -1");
	zassert_equal(errno, EINVAL, "errno should be %s", "EINVAL");

	zassert_equal(sigdelset(&set, _NSIG + 1), -1, "rc should be -1");
	zassert_equal(errno, EINVAL, "errno should be %s", "EINVAL");
}

ZTEST(posix_apis, test_signal_delset)
{
	int signo;
	sigset_t set = (sigset_t){0};
	sigset_t target = (sigset_t){0};

	signo = SIGHUP;
	zassert_ok(sigdelset(&set, signo));
	WRITE_BIT(target.sig[0], signo, 0);
	for (int i = 0; i < ARRAY_SIZE(set.sig); i++) {
		zassert_equal(set.sig[i], target.sig[i],
			      "set.sig[%d of %d] has content: %lx, expected %lx", i,
			      ARRAY_SIZE(set.sig), set.sig[i], target.sig[i]);
	}

	signo = SIGSYS;
	zassert_ok(sigdelset(&set, signo));
	WRITE_BIT(target.sig[0], signo, 0);
	for (int i = 0; i < ARRAY_SIZE(set.sig); i++) {
		zassert_equal(set.sig[i], target.sig[i],
			      "set.sig[%d of %d] has content: %lx, expected %lx", i,
			      ARRAY_SIZE(set.sig), set.sig[i], target.sig[i]);
	}

	signo = SIGRTMIN; /* >=32, will be in the second sig set for 32bit */
	zassert_ok(sigdelset(&set, signo));
#ifdef CONFIG_64BIT
	WRITE_BIT(target.sig[0], signo, 0);
#else /* 32BIT */
	WRITE_BIT(target.sig[1], (signo)-BITS_PER_LONG, 0);
#endif
	for (int i = 0; i < ARRAY_SIZE(set.sig); i++) {
		zassert_equal(set.sig[i], target.sig[i],
			      "set.sig[%d of %d] has content: %lx, expected %lx", i,
			      ARRAY_SIZE(set.sig), set.sig[i], target.sig[i]);
	}

	signo = SIGRTMAX; /* >=32, will be in the second sig set for 32bit */
	zassert_ok(sigdelset(&set, signo));
#ifdef CONFIG_64BIT
	WRITE_BIT(target.sig[0], signo, 0);
#else /* 32BIT */
	WRITE_BIT(target.sig[1], (signo)-BITS_PER_LONG, 0);
#endif
	for (int i = 0; i < ARRAY_SIZE(set.sig); i++) {
		zassert_equal(set.sig[i], target.sig[i],
			      "set.sig[%d of %d] has content: %lx, expected %lx", i,
			      ARRAY_SIZE(set.sig), set.sig[i], target.sig[i]);
	}
}

ZTEST(posix_apis, test_signal_ismember_oor)
{
	sigset_t set = {0};

	zassert_equal(sigismember(&set, -1), -1, "rc should be -1");
	zassert_equal(errno, EINVAL, "errno should be %s", "EINVAL");

	zassert_equal(sigismember(&set, 0), -1, "rc should be -1");
	zassert_equal(errno, EINVAL, "errno should be %s", "EINVAL");

	zassert_equal(sigismember(&set, _NSIG + 1), -1, "rc should be -1");
	zassert_equal(errno, EINVAL, "errno should be %s", "EINVAL");
}

ZTEST(posix_apis, test_signal_ismember)
{
	sigset_t set;

#ifdef CONFIG_64BIT
	set.sig[0] = BIT(SIGHUP) | BIT(SIGSYS) | BIT(SIGRTMIN) | BIT(SIGRTMAX);
#else /* 32BIT */
	set.sig[0] = BIT(SIGHUP) | BIT(SIGSYS);
	set.sig[1] = BIT((SIGRTMIN)-BITS_PER_LONG) | BIT((SIGRTMAX)-BITS_PER_LONG);
#endif

	zassert_equal(sigismember(&set, SIGHUP), 1, "%s expected to be member", "SIGHUP");
	zassert_equal(sigismember(&set, SIGRTMIN), 1, "%s expected to be member", "SIGRTMIN");
	zassert_equal(sigismember(&set, SIGRTMIN), 1, "%s expected to be member", "SIGRTMIN");
	zassert_equal(sigismember(&set, SIGRTMAX), 1, "%s expected to be member", "SIGRTMAX");

	zassert_equal(sigismember(&set, SIGKILL), 0, "%s not expected to be member", "SIGKILL");
	zassert_equal(sigismember(&set, SIGTERM), 0, "%s not expected to be member", "SIGTERM");
}

ZTEST(posix_apis, test_signal_strsignal)
{
	char buf[sizeof("RT signal xx")] = {0};

	zassert_mem_equal(strsignal(-1), "Invalid signal", sizeof("Invalid signal"));
	zassert_mem_equal(strsignal(0), "Invalid signal", sizeof("Invalid signal"));
	zassert_mem_equal(strsignal(_NSIG + 1), "Invalid signal", sizeof("Invalid signal"));

	zassert_mem_equal(strsignal(30), "Signal 30", sizeof("Signal 30"));
	snprintf(buf, sizeof(buf), "RT signal %d", SIGRTMIN - SIGRTMIN);
	zassert_mem_equal(strsignal(SIGRTMIN), buf, strlen(buf));
	snprintf(buf, sizeof(buf), "RT signal %d", SIGRTMAX - SIGRTMIN);
	zassert_mem_equal(strsignal(SIGRTMAX), buf, strlen(buf));

#ifdef CONFIG_POSIX_SIGNAL_STRING_FULL
	zassert_mem_equal(strsignal(SIGHUP), "Hangup", sizeof("Hangup"));
	zassert_mem_equal(strsignal(SIGSYS), "Bad system call", sizeof("Bad system call"));
#else
	zassert_mem_equal(strsignal(SIGHUP), "SIGHUP", sizeof("SIGHUP"));
	zassert_mem_equal(strsignal(SIGSYS), "SIGSYS", sizeof("SIGSYS"));
#endif
}

static void *sigpending_test_fn(void *arg)
{
	sigset_t set = (sigset_t){0};
	sigset_t target = (sigset_t){0};
	struct posix_thread *t;
	struct sig_pending sig[4];

	ARG_UNUSED(arg);

	/* Keep the signal lower than 32 to work for both 32 & 64 bit */
	t = (struct posix_thread *)CONTAINER_OF(k_current_get(), struct posix_thread, thread);
	for (int i = 0; i < ARRAY_SIZE(sig); i++) {
		sig[i].info.si_signo = SIGTERM + i;
		WRITE_BIT(target.sig[0], (SIGTERM + i), 1);
		sys_slist_append((&t->sigpending_list), &sig[i].node);
	}

	zassert_ok(sigpending(&set));
	zassert_equal(set.sig[0], target.sig[0], "%lx", set.sig[0]);

	return NULL;
}

#define STACK_SIZE K_THREAD_STACK_LEN(1024)
static K_THREAD_STACK_ARRAY_DEFINE(thread_stack, 1, STACK_SIZE);
ZTEST(posix_apis, test_signal_pending)
{
	void *retval;
	static pthread_t pthread;
	static pthread_attr_t pthread_attr;
	const struct sched_param param = {
		.sched_priority = sched_get_priority_max(SCHED_FIFO),
	};

	zassert_ok(pthread_attr_init(&pthread_attr));
	zassert_ok(pthread_attr_setstack(&pthread_attr, thread_stack, STACK_SIZE));
	zassert_ok(pthread_attr_setschedpolicy(&pthread_attr, SCHED_FIFO));
	zassert_ok(pthread_attr_setschedparam(&pthread_attr, &param));
	zassert_ok(pthread_create(&pthread, &pthread_attr, sigpending_test_fn, NULL));
	zassert_ok(pthread_join(pthread, &retval));
}

static void *sigprocmask_test_fn(void *arg)
{
	sigset_t set = (sigset_t){0};
	sigset_t oset = (sigset_t){0};
	sigset_t last_set = (sigset_t){0};
	// sigset_t target_oset = (sigset_t){0};
	struct posix_thread *t;

	t = (struct posix_thread *)CONTAINER_OF(k_current_get(), struct posix_thread, thread);
	zassert_equal(t->sigprocmask.sig[0], (sigset_t){0}.sig[0], "should initialize to empty");

	set.sig[0] = BIT(SIGTERM);
	zassert_equal(sigprocmask(42, &set, &oset), -1, "should fail as `how` is out of range");
	zassert_equal(errno, EINVAL);
	zassert_equal(t->sigprocmask.sig[0], (sigset_t){0}.sig[0],
		      "should not modify mask if failed");

	zassert_ok(sigprocmask(SIG_SETMASK, &set, &oset));
	zassert_equal(t->sigprocmask.sig[0], set.sig[0]);
	zassert_equal(oset.sig[0], (sigset_t){0}.sig[0]);

	// query
	zassert_ok(sigprocmask(SIG_BLOCK, NULL, &oset));
	zassert_equal(oset.sig[0], t->sigprocmask.sig[0]);

	last_set.sig[0] = t->sigprocmask.sig[0];
	set.sig[0] = BIT(SIGABRT);
	zassert_ok(sigprocmask(SIG_SETMASK, &set, &oset));
	zassert_equal(t->sigprocmask.sig[0], set.sig[0]);
	zassert_equal(oset.sig[0], last_set.sig[0]);

	// set.sig[0] = 0;
	// WRITE_BIT(set.sig[0], SIGABRT, 1);
	// zassert_ok(sigprocmask(SIG_SETMASK, &set, &oset));
	// target_set.sig[0] = 0;
	// WRITE_BIT(target_set.sig[0], SIGABRT, 1);
	// zassert_equal(t->sigprocmask.sig[0], target_set.sig[0]);
	// zassert_equal(oset.sig[0], target_oset.sig[0]);

	// zassert_ok(sigprocmask(42, &set));
	// zassert_equal(set.sig[0], target.sig[0], "%lx", set.sig[0]);

	// WRITE_BIT(set.sig[0], SIGTERM, 1);

	return NULL;
}

ZTEST(posix_apis, test_signal_procmask)
{
	void *retval;
	static pthread_t pthread;
	static pthread_attr_t pthread_attr;
	const struct sched_param param = {
		.sched_priority = sched_get_priority_max(SCHED_FIFO),
	};

	zassert_ok(pthread_attr_init(&pthread_attr));
	zassert_ok(pthread_attr_setstack(&pthread_attr, thread_stack, STACK_SIZE));
	zassert_ok(pthread_attr_setschedpolicy(&pthread_attr, SCHED_FIFO));
	zassert_ok(pthread_attr_setschedparam(&pthread_attr, &param));
	zassert_ok(pthread_create(&pthread, &pthread_attr, sigprocmask_test_fn, NULL));
	zassert_ok(pthread_join(pthread, &retval));
}
