/*
 * Copyright (c) 2026 Yong Cong Sin <yongcong.sin@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Placement of the intc2 per-node dispatch tables. Entries are emitted at
 * each INTC2_DT_CONNECT() call site into .intc2_entry.<ord>.<line>.<n>
 * input sections; the KEEP below anchors them against --gc-sections and
 * collects them here (RODATA, or RWDATA under CONFIG_INTC2_DYNAMIC).
 *
 * Table layout (per-node ordering, gaps, and the __intc2_table_* symbols)
 * is produced by scripts/build/gen_intc2_tables.py as a generated fragment
 * included here in the final link pass. With CONFIG_INTC2 enabled the
 * final pass always exists, because the generated companion source is
 * added to GENERATED_KERNEL_SOURCE_FILES.
 */

. = ALIGN(8);
#if defined(LINKER_ZEPHYR_FINAL) || defined(LINKER_INTC2_TABLES)
INCLUDE intc2_tables.ld
#else
KEEP(*(.intc2_entry.*))
#ifdef CONFIG_INTC2_LEGACY_BRIDGE
/*
 * Pass-0 placeholder for the bridged legacy table symbol; the real
 * definition comes from the generated fragment in later passes.
 */
PROVIDE(_sw_isr_table = .);
#endif
#endif
