/*
 * Copyright (c) 2024 Meta Platforms
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/debug/symtab.h>
#include <zephyr/shell/shell.h>

static int cmd_list(const struct shell *sh, size_t argc, char *argv[])
{
	const struct symtab_info *const symtab = symtab_get();

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "   #       Address    Name");
	shell_print(sh, "__________________________________________________________");
	for (int i = 0; i < symtab->length; i++) {
		shell_print(sh, "%4d    %10p    %s", i + 1,
			    (void *)(symtab->entries[i].offset + symtab->first_addr),
			    symtab->entries[i].name);
	}

	return 0;
}

static int cmd_len(const struct shell *sh, size_t argc, char *argv[])
{
	const struct symtab_info *const symtab = symtab_get();

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "Total number of symbols: %d\n", symtab->length);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(symtab_cmds,
	SHELL_CMD_ARG(list, NULL, "List all symbols", cmd_list, 0, 0),
	SHELL_CMD_ARG(len, NULL, "Length of symbol table", cmd_len, 0, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_ARG_REGISTER(symtab, &symtab_cmds, "Symtab shell commands",
		       NULL, 2, 0);
