/*
 * Copyright 2024 Meta Platforms
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/debug/symtab.h>
#include <zephyr/ztest.h>

const struct symtab_info *symtab;

static void *setup(void)
{
	symtab = symtab_get();

	return NULL;
}

ZTEST_SUITE(test_symtab, NULL, setup, NULL, NULL, NULL);

ZTEST(test_symtab, test_size)
{
	zassert_true(symtab->length > 0);
}

ZTEST(test_symtab, test_symtab_find_symbol_name)
{
	extern int main(void);
	const uintptr_t first_addr = symtab->first_addr;
	const uintptr_t last_addr = first_addr + symtab->entries[symtab->length - 1].offset;
	uint32_t offset;
	const char *symbol_name;

	zassert_between_inclusive((uintptr_t)main, first_addr, last_addr,
				  "No valid address found for `main()`");

	/* Find the name of functions with `symtab_find_symbol_name()` */
	offset = -1;
	symbol_name = symtab_find_symbol_name((uintptr_t)main, &offset);
	zassert_equal(strcmp(symbol_name, "main"), 0);
	zassert_equal(offset, 0);

	/* Do a few more just for fun */
	symbol_name = symtab_find_symbol_name((uintptr_t)strcmp, NULL);
	zassert_equal(strcmp(symbol_name, "strcmp"), 0);

	symbol_name = symtab_find_symbol_name((uintptr_t)symtab_find_symbol_name, NULL);
	zassert_equal(strcmp(symbol_name, "symtab_find_symbol_name"), 0);

	symbol_name = symtab_find_symbol_name((uintptr_t)test_main, NULL);
	zassert_equal(strcmp(symbol_name, "test_main"), 0);

	symbol_name = symtab_find_symbol_name((uintptr_t)setup, NULL);
	zassert_equal(strcmp(symbol_name, "setup"), 0);
}

ZTEST(test_symtab, test_symtab_before_first)
{
	const uintptr_t first_addr = symtab->first_addr;
	const char *symbol_name;
	uint32_t offset;

	/* No symbol should appear before first_addr, but make sure that first_addr != 0 */
	if (first_addr > 0) {
		offset = -1;
		symbol_name = symtab_find_symbol_name(first_addr - 1, &offset);
		zassert_equal(strcmp(symbol_name, "?"), 0);
		zassert_equal(offset, 0);
	} else {
		ztest_test_skip();
	}
}

ZTEST(test_symtab, test_symtab_first)
{
	const uintptr_t first_addr = symtab->first_addr;
	const char *symbol_name;
	uint32_t offset;

	offset = -1;
	symbol_name = symtab_find_symbol_name(first_addr, &offset);
	zassert_equal(strcmp(symbol_name, symtab->entries[0].name), 0);
	zassert_equal(offset, 0);

	if ((symtab->entries[0].offset + 1) != symtab->entries[1].offset) {
		offset = -1;
		symbol_name = symtab_find_symbol_name(first_addr + 1, &offset);
		zassert_equal(strcmp(symbol_name, symtab->entries[0].name), 0);
		zassert_equal(offset, 1);
	}
}

ZTEST(test_symtab, test_symtab_last)
{
	const uintptr_t first_addr = symtab->first_addr;
	const int last_idx = symtab->length - 1;
	const uintptr_t last_addr = first_addr + symtab->entries[last_idx].offset;
	const int sec_last_idx = symtab->length - 1;
	const uintptr_t sec_last_addr = first_addr + symtab->entries[sec_last_idx].offset;
	const char *symbol_name;
	uint32_t offset;

	offset = -1;
	symbol_name = symtab_find_symbol_name(last_addr, &offset);
	zassert_equal(strcmp(symbol_name, symtab->entries[last_idx].name), 0);
	zassert_equal(offset, 0);

	if ((sec_last_addr + 1) != last_addr) {
		offset = -1;
		symbol_name = symtab_find_symbol_name(sec_last_addr + 1, &offset);
		zassert_equal(strcmp(symbol_name, symtab->entries[sec_last_idx].name), 0);
		zassert_equal(offset, 1);
	}
}

ZTEST(test_symtab, test_symtab_after_last)
{
	const uintptr_t first_addr = symtab->first_addr;
	const uintptr_t last_offset = symtab->entries[symtab->length - 1].offset;
	const uintptr_t last_addr = first_addr + last_offset;
	const char *symbol_name;
	uint32_t offset;

	/* Test `offset` output with last symbol, so that the test always works */
	if (last_offset + 0x1 != symtab->entries[symtab->length].offset) {
		offset = -1;
		symbol_name = symtab_find_symbol_name(last_addr + 0x1, &offset);
		zassert_equal(strcmp(symbol_name, symtab->entries[symtab->length - 1].name), 0);
		zassert_equal(offset, 0x1, "%x", offset);
	}
}

void symtab_test_function(void)
{
	printk("%s\n", __func__);
}
