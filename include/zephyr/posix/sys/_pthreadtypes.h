/*
 * Copyright (c) 2017-2018 Intel Corporation
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_POSIX_SYS__PTHREADTYPES_H_
#define ZEPHYR_INCLUDE_POSIX_SYS__PTHREADTYPES_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* exclude external sys/_pthreadtypes.h */
#define _SYS__PTHREADTYPES_H_

#if defined(_POSIX_THREADS) || (_POSIX_C_SOURCE >= 199506L) || defined(__DOXYGEN__)

#include <zephyr/posix/sys/sched.h>

/* Pthread */
typedef uint32_t pthread_t;

/* Pthread scope */
#define PTHREAD_SCOPE_PROCESS 1
#define PTHREAD_SCOPE_SYSTEM  0

/* Pthread inherit scheduler */
#define PTHREAD_INHERIT_SCHED  0
#define PTHREAD_EXPLICIT_SCHED 1

/* Pthread detach/joinable */
#define PTHREAD_CREATE_DETACHED 1
#define PTHREAD_CREATE_JOINABLE 0

/* Thread attributes */
typedef struct {
	void *stack;
	uint32_t details[2];
} pthread_attr_t;

#if defined(_POSIX_THREAD_PROCESS_SHARED) || defined(__DOXYGEN__)
/* Pthread resource visibility */
#define PTHREAD_PROCESS_PRIVATE 0
#define PTHREAD_PROCESS_SHARED  1
#endif

#if defined(_POSIX_THREAD_PRIO_PROTECT) || defined(__DOXYGEN__)
/*
 *  Mutex attributes - protocol
 *
 *  PTHREAD_PRIO_NONE: Ownership of mutex does not affect priority.
 *  PTHREAD_PRIO_INHERIT: Owner's priority is boosted to the priority of
 *      highest priority thread blocked on the mutex.
 *  PTHREAD_PRIO_PROTECT:  Mutex has a priority ceiling.  The owner's
 *      priority is boosted to the highest priority ceiling of all mutexes
 *      owned (regardless of whether or not other threads are blocked on
 *      any of these mutexes).
 *  FIXME: Only PRIO_NONE is supported. Implement other protocols.
 */
#define PTHREAD_PRIO_NONE 0
#endif

/*
 *  Mutex attributes - type
 *
 *  PTHREAD_MUTEX_NORMAL: Owner of mutex cannot relock it. Attempting
 *      to relock will cause deadlock.
 *  PTHREAD_MUTEX_RECURSIVE: Owner can relock the mutex.
 *  PTHREAD_MUTEX_ERRORCHECK: If owner attempts to relock the mutex, an
 *      error is returned.
 *
 */
#define PTHREAD_MUTEX_NORMAL     0
#define PTHREAD_MUTEX_RECURSIVE  1
#define PTHREAD_MUTEX_ERRORCHECK 2
#define PTHREAD_MUTEX_DEFAULT    PTHREAD_MUTEX_NORMAL

/* Mutex */
typedef uint32_t pthread_mutex_t;
typedef struct {
	unsigned char type: 2;
	bool initialized: 1;
} pthread_mutexattr_t;

#define _PTHREAD_MUTEX_INITIALIZER (-1)

/* Condition variables */
typedef uint32_t pthread_cond_t;
typedef struct {
	clockid_t clock;
} pthread_condattr_t;

/* Thread-specific storage */
typedef uint32_t pthread_key_t;

/* One-time initialization */
typedef struct pthread_once {
	bool flag;
} pthread_once_t;
/* clang-format off */
#define _PTHREAD_ONCE_INIT {0}
/* clang-format on */

#if defined(_POSIX_BARRIERS) || defined(__DOXYGEN__)
/* Barrier */
typedef uint32_t pthread_barrier_t;
typedef struct {
	int pshared;
} pthread_barrierattr_t;
#endif

#if defined(_POSIX_SPIN_LOCKS) || defined(__DOXYGEN__)
typedef uint32_t pthread_spinlock_t;
#endif

#if defined(_POSIX_READER_WRITER_LOCKS) || defined(__DOXYGEN__)
typedef uint32_t pthread_rwlockattr_t;
typedef uint32_t pthread_rwlock_t;
#endif

#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_POSIX_SYS__PTHREADTYPES_H_ */
