/*
 * Copyright (c) 2024 Meta Platforms
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_ZEPHYR_ZVFS_H_
#define ZEPHYR_INCLUDE_ZEPHYR_ZVFS_H_

#include <zephyr/sys/fdtable.h>

#ifdef __cplusplus
extern "C" {
#endif

enum zvfs_mode {
	ZVFS_MODE_FILE,
	ZVFS_MODE_SOCKET,
	ZVFS_MODE_MAX = INT_MAX,
};

struct zvfs_file {
	struct fd_entry entry;
	enum zvfs_mode mode;
};

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ZEPHYR_ZVFS_H_ */
