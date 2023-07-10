/*
 * Copyright (c) 2023 Meta
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "posix/strsignal_table.h"
#include "posix_internal.h"
#include "signal_internal.h"

#include <errno.h>
#include <stdio.h>

#include <zephyr/posix/signal.h>
#include <zephyr/sys/slist.h>

static struct k_spinlock signal_key_lock;

#define SIGNO_WORD_IDX(_signo) (signo / BITS_PER_LONG)
#define SIGNO_WORD_BIT(_signo) (signo & BIT_MASK(LOG2(BITS_PER_LONG)))

static inline bool signo_valid(int signo)
{
	return ((signo > 0) && (signo <= _NSIG));
}

static inline bool signo_is_rt(int signo)
{
	return ((signo >= SIGRTMIN) && (signo <= SIGRTMAX));
}

static void sig_orset(sigset_t *dest, const sigset_t *left, const sigset_t *right)
{
	for (int i = 0; i < ARRAY_SIZE(dest->sig); i++) {
		dest->sig[i] = left->sig[i] | right->sig[i];
	}
}

static void sig_andset(sigset_t *dest, const sigset_t *left, const sigset_t *right)
{
	for (int i = 0; i < ARRAY_SIZE(dest->sig); i++) {
		dest->sig[i] = left->sig[i] & right->sig[i];
	}
}

int sigemptyset(sigset_t *set)
{
	*set = (sigset_t){0};

	return 0;
}

int sigfillset(sigset_t *set)
{
	for (int i = 0; i < ARRAY_SIZE(set->sig); i++) {
		set->sig[i] = -1;
	}

	return 0;
}

int sigaddset(sigset_t *set, int signo)
{
	if (!signo_valid(signo)) {
		errno = EINVAL;
		return -1;
	}

	WRITE_BIT(set->sig[SIGNO_WORD_IDX(signo)], SIGNO_WORD_BIT(signo), 1);

	return 0;
}

int sigdelset(sigset_t *set, int signo)
{
	if (!signo_valid(signo)) {
		errno = EINVAL;
		return -1;
	}

	WRITE_BIT(set->sig[SIGNO_WORD_IDX(signo)], SIGNO_WORD_BIT(signo), 0);

	return 0;
}

int sigismember(const sigset_t *set, int signo)
{
	if (!signo_valid(signo)) {
		errno = EINVAL;
		return -1;
	}

	return 1 & (set->sig[SIGNO_WORD_IDX(signo)] >> SIGNO_WORD_BIT(signo));
}

char *strsignal(int signum)
{
	static char other_sigstr[sizeof("RT signal xx")];

	if (!signo_valid(signum)) {
		return "Invalid signal";
	}

	if (signo_is_rt(signum)) {
		snprintf(other_sigstr, sizeof(other_sigstr), "RT signal %d", signum - SIGRTMIN);
		return other_sigstr;
	}

	if (strsignal_list[signum] != NULL) {
		return (char *)strsignal_list[signum];
	}

	snprintf(other_sigstr, sizeof(other_sigstr), "Signal %d", signum);

	return other_sigstr;
}

int sigpending(sigset_t *set)
{
	sys_snode_t *node;
	struct posix_thread *t;
	k_spinlock_key_t key;

	key = k_spin_lock(&signal_key_lock);
	t = (struct posix_thread *)CONTAINER_OF(k_current_get(), struct posix_thread, thread);
	SYS_SLIST_FOR_EACH_NODE(&(t->sigpending_list), node) {
		struct sig_pending *sig = CONTAINER_OF(node, struct sig_pending, node);

		sigaddset(set, sig->info.si_signo);
	}
	k_spin_unlock(&signal_key_lock, key);

	return 0;
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
	sys_snode_t *node;
	struct posix_thread *t;

	if (!signo_valid(sig)) {
		errno = EINVAL;
		return -1;
	}

	t = (struct posix_thread *)CONTAINER_OF(k_current_get(), struct posix_thread, thread);
	SYS_SLIST_FOR_EACH_NODE(&(t->sigaction_list), node) {
		//
	}

	return 0;
}

int sigprocmask(int how, const sigset_t *ZRESTRICT set, sigset_t *ZRESTRICT oset)
{
	struct posix_thread *t;

	t = (struct posix_thread *)CONTAINER_OF(k_current_get(), struct posix_thread, thread);
	if (oset != NULL) {
		*oset = t->sigprocmask;
	}

	if (set != NULL) {
		switch (how) {
		case SIG_BLOCK:
			sig_andset(&t->sigprocmask, set, &t->sigprocmask);
			break;
		case SIG_UNBLOCK:
			sig_orset(&t->sigprocmask, set, &t->sigprocmask);
			break;
		case SIG_SETMASK:
			t->sigprocmask = *set;
			break;

		default:
			errno = EINVAL;
			return -1;
		}
	}

	return 0;
}
