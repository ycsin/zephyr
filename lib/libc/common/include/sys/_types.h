#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _BLKCNT_T_DECLARED
typedef unsigned long blkcnt_t;
#define _BLKCNT_T_DECLARED
#endif

#ifndef _BLKSIZE_T_DECLARED
typedef unsigned long blksize_t;
#define _BLKSIZE_T_DECLARED
#endif

#ifndef _CLOCK_T_DECLARED
typedef unsigned int clock_t;
#define _CLOCK_T_DECLARED
#endif

#ifndef _DEV_T_DECLARED
typedef int dev_t;
#define _DEV_T_DECLARED
#endif

#ifndef _FSBLKCNT_T_DECLARED
typedef unsigned long fsblkcnt_t;
#define _FSBLKCNT_T_DECLARED
#endif

#ifndef _FSFILCNT_T_DECLARED
typedef unsigned long fsfilcnt_t;
#define _FSFILCNT_T_DECLARED
#endif

#ifndef _GID_T_DECLARED
typedef unsigned short gid_t;
#define _GID_T_DECLARED
#endif

#ifndef _INO_T_DECLARED
typedef int ino_t;
#define _INO_T_DECLARED
#endif

#ifndef _KEY_T_DECLARED
typedef unsigned long key_t;
#define _KEY_T_DECLARED
#endif

#ifndef _MODE_T_DECLARED
typedef unsigned int mode_t;
#define _MODE_T_DECLARED
#endif

#ifndef _NLINK_T_DECLARED
typedef unsigned short nlink_t;
#define _NLINK_T_DECLARED
#endif

#ifndef _OFF_T_DECLARED
typedef long off_t;
#define _OFF_T_DECLARED
#endif

#ifndef _PID_T_DECLARED
#if defined(CONFIG_ARCMWDT_LIBC)
typedef long pid_t;
#else
typedef int pid_t;
#endif
#define _PID_T_DECLARED
#endif

#include <zephyr/posix/sys/_pthreadtypes.h>

#ifndef _SIZE_T_DECLARED
typedef unsigned long size_t;
#define _SIZE_T_DECLARED
#endif

#ifndef _SSIZE_T_DECLARED
typedef long ssize_t;
#define _SSIZE_T_DECLARED
#endif

#ifndef _SUSECONDS_T_DECLARED
typedef int suseconds_t;
#define _SUSECONDS_T_DECLARED
#endif

#ifndef _TIME_T_DECLARED
#define _TIME_T_ int64_t
typedef _TIME_T_ time_t;
#define __time_t_defined
#define _TIME_T_DECLARED
#endif

#ifndef _UID_T_DECLARED
typedef unsigned short uid_t;
#define _UID_T_DECLARED
#endif

#ifndef _USECONDS_T_DECLARED
typedef unsigned long useconds_t;
#define _USECONDS_T_DECLARED
#endif

#ifndef __machine_fpos_t_defined
typedef long _fpos_t;
#endif

#ifndef __machine_off_t_defined
typedef long _off_t;
#endif

#ifndef __WINT_TYPE__
#define __WINT_TYPE__ unsigned int
#endif
typedef __WINT_TYPE__ wint_t;

typedef struct {
	int __count;
	union {
		wint_t __wch;
		unsigned char __wchb[4];
	} __value;
} _mbstate_t;

#define _TIMER_T_ unsigned long
typedef _TIMER_T_ __timer_t;
typedef __timer_t timer_t;

#ifndef __machine_clockid_t_defined
#define _CLOCKID_T_ unsigned long
#endif
typedef _CLOCKID_T_ __clockid_t;
typedef __clockid_t clockid_t;

#ifdef __cplusplus
}
#endif
