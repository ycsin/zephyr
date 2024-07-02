#pragma once

#include <zephyr/sys/util.h>

#if !defined(_SIGSET_T_DECLARED)
struct sigset_s {
	unsigned long sig[MAX(DIV_ROUND_UP((32 + RTSIG_MAX), BITS_PER_LONG), 1)];
};
typedef struct sigset_s __sigset_t;
typedef __sigset_t sigset_t;
#define _SIGSET_T_DECLARED
#endif
