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
typedef __useconds_t useconds_t;
#define _USECONDS_T_DECLARED
#endif

#ifdef __cplusplus
}
#endif
