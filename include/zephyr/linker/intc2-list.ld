/*
 * Copyright (c) 2026 Yong Cong Sin <yongcong.sin@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * First-link-pass record section for the intc2 interrupt core. The
 * .intc2_info header and the .intc2_list records are consumed by
 * scripts/build/gen_intc2_tables.py from the pass-1 ELF and are not part
 * of the final image; the KEEP inside /DISCARD/ retains the input
 * sections through garbage collection while stripping them from the
 * output. Same lifecycle as .intList (see intlist.ld).
 */

#ifndef LINKER_ZEPHYR_FINAL
SECTION_PROLOGUE(.intc2_list,,)
{
	KEEP(*(.intc2_info*))
	KEEP(*(.intc2_list*))
} GROUP_ROM_LINK_IN(IDT_LIST, IDT_LIST)
#else
/DISCARD/ :
{
	KEEP(*(.intc2_info*))
	KEEP(*(.intc2_list*))
}
#endif
