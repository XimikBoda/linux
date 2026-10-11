/*
 *  linux/arch/arm/mm/proc-arm7ejs.S: utility functions for ARM7EJ-S
 *
 *  Copyright (C) 2003-2006 Hyok S. Choi <hyok.choi@samsung.com>
 *  Copyright (C) 2026 Ximik_Boda <ximikboda@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/hwcap.h>
#include <asm/pgtable-hwdef.h>
#include <asm/pgtable.h>
#include <asm/ptrace.h>

#include "proc-macros.S"

	.text
ENTRY(cpu_arm7ejs_proc_init)
ENTRY(cpu_arm7ejs_do_idle)
ENTRY(cpu_arm7ejs_dcache_clean_area)
ENTRY(cpu_arm7ejs_switch_mm)
		ret	lr

ENTRY(cpu_arm7ejs_proc_fin)
		ret	lr

/*
 * Function: cpu_arm7ejs_reset(loc)
 * Params  : loc(r0)	address to jump to
 * Purpose : Sets up everything for a reset and jump to the location for soft reset.
 */
		.pushsection	.idmap.text, "ax"
ENTRY(cpu_arm7ejs_reset)
		ret	r0
ENDPROC(cpu_arm7ejs_reset)
		.popsection

		.type	__arm7ejs_setup, #function
__arm7ejs_setup:
		ret	lr
		.size	__arm7ejs_setup, . - __arm7ejs_setup

		__INITDATA

		@ define struct processor (see <asm/proc-fns.h> and proc-macros.S)
		define_processor_functions arm7ejs, dabort=nommu_early_abort, pabort=legacy_pabort, nommu=1

		.section ".rodata"

		string	cpu_arch_name, "armv5te"
		string	cpu_elf_name, "v5"
		string	cpu_arm7ejs_name, "ARM7EJ-S"

		.align

		.section ".proc.info.init", "a"

.macro arm7ejs_proc_info name:req, cpu_val:req, cpu_mask:req, cpu_name:req, \
	extra_hwcaps=0
		.type	__\name\()_proc_info, #object
__\name\()_proc_info:
		.long	\cpu_val
		.long	\cpu_mask
		.long	0
		.long	0
		initfn	__arm7ejs_setup, __\name\()_proc_info
		.long	cpu_arch_name
		.long	cpu_elf_name
		.long	HWCAP_SWP | HWCAP_26BIT | ( \extra_hwcaps )
		.long	\cpu_name
		.long	arm7ejs_processor_functions
		.long	0
		.long	0
		.long	v4_cache_fns
		.size	__\name\()_proc_info, . - __\name\()_proc_info
.endm

		arm7ejs_proc_info arm7ejs_, 0x41007700, 0xfff8ff00, \
			cpu_arm7ejs_name
