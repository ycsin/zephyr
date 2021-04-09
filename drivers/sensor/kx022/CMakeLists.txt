# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_KX022            kx022)
zephyr_library_sources_ifdef(CONFIG_KX022            kx022_i2c.c)
#zephyr_library_sources_ifdef(CONFIG_KX022_TRIGGER    kx022_trigger.c)
