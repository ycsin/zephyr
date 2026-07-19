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
 * is produced by the build-time table generator as a generated fragment
 * included here in the final link pass.
 */

. = ALIGN(8);
KEEP(*(.intc2_entry.*))
