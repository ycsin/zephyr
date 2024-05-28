# SPDX-License-Identifier: Apache-2.0

add_library(symtab
  symtab.c
  symtab_stub.c
)

zephyr_sources_ifdef(CONFIG_SYMTAB_SHELL
  symtab_shell.c
)

target_link_libraries(symtab zephyr_interface)
zephyr_library_link_libraries(symtab)
