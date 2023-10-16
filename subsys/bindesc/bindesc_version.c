/*
 * Copyright (c) 2023 Yonatan Schachter
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/bindesc.h>
#include <zephyr/version.h>

#if IS_ENABLED(CONFIG_BINDESC_KERNEL_VERSION_STRING)
BINDESC_STR_DEFINE(kernel_version_string, BINDESC_ID_KERNEL_VERSION_STRING,
			KERNEL_VERSION_STRING);
#endif /* IS_ENABLED(CONFIG_BINDESC_KERNEL_VERSION_STRING) */

#if IS_ENABLED(CONFIG_BINDESC_KERNEL_VERSION_MAJOR)
BINDESC_UINT_DEFINE(kernel_version_major, BINDESC_ID_KERNEL_VERSION_MAJOR,
			KERNEL_VERSION_MAJOR);
#endif /* IS_ENABLED(CONFIG_BINDESC_KERNEL_VERSION_MAJOR) */

#if IS_ENABLED(CONFIG_BINDESC_KERNEL_VERSION_MINOR)
BINDESC_UINT_DEFINE(kernel_version_minor, BINDESC_ID_KERNEL_VERSION_MINOR,
			KERNEL_VERSION_MINOR);
#endif /* IS_ENABLED(CONFIG_BINDESC_KERNEL_VERSION_MINOR) */

#if IS_ENABLED(CONFIG_BINDESC_KERNEL_VERSION_PATCHLEVEL)
BINDESC_UINT_DEFINE(kernel_version_patchlevel, BINDESC_ID_KERNEL_VERSION_PATCHLEVEL,
			KERNEL_PATCHLEVEL);
#endif /* IS_ENABLED(CONFIG_BINDESC_KERNEL_VERSION_PATCHLEVEL) */

#if IS_ENABLED(CONFIG_BINDESC_KERNEL_VERSION_NUMBER)
BINDESC_UINT_DEFINE(kernel_version_number, BINDESC_ID_KERNEL_VERSION_NUMBER,
			KERNEL_VERSION_NUMBER);
#endif /* IS_ENABLED(CONFIG_BINDESC_KERNEL_VERSION_NUMBER) */

#if IS_ENABLED(HAS_APP_VERSION)
#include <app_version.h>

#if IS_ENABLED(CONFIG_BINDESC_APP_VERSION_STRING)
BINDESC_STR_DEFINE(app_version_string, BINDESC_ID_APP_VERSION_STRING,
			APP_VERSION_STRING);
#endif /* IS_ENABLED(CONFIG_BINDESC_APP_VERSION_STRING) */

#if IS_ENABLED(CONFIG_BINDESC_APP_VERSION_MAJOR)
BINDESC_UINT_DEFINE(app_version_major, BINDESC_ID_APP_VERSION_MAJOR,
			APP_VERSION_MAJOR);
#endif /* IS_ENABLED(CONFIG_BINDESC_APP_VERSION_MAJOR) */

#if IS_ENABLED(CONFIG_BINDESC_APP_VERSION_MINOR)
BINDESC_UINT_DEFINE(app_version_minor, BINDESC_ID_APP_VERSION_MINOR,
			APP_VERSION_MINOR);
#endif /* IS_ENABLED(CONFIG_BINDESC_APP_VERSION_MINOR) */

#if IS_ENABLED(CONFIG_BINDESC_APP_VERSION_PATCHLEVEL)
BINDESC_UINT_DEFINE(app_version_patchlevel, BINDESC_ID_APP_VERSION_PATCHLEVEL,
			APP_PATCHLEVEL);
#endif /* IS_ENABLED(CONFIG_BINDESC_APP_VERSION_PATCHLEVEL) */

#if IS_ENABLED(CONFIG_BINDESC_APP_VERSION_NUMBER)
BINDESC_UINT_DEFINE(app_version_number, BINDESC_ID_APP_VERSION_NUMBER,
			APP_VERSION_NUMBER);
#endif /* IS_ENABLED(CONFIG_BINDESC_APP_VERSION_NUMBER) */

#endif /* IS_ENABLED(HAS_APP_VERSION) */
