# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(intc2)

FILE(GLOB app_sources src/*.c)
list(FILTER app_sources EXCLUDE REGEX "emul_alloc_intc\\.c$")
target_sources(app PRIVATE ${app_sources})

if(CONFIG_INTC2_ALLOC)
  target_sources(app PRIVATE src/emul_alloc_intc.c)
endif()
