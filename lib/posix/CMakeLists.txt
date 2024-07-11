# SPDX-License-Identifier: Apache-2.0

if(CONFIG_POSIX_API)
  zephyr_include_directories(${ZEPHYR_BASE}/lib/posix/include)
endif()

add_subdirectory(options)
add_subdirectory_ifdef(CONFIG_POSIX_SHELL shell)
