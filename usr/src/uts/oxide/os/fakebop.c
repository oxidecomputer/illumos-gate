/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2010, Intel Corporation.
 * All rights reserved.
 *
 * Copyright (c) 2012 Gary Mills
 * Copyright 2020 Joyent, Inc.
 * Copyright 2024 Oxide Computer Co.
 */

/*
 * This file contains the functionality that mimics the boot operations
 * on SPARC systems or the old boot.bin/multiboot programs on x86 systems.
 * The x86 kernels now do everything on their own.
 */

#include <sys/types.h>
#include <sys/bootconf.h>
#include <sys/bootsvcs.h>
#include <sys/bootinfo.h>
#include <sys/bootvfs.h>
#include <sys/bootprops.h>
#include <sys/boot_console.h>
#include <sys/boot_data.h>
#include <sys/boot_debug.h>
#include <sys/platform_detect.h>
#include <sys/kernel_ipcc.h>
#include <sys/boot_physmem.h>
#include <sys/varargs.h>
#include <sys/param.h>
#include <sys/machparam.h>
#include <sys/machsystm.h>
#include <sys/archsystm.h>
#include <sys/cmn_err.h>
#include <sys/systm.h>
#include <sys/promif.h>
#include <sys/archsystm.h>
#include <sys/x86_archext.h>
#include <sys/kobj.h>
#include <sys/privregs.h>
#include <sys/sysmacros.h>
#include <sys/ctype.h>
#include <vm/kboot_mmu.h>
#include <vm/hat_pte.h>
#include <sys/kobj.h>
#include <sys/kobj_lex.h>
#include <sys/pci_cfgspace_impl.h>
#include <sys/ddipropdefs.h>	/* For DDI prop types */
#include <sys/dw_apb_uart.h>
#include <sys/x86_archext.h>
#include <sys/uart.h>
#include <sys/memlist_impl.h>
#include <sys/apob.h>
#include <sys/kapob.h>
#include <sys/io/milan/ccx.h>

#include "milan/milan_apob.h"
#include "milan/milan_physaddrs.h"

/*
 * Comes from fs/ufsops.c.  For debugging the ramdisk/root fs operations.  Set
 * by a bit in the debug register received from the SP - see os/boot_data.c
 */
extern int bootrd_debug;

/*
 * Comes from os/startup.c.  Set by a bit in the debug register received from
 * the SP - see os/boot_data.c
 */
extern int prom_debug;

/*
 * General early boot (pre-kobj, pre-prom_printf) debug flag.  Set by the
 * existence of the boot property of the same name.
 */
boolean_t kbm_debug = B_FALSE;

static bootops_t bootop;
static struct bsys_mem bm;

uint32_t reset_vector;

/*
 * We want to make runtime decisions based on what CPU/platform we're running
 * on but we don't have access to the cpuid_* interfaces yet.  So we'll just
 * have to gather that info ourselves.
 */
x86_chiprev_t early_chiprev = X86_CHIPREV_UNKNOWN;

extern x86_chiprev_t _cpuid_chiprev(uint_t, uint_t, uint_t, uint_t);
extern uint_t _cpuid_vendorstr_to_vendorcode(char *);

static void
early_cpuid_ident(void)
{
	struct cpuid_regs cp;
	char vendorstr[13];
	uint_t vendor, family, model, stepping;

	bzero(&cp, sizeof (cp));
	cp.cp_eax = 0;
	(void) __cpuid_insn(&cp);
	{
		uint32_t *iptr = (uint32_t *)vendorstr;
		*iptr++ = cp.cp_ebx;
		*iptr++ = cp.cp_edx;
		*iptr++ = cp.cp_ecx;
		vendorstr[12] = '\0';
	}
	vendor = _cpuid_vendorstr_to_vendorcode(vendorstr);
	if (vendor != X86_VENDOR_AMD)
		return;

	bzero(&cp, sizeof (cp));
	cp.cp_eax = 1;
	(void) __cpuid_insn(&cp);

	family = BITX(cp.cp_eax, 11, 8);
	if (family == 0xf)
		family += BITX(cp.cp_eax, 27, 20);

	model = BITX(cp.cp_eax, 7, 4);
	if (family >= 0xf)
		model += BITX(cp.cp_eax, 19, 16) << 4;

	stepping = BITX(cp.cp_eax, 3, 0);

	early_chiprev = _cpuid_chiprev(vendor, family, model, stepping);
}

