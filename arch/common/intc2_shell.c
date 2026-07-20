/*
 * Copyright (c) 2026 Yong Cong Sin <yongcong.sin@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * intc2 shell commands (design delta R32): a generic node/line
 * inspector and affinity control that works against any
 * devicetree-backed intc2 node by name. Unlike the PLIC-specific
 * "plic affinity" shell, this has no dependency on a struct device --
 * many intc2 nodes (allocator nodes in particular) have none.
 */

#include <string.h>

#include <zephyr/intc2.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/iterable_sections.h>

static const struct intc2_node *find_node(const struct shell *sh, const char *name)
{
	STRUCT_SECTION_FOREACH(intc2_shell_rec, rec) {
		if (strcmp(rec->name, name) == 0) {
			return rec->node;
		}
	}

	shell_error(sh, "intc2 node '%s' not found (see 'intc2 list')", name);

	return NULL;
}

static void node_name_get(size_t idx, struct shell_static_entry *entry)
{
	size_t i = 0;
	const char *name = NULL;

	STRUCT_SECTION_FOREACH(intc2_shell_rec, rec) {
		if (i == idx) {
			name = rec->name;
			break;
		}
		i++;
	}

	entry->syntax = name;
	entry->handler = NULL;
	entry->help = NULL;
	entry->subcmd = NULL;
}

SHELL_DYNAMIC_CMD_CREATE(dsub_intc2_node_name, node_name_get);

static int cmd_list(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "%-32s %6s  %s", "NODE", "NLINES", "FLAGS");

	STRUCT_SECTION_FOREACH(intc2_shell_rec, rec) {
		const struct intc2_node *node = rec->node;

		shell_print(sh, "%-32s %6u  %s%s", rec->name, node->nlines,
			    (node->flags & INTC2_NODE_ROOT_BRIDGE) ? "root-bridge " : "",
			    (node->flags & INTC2_NODE_ALLOC) ? "alloc " : "");
	}

	return 0;
}

static int parse_node_and_line(const struct shell *sh, char **argv,
			       const struct intc2_node **node, uint32_t *line)
{
	/* shell_strtoul() only writes *err on failure, never clears it
	 * on success, so it must start out at 0
	 */
	int rc = 0;

	*node = find_node(sh, argv[1]);
	if (*node == NULL) {
		return -ENODEV;
	}

	*line = (uint32_t)shell_strtoul(argv[2], 10, &rc);
	if (rc != 0) {
		shell_error(sh, "failed to parse line '%s': %d", argv[2], rc);
		return rc;
	}

	if (*line >= (*node)->nlines) {
		shell_error(sh, "line %u >= nlines %u", *line, (*node)->nlines);
		return -EINVAL;
	}

	return 0;
}

static int cmd_affinity_get(const struct shell *sh, size_t argc, char **argv)
{
	const struct intc2_node *node;
	uint32_t line;
	uint32_t mask;
	int rc;

	ARG_UNUSED(argc);

	rc = parse_node_and_line(sh, argv, &node, &line);
	if (rc != 0) {
		return rc;
	}

	rc = intc2_get_affinity((struct intc2_spec){.node = node, .line = line}, &mask);
	if (rc != 0) {
		shell_error(sh, "intc2_get_affinity: %d", rc);
		return rc;
	}

	shell_print(sh, "%s line %u affinity: 0x%x", argv[1], line, mask);

	return 0;
}

static int cmd_affinity_set(const struct shell *sh, size_t argc, char **argv)
{
	const struct intc2_node *node;
	uint32_t line;
	uint32_t mask;
	int rc;

	ARG_UNUSED(argc);

	rc = parse_node_and_line(sh, argv, &node, &line);
	if (rc != 0) {
		return rc;
	}

	mask = (uint32_t)shell_strtoul(argv[3], 16, &rc);
	if (rc != 0) {
		shell_error(sh, "failed to parse mask '%s': %d", argv[3], rc);
		return rc;
	}

	rc = intc2_set_affinity((struct intc2_spec){.node = node, .line = line}, mask);
	if (rc != 0) {
		shell_error(sh, "intc2_set_affinity: %d", rc);
		return rc;
	}

	shell_print(sh, "%s line %u affinity set to 0x%x", argv[1], line, mask);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(intc2_affinity_cmds,
	SHELL_CMD_ARG(get, &dsub_intc2_node_name,
		"Get the runtime CPU affinity mask of a line.\n"
		"Usage: intc2 affinity get <node> <line>",
		cmd_affinity_get, 3, 0),
	SHELL_CMD_ARG(set, &dsub_intc2_node_name,
		"Set the runtime CPU affinity mask of a line.\n"
		"Usage: intc2 affinity set <node> <line> <mask (hex)>",
		cmd_affinity_set, 4, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(intc2_cmds,
	SHELL_CMD_ARG(list, NULL,
		"List devicetree-backed intc2 nodes.\n"
		"Usage: intc2 list",
		cmd_list, 1, 0),
	SHELL_CMD(affinity, &intc2_affinity_cmds,
		"intc2 line affinity (CONFIG_INTC2_AFFINITY; reports -ENOSYS/-ENOTSUP"
		" otherwise)",
		NULL),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_ARG_REGISTER(intc2, &intc2_cmds, "intc2 shell commands", NULL, 0, 0);