/*ARGSUSED*/
static caddr_t
do_bsys_alloc(bootops_t *_bop, caddr_t virthint, size_t size, int align)
{
	return (eb_alloc(virthint, size, (size_t)align));
}

/*
 * Free virtual memory - we'll just ignore these.
 */
/*ARGSUSED*/
static void
do_bsys_free(bootops_t *_bop, caddr_t virt, size_t size)
{
	KBM_DBGMSG("do_bsys_free(virt=0x%p, size=0x%lx) ignored\n",
	    (void *)virt, size);
}

/*
 * Old interface
 */
/*ARGSUSED*/
static caddr_t
do_bsys_ealloc(bootops_t *_bop, caddr_t _virthint, size_t _size,
    int _align, int _flags)
{
	prom_panic("unsupported call to BOP_EALLOC()\n");
	return (0);
}

/*ARGSUSED*/
static caddr_t
no_more_alloc(bootops_t *bop, caddr_t virthint, size_t size, int align)
{
	panic("Attempt to bsys_alloc() too late\n");
	return (NULL);
}

/*ARGSUSED*/
static void
no_more_free(bootops_t *bop, caddr_t virt, size_t size)
{
	panic("Attempt to bsys_free() too late\n");
}

void
bop_no_more_mem(void)
{
	eb_physmem_fini();
	bootops->bsys_alloc = no_more_alloc;
	bootops->bsys_free = no_more_free;
}

static const bt_prop_t *
find_bt_prop(const char *name)
{
	const bt_prop_t *btpp;

	for (btpp = bt_props; btpp != NULL; btpp = btpp->btp_next) {
		if (strcmp(name, btpp->btp_name) == 0)
			return (btpp);
	}

	return (NULL);
}

/*
 * to find the type of the value associated with this name
 */
/*ARGSUSED*/
int
do_bsys_getproptype(bootops_t *bop, const char *name)
{
	const bt_prop_t *btpp;

	if ((btpp = find_bt_prop(name)) == NULL)
		return (-1);

	return (btpp->btp_typeflags & DDI_PROP_TYPE_MASK);
}

/*
 * to find the size of the buffer to allocate
 */
/*ARGSUSED*/
int
do_bsys_getproplen(bootops_t *bop, const char *name)
{
	const bt_prop_t *btpp;

	if ((btpp = find_bt_prop(name)) == NULL)
		return (-1);

	/*
	 * The signature of this method should really be changed instead to
	 * return a size_t.  Until we do that work, this ugly thing.
	 */
	if (btpp->btp_vlen > INT_MAX) {
		bop_panic("value for property %s has length %lu, which "
		    "cannot be represented to the legacy bootops interface",
		    name, btpp->btp_vlen);
	}

	return (btpp->btp_vlen);
}

/*
 * get the value associated with this name
 */
/*ARGSUSED*/
int
do_bsys_getprop(bootops_t *bop, const char *name, void *value)
{
	const bt_prop_t *btpp;

	if ((btpp = find_bt_prop(name)) == NULL)
		return (-1);

	bcopy(btpp->btp_value, value, btpp->btp_vlen);
	return (0);
}

/*
 * get the name of the next property in succession.
 * XXX constify this interface properly; it has few consumers.
 */
static char *
do_bsys_nextprop(bootops_t *bop, char *name)
{
	const bt_prop_t *btpp;

	/*
	 * If we're given NULL we're being asked for the name of the first
	 * property.
	 */
	if (name == NULL || strlen(name) == 0) {
		if (bt_props != NULL)
			return (bt_props->btp_name);
		return (NULL);
	}

	btpp = find_bt_prop(name);
	if (btpp == NULL) {
		bop_panic("unknown boot-time property name '%s' "
		    "passed as previous property name", name);
	}

	if (btpp->btp_next != NULL)
		return (btpp->btp_next->btp_name);

	return (NULL);
}

static boolean_t
unprintable(char *value, int size)
{
	int i;

	if (size <= 0 || value[0] == '\0')
		return (B_TRUE);

	for (i = 0; i < size; i++) {
		if (value[i] == '\0')
			return (i != (size - 1));

		if (!isprint(value[i]))
			return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * Print out information about all boot properties.
 * buffer is pointer to pre-allocated space to be used as temporary
 * space for property values.
 */
static void
boot_prop_display(char *buffer)
{
	char *name = "";
	int i, len, flags, *buf32;
	int64_t *buf64;

	eb_printf("\nBoot properties:\n");

	while ((name = do_bsys_nextprop(NULL, name)) != NULL) {
		eb_printf("\t0x%p %s = ", (void *)name, name);
		(void) do_bsys_getprop(NULL, name, buffer);
		len = do_bsys_getproplen(NULL, name);
		flags = do_bsys_getproptype(NULL, name);
		eb_printf("len=%d ", len);

		switch (flags) {
		case DDI_PROP_TYPE_INT:
			len = len / sizeof (int);
			buf32 = (int *)buffer;
			for (i = 0; i < len; i++) {
				eb_printf("%08x", buf32[i]);
				if (i < len - 1)
					eb_printf(".");
			}
			break;
		case DDI_PROP_TYPE_STRING:
			eb_printf("%-.*s", len, buffer);
			break;
		case DDI_PROP_TYPE_INT64:
			len = len / sizeof (int64_t);
			buf64 = (int64_t *)buffer;
			for (i = 0; i < len; i++) {
				eb_printf("%016" PRIx64, buf64[i]);
				if (i < len - 1)
					eb_printf(".");
			}
			break;
		default:
			if (!unprintable(buffer, len)) {
				buffer[len] = 0;
				eb_printf("%s", buffer);
				break;
			}
			for (i = 0; i < len; i++) {
				eb_printf("%02x", buffer[i] & 0xff);
				if (i < len - 1)
					eb_printf(".");
			}
			break;
		}
		eb_printf("\n");
	}
}

/*
 * simple description of a stack frame (args are 64 bit only currently)
 */
typedef struct bop_frame {
	struct bop_frame *old_frame;
	pc_t retaddr;
	long arg[1];
} bop_frame_t;

/* XXX shareable */
static void
bop_traceback(bop_frame_t *frame)
{
	pc_t pc;
	int cnt;
	char *ksym;
	ulong_t off;

	eb_printf("Stack traceback:\n");
	for (cnt = 0; cnt < 30; ++cnt) {	/* up to 30 frames */
		pc = frame->retaddr;
		if (pc == 0)
			break;

		ksym = NULL;
		off = 0;
		if (genunix_is_loaded())
			ksym = kobj_getsymname(pc, &off);

		if (ksym != NULL)
			eb_printf("  %s+%lx", ksym, off);
		else
			eb_printf("  0x%lx", pc);

		kipcc_panic_stack_item(pc, ksym, off);

		frame = frame->old_frame;
		if (frame == 0) {
			eb_printf("\n");
			break;
		}
		eb_printf("\n");
	}
}

struct trapframe {
	ulong_t error_code;	/* optional */
	ulong_t inst_ptr;
	ulong_t code_seg;
	ulong_t flags_reg;
	ulong_t stk_ptr;
	ulong_t stk_seg;
};

/* XXX shareable */
void
bop_trap(ulong_t *tfp)
{
	struct trapframe *tf = (struct trapframe *)tfp;
	bop_frame_t fakeframe;
	static int depth = 0;

	/*
	 * Check for an infinite loop of traps.
	 */
	if (++depth > 2)
		bop_panic("Nested trap");

	eb_printf("Unexpected trap\n");

	/*
	 * adjust the tf for optional error_code by detecting the code selector
	 */
	if (tf->code_seg != B64CODE_SEL) {
		tf = (struct trapframe *)(tfp - 1);
	} else {
		eb_printf("error code           0x%lx\n",
		    tf->error_code & 0xffffffff);
		kipcc_panic_field(IPF_ERROR, tf->error_code & 0xffffffff);
	}

	eb_printf("instruction pointer  0x%lx\n", tf->inst_ptr);
	eb_printf("code segment         0x%lx\n", tf->code_seg & 0xffff);
	eb_printf("flags register       0x%lx\n", tf->flags_reg);
	eb_printf("return %%rsp          0x%lx\n", tf->stk_ptr);
	eb_printf("return %%ss           0x%lx\n", tf->stk_seg & 0xffff);
	eb_printf("%%cr2			0x%lx\n", getcr2());

	kipcc_panic_field(IPF_CAUSE, IPCC_PANIC_EARLYBOOT_TRAP);
	kipcc_panic_field(IPF_PC, tf->inst_ptr);
	kipcc_panic_field(IPF_FP, tf->stk_ptr);

	/* grab %[er]bp pushed by our code from the stack */
	fakeframe.old_frame = (bop_frame_t *)*(tfp - 3);
	fakeframe.retaddr = (pc_t)tf->inst_ptr;
	eb_printf("Attempting stack backtrace:\n");
	bop_traceback(&fakeframe);
	bop_panic("unexpected trap in early boot");
}

extern void bop_trap_handler(void);
static gate_desc_t *bop_idt;
static desctbr_t bop_idt_info;

/* XXX shareable? */
/*
 * Install a temporary IDT that lets us catch errors in the boot time code.
 * We shouldn't get any faults at all while this is installed, so we'll
 * just generate a traceback and exit.
 */
static void
early_idt_init(void)
{
	int t;

	bop_idt = (gate_desc_t *)eb_alloc_page();
	bzero(bop_idt, MMU_PAGESIZE);
	for (t = 0; t < NIDT; ++t) {
		/*
		 * Note that since boot runs without a TSS, the
		 * double fault handler cannot use an alternate stack (64-bit).
		 */
		set_gatesegd(&bop_idt[t], &bop_trap_handler, B64CODE_SEL,
		    SDT_SYSIGT, TRP_KPL, 0);
	}
	bop_idt_info.dtr_limit = (NIDT * sizeof (gate_desc_t)) - 1;
	bop_idt_info.dtr_base = (uintptr_t)bop_idt;
	wr_idtr(&bop_idt_info);
}

static void
early_gdt_init(void)
{
	static user_desc_t gdt[NGDT];
	desctbr_t gdtr;

	init_boot_gdt(gdt);
	gdtr.dtr_limit = sizeof (gdt) - 1;
	gdtr.dtr_base = (uint64_t)gdt;
	wr_gdtr(&gdtr);
	load_segment_registers(B64CODE_SEL, 0, 0, 0);
}

static void
protect_ramdisk(void)
{
	uint64_t start, end;

	if (do_bsys_getproplen(NULL, BTPROP_NAME_RAMDISK_START) ==
	    sizeof (uint64_t) &&
	    do_bsys_getprop(NULL, BTPROP_NAME_RAMDISK_START, &start) == 0 &&
	    do_bsys_getproplen(NULL, BTPROP_NAME_RAMDISK_END) ==
	    sizeof (uint64_t) &&
	    do_bsys_getprop(NULL, BTPROP_NAME_RAMDISK_END, &end) == 0) {
		start = P2ALIGN(start, MMU_PAGESIZE);
		end = P2ROUNDUP(end, MMU_PAGESIZE);
		eb_physmem_reserve_range(start, end - start, EBPR_NO_ALLOC);
	}
}

static void
apob_init(void)
{
	const bt_prop_t *apob_prop = find_bt_prop(BTPROP_NAME_APOB_ADDRESS);
	paddr_t apob_pa, apob_limit;

	if (apob_prop == NULL) {
		bop_panic("APOB address property %s is missing; don't "
		    "know how to probe memory ourselves",
		    BTPROP_NAME_APOB_ADDRESS);
	}

	if ((apob_prop->btp_typeflags & DDI_PROP_TYPE_MASK) !=
	    DDI_PROP_TYPE_INT64) {
		bop_panic("Boot-time property %s has incorrect type; can't "
		    "find the APOB without it", BTPROP_NAME_APOB_ADDRESS);
	}

	/*
	 * The APOB is assumed to be physically contiguous.  All known
	 * implementations have this property.
	 */
	apob_pa = (paddr_t)(*(uint64_t *)apob_prop->btp_value);
	apob_limit = eb_phys_bound_usable(apob_pa);

	if (apob_limit == 0) {
		bop_panic("APOB lies in unusable memory region");
	}
	kapob_eb_init(apob_pa, apob_limit);
}

/*
 * BTS: oxide boot
 *
 * This is where we enter the kernel. _start() dummies up the boot_ops and
 * boot_syscalls vectors and jumps off to _kobj_boot().  How does the loader
 * find this entry point?  By the miracle of looking at the ELF e_entry field.
 * Unlike i86pc, we don't enter at a fixed address in locore.s.  We're also
 * called as a function from 64-bit higher-level language code (almost
 * certainly Rust), so we don't need to muck about setting up a stack, nor do
 * we have to deal with the build system contortions and tedious assembly code
 * associated with a 32-bit stub like dboot.  That's been done already, and we
 * don't care to do it again.
 *
 * The contract between us and the loader is described in vm/kboot_mmu.c along
 * with the big theory statement on earlyboot memory management.  Beyond that,
 * this is fairly simple; we do things in the order we do them because:
 *
 * - We need complete boot services (the allocator and real memlists) plus a
 *   skeletal IDT and the ability to panic before we can call into krtld;
 * - We want the allocator to set up the IDT and create the real memlists;
 * - We need access to boot properties to find DRAM because only the SP knows
 *   where the APOB should reside;
 * - We'd like to set up the IDT as early as possible to aid in debugging;
 * - We cannot output any debug messages until we have the console, nor can
 *   we obtain the values of boot properties from the SP without UARTs;
 * - We cannot set up the UARTs until we've set up the MMU because the UARTs
 *   are memory-mapped.
 * - We rely on the loader's pagetables to help us discover what physical
 *   memory is "guaranteed" to be usable for bootstrapping.
 *
 * Thus, the dependency tree for bootstrapping looks like so:
 *
 *       +----------------------+
 *       |  previous stage(s)   |
 *       | contracted interface |<---------------+
 *       +----------------------+                |
 *                  ^                            |
 *                  |                            |
 *            +------------+                     |
 *            |  boot MMU  |                     |
 *            | virt alloc |<--------------+     |
 *            +------------+               |     |
 *                 ^                       |     |
 *                 |                       |     |
 *                 |                       |     |
 *           +------------+             +------------+
 *           | UART setup |             | naive phys |
 *           +------------+             |  allocator |<----------------+
 *               ^      ^               +------------+                 |
 *               |      |                         ^                    |
 *               |      |    +----------------+   |                    |
 *               |      +----| unconditional  |   +------+             |
 *               |           | debug messages |<-----+   |             |
 *               |           +----------------+      |   |             |
 *               |                    ^              |   |             |
 *           +------------+           |            +---------------+   |
 *           | fetch boot |    +-------------+     | IDT setup and |   |
 *           | properties |<---| conditional |     |   panicking   |   |
 *           +------------+    |  debugging  |     +---------------+   |
 *                ^   ^        +-------------+         ^               |
 *                |   |                                |               |
 *                |   |    +---------------------------+               |
 *                |   |    |                                           |
 *                |   |    |             +----------------+            |
 *                |   +---(|)------------| physical space |------------+
 *                |        |             |   enumeration  |
 *                |        |             +----------------+
 *                |        |                      ^
 *                |        |                      |
 *          +------------------+              +----------------+
 *          |  krtld handoff   |------------->|    full RAM    |
 *          | via _kobj_boot() |              | phys allocator |
 *          +------------------+              +----------------+
 *                  ^
 *                  |
 *               +------+
 *               | DONE |
 *               +------+
 *
 * This function is nothing but a topo-sorted implementation of the above.
 * Some of it could be simplified further by the use of more static data, but
 * we're trying to keep the kernel small because it may end up in boot flash.
 *
 * This explanation, along with its VM sibling, doesn't mention much of the
 * SOC-specific grotty work needed to probe the DF, set up DXIO, or contact
 * the SMU.  Rather, the purpose of this code is to provide the earliest
 * foundation upon which those tasks can more comfortably be performed by
 * code in startup.c and the SOC-specific subdirectories.  That code wants to
 * rely on our vast library of utility code in genunix as well as other
 * utility and driver modules, and to use those we must first do enough to
 * convince krtld to load them.  In that sense, our purpose is very much the
 * same as our i86pc counterpart's; we simply have much less to do and far
 * greater knowledge and control of our environment, sufficiently so that one
 * day this might look more like the sun4 code than i86pc.
 */
void
_start(uint64_t ramdisk_paddr, size_t ramdisk_len)
{
	extern void _kobj_boot();
	extern int use_mp;
	struct boot_syscalls *bsp;

	early_cpuid_ident();

	kbm_init();
	oxide_derive_platform();
	bsp = boot_console_init();
	oxide_report_platform();

	kernel_ipcc_init(IPCC_INIT_EARLYBOOT);
	eb_physmem_init(&bm);
	eb_create_properties(ramdisk_paddr, ramdisk_len);

	EB_DBGMSG("\n\n*** Entered illumos in _start()\n");

	eb_set_tunables();

	/*
	 * Fill in the bootops vector; all of this can now work.
	 */
	bootop.bsys_version = BO_VERSION;
	bootop.boot_mem = &bm;
	bootop.bsys_alloc = do_bsys_alloc;
	bootop.bsys_free = do_bsys_free;
	bootop.bsys_getproplen = do_bsys_getproplen;
	bootop.bsys_getprop = do_bsys_getprop;
	bootop.bsys_nextprop = do_bsys_nextprop;
	bootop.bsys_printf = bop_printf;
	bootop.bsys_ealloc = do_bsys_ealloc;

	/*
	 * Get and save the reset vector for MP startup use later.
	 */
	if (do_bsys_getproplen(&bootop, BTPROP_NAME_RESET_VECTOR) !=
	    sizeof (reset_vector) ||
	    do_bsys_getprop(&bootop, BTPROP_NAME_RESET_VECTOR, &reset_vector) !=
	    0 ||
	    reset_vector == 0) {
		eb_printf("missing boot-time property %s; MP disabled.\n",
		    BTPROP_NAME_RESET_VECTOR);
		use_mp = 0;
	}
	if ((reset_vector & 0xffff) != 0xfff0) {
		eb_printf("reset vector %x has invalid offset; MP disabled.\n",
		    reset_vector);
		reset_vector = 0;
		use_mp = 0;
	}

	if (reset_vector != 0) {
		eb_physmem_reserve_range(reset_vector & PAGEMASK, PAGESIZE,
		    EBPR_NO_ALLOC);
	}

	/*
	 * Install a GDT and an IDT to catch early pagefaults (shouldn't
	 * have any).  Also needed for kmdb.
	 */
	EB_DBGMSG("Initializing temporary GDT and IDT: ");
	early_gdt_init();
	early_idt_init();
	EB_DBGMSG("done\n");

	if (prom_debug || kbm_debug) {
		char *bufpage;

		bufpage = do_bsys_alloc(NULL, NULL, MMU_PAGESIZE, MMU_PAGESIZE);
		boot_prop_display(bufpage);
	}

	milan_ccx_physmem_init();
	protect_ramdisk();

	/*
	 * Initialize the APOB boot operations. This will be required for us to
	 * successfully use it as a boot operation vector.
	 */
	apob_init();

	/*
	 * _kobj_boot() vectors us to mlsetup and thence to main(), so there
	 * is no return from this point.
	 */
	_kobj_boot(bsp, NULL, &bootop, NULL);

	/*NOTREACHED*/
}

/* XXX shareable */
/*ARGSUSED*/
int
boot_compinfo(int fd, struct compinfo *cbp)
{
	cbp->iscmp = 0;
	cbp->blksize = MAXBSIZE;
	return (0);
}

/* XXX shareable */
int
bootprop_getval(const char *prop_name, u_longlong_t *prop_value)
{
	int		boot_prop_len;
	char		str[BP_MAX_STRLEN];
	u_longlong_t	value;

	boot_prop_len = BOP_GETPROPLEN(bootops, prop_name);
	if (boot_prop_len < 0 || boot_prop_len >= sizeof (str) ||
	    BOP_GETPROP(bootops, prop_name, str) < 0 ||
	    kobj_getvalue(str, &value) == -1)
		return (-1);

	if (prop_value)
		*prop_value = value;

	return (0);
}

/* XXX shareable */
int
bootprop_getstr(const char *prop_name, char *buf, size_t buflen)
{
	int boot_prop_len = BOP_GETPROPLEN(bootops, prop_name);

	if (boot_prop_len < 0 || boot_prop_len >= buflen ||
	    BOP_GETPROP(bootops, prop_name, buf) < 0)
		return (-1);

	return (0);
}
