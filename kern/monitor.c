// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "pgtable", "Display page table information. \n"
		"\tUsage: pgtable [-d <pdx_start> <pgx_end>] [-t <ptx_start> <ptx_end>] [ -r <va_start> <va_end>] [-v]\n"
		"\t-d, -t  list page entries by idx\n"
		"\t-r      list page entries by virtual address range\n"
		"\t-v      by default, pgtable will ignore entries not present, using it to show the ignored entries", mon_pagetable},
	{ "showmappings", "Dsiplay virtual adress mapping information\n"
		"\tUsage: showmappings [<va> ...]"
		, mon_showmappings},
	{ "chgmapping", "Change the permissions of any mappings\n" 
		"\tUsage: chgmapping [-s|-c] <va> [<perm>]",
		mon_chgmapping},
	{ "dumppgstru", "Dump the contents of the page structure", mon_dumppgstru},
	{ "dumppmem", "Dump the contents of memory which specified by physical address", mon_dumppmem},
	{ "dumpvmem", "Dump the contents of memory which specified by virtual address", mon_dumpvmem},
	{ "setmem", "Set memory contents", mon_setmem},
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

unsigned read_eip();

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		(end-entry+1023)/1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	unsigned *ebp;
	unsigned eip;
	struct Eipdebuginfo info;
	cprintf("Stack backtrace:\n");
	ebp = (unsigned *)(void *)read_ebp();
	while (ebp) {
		eip = *(ebp + 1);
		cprintf("  ebp %08x  eip %08x  args %08x %08x %08x %08x %08x\n", 
			ebp,
			eip,
			*(ebp + 2),
			*(ebp + 3),
			*(ebp + 4),
			*(ebp + 5),
			*(ebp + 6)
			);
		debuginfo_eip(eip, &info);
		cprintf("%s:%d: %.*s+%d\n", info.eip_file, info.eip_line, 
			info.eip_fn_namelen, info.eip_fn_name, eip-info.eip_fn_addr);
		ebp = (unsigned *)(void *)*ebp;
	}
	return 0;
}

#define EXPR_TO_BOOL(e) ((e) ? 1 : 0)

int 
mon_pagetable(int argc, char **argv, struct Trapframe *tf)
{
	extern pde_t *kern_pgdir;
	pde_t *pde;
	int start_pdx = 0, end_pdx = 1024;
	int start_ptx = 0, end_ptx = 1024;
	void *start_virt = 0, *end_virt = 0;
	int detail = 0;
	pte_t *pte;
	pte_t *pte_base;
	char *arg;
	while (argc-- > 0) {
		arg = *argv++;
		if (!strncmp(arg, "-d", 2) && argc >= 2) {
			start_pdx = atoi(argv[0]);
			end_pdx = atoi(argv[1]);
			end_pdx = end_pdx < 1024 ? end_pdx : 1024;
			argc -= 2;
			argv += 2;
		}
		if (!strncmp(arg, "-t", 2) && argc >= 2) {
			start_ptx = atoi(argv[0]);
			end_ptx = atoi(argv[1]);
			end_ptx = end_ptx < 1024 ? end_ptx : 1024;
			argc -= 2;
			argv += 2;
		}
		if (!strncmp(arg, "-r", 2) && argc >= 2) {
			start_virt = (void *)(uint32_t)strtol(argv[0], NULL, 16);
			end_virt = (void *)(uint32_t)strtol(argv[1], NULL, 16);
			argc -= 2;
			argv += 2;
		}
		if (!strncmp(arg, "-v", 2)) {
			detail = 1;
		}
	}
	if (start_virt < end_virt) {
		start_pdx = PDX(start_virt);
		end_pdx = PDX(end_virt);
		start_ptx = PTX(start_virt);
		end_ptx = PTX(end_virt);
	}
	assert(start_pdx >= 0 && start_pdx < 1024);
	assert(end_pdx >= 0 && end_pdx <= 1024);
	assert(start_ptx >= 0 && start_ptx < 1024);
	assert(end_ptx >= 0 && end_ptx <= 1024);

	cprintf("Page Directory Base: 0x%x\n", kern_pgdir);
	int cur_end_ptx;
	for (int i = start_pdx; i < end_pdx; ++i) {
		pde = kern_pgdir + i;
		if (!detail && !(*pde & PTE_P)) {
			continue;
		}
		cprintf("entry[%04d]: virtual(0x%08x), table-base(0x%08x), P-W-U-PS(%d-%d-%d-%d)\n", 
			i,
			i<<PDXSHIFT, 
			PTE_BASE(*pde), 
			EXPR_TO_BOOL(*pde&PTE_P), 
			EXPR_TO_BOOL(*pde&PTE_W), 
			EXPR_TO_BOOL(*pde&PTE_U), 
			EXPR_TO_BOOL(*pde&PTE_PS));

		if (*pde&PTE_PS) {
			continue;
		}
		pte_base = KADDR(PTE_BASE(*pde));
		cur_end_ptx = (i == end_pdx) ? end_pdx : 1024;
		for (int it = start_ptx; it < cur_end_ptx; ++it) {
			pte = pte_base + it;
			if (!detail && !(*pte & PTE_P)) {
				continue;
			}
			cprintf("    entry[%04d]: virtual(0x%08x), frame-base(0x%08x), P-W-U(%d-%d-%d)\n", 
				it,
				(i<<PDXSHIFT) + (it<<PTXSHIFT), 
				PTE_BASE(*pte), 
				EXPR_TO_BOOL(*pte&PTE_P), 
				EXPR_TO_BOOL(*pte&PTE_W), 
				EXPR_TO_BOOL(*pte&PTE_U));
		}
		start_ptx = 0;
	}
	return 0;
}

int
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
	--argc;
	++argv;

	void *virt_addr[16];
	char *arg_end;
	argc = argc > 16 ? 16 : argc;
	arg_end = argv[argc];
	void **pv = virt_addr;
	while (*argv != arg_end) {
		*pv++ = (void *)(uint32_t)strtol(*argv++, NULL, 16);
	}
	for (int i = 0; i < argc; ++i) {
		cprintf("Virtual Address 0x%08x mapping\n", virt_addr[i]);
		pde_t *p_pde = kern_pgdir + PDX(virt_addr[i]);
		if (!(*p_pde & PTE_PS) && !(*p_pde & PTE_P)) {
			cprintf("    None\n");
			continue;
		}
		cprintf("    page dir entry: virt-phys(0x%8x-0x%08x) P-W-U-PS(%d-%d-%d-%d)\n",
			KADDR(PTE_BASE(*p_pde)),
			PTE_BASE(*p_pde),
			EXPR_TO_BOOL(*p_pde&PTE_P),
			EXPR_TO_BOOL(*p_pde&PTE_W),
			EXPR_TO_BOOL(*p_pde&PTE_U),
			EXPR_TO_BOOL(*p_pde&PTE_PS));
		if (*p_pde&PTE_PS) {
			continue;
		}
		pte_t *p_pte = ((pte_t *)KADDR(PTE_BASE(*p_pde))) + PTX(virt_addr[i]);
		cprintf("    page table entry: virt-phys(0x%8x-0x%08x) P-W-U(%d-%d-%d)\n",
			KADDR(PTE_BASE(*p_pte)),
			PTE_BASE(*p_pte),
			EXPR_TO_BOOL(*p_pte&PTE_P),
			EXPR_TO_BOOL(*p_pte&PTE_W),
			EXPR_TO_BOOL(*p_pte&PTE_U));
	}
	return 0;
}

int mon_chgmapping(int argc, char **argv, struct Trapframe *tf)
{
	int set_perm = 0;
	int perm = 0;
	void *va = NULL;
	if (argc < 2) {
		return -1;
	}
	if (!(strncmp(argv[1], "-c", 2)) && argc >= 3) {
		set_perm = 0;
	} else if (!(strncmp(argv[1], "-s", 2)) && argc >= 4) {
		set_perm = 1;
	} else {
		return -1;
	}
	va = (void *)(uint32_t)strtol(argv[2], NULL, 16);
	if (set_perm) {
		perm = strtol(argv[3], NULL, 16);
	}
	pde_t *pde = &kern_pgdir[PDX(va)];
	if (*pde & PTE_PS) {
		if (set_perm) {
			*pde |= perm;
		} else {
			*pde &= ~0x7;
		}
		return 0;
	}
	if (!(*pde & PTE_P)) {
		cprintf("page directory entry of 0x%x not present\n", va);
		return 0;
	}
	pte_t *pte = &((pte_t *)KADDR(PTE_BASE(*pde)))[PTX(va)];
	if (set_perm) {
		*pte |= perm;
	} else {
		*pte &= ~0x7;
	}
	// fixme: 是否要刷新tlb?
	tlbflush();
	return 0;
}
int mon_dumppgstru(int argc, char **argv, struct Trapframe *tf)
{
	uint32_t start = 0;
	uint32_t end = npages;
	if (argc >= 3) {
		start = strtol(argv[1], NULL, 16);
		end = strtol(argv[2], NULL, 16);
	}
	if (start >= end) {
		cprintf("error: invalid args\n");
		return 0;
	}
	cprintf("page struct base: 0x%x\n", pages);
	int cols = 16;
	cprintf("%*s", 10, "");
	for (uint32_t i = 0; i < cols; ++i) {
		cprintf("%02x ", i);
	}
	cprintf("\n");
	for (uint32_t i = 0; i < 60; ++i) {
		cprintf("-");
	}
	for (uint32_t i = start; i < end; ++i) {
		if (!(i%cols)) {
			cprintf("\n%04x: ", i);
		}
		cprintf("%02d ", pages[i].pp_ref);
	}
	cprintf("\n");
	return 0;
}
// todo 加入字符串支持
static void dump_mem(const uint8_t *base, uint32_t len)
{
	int cols = 16;
	cprintf("%*s", 10, "");
	for (uint32_t i = 0; i < cols; ++i) {
		cprintf("%02x ", i);
	}
	cprintf("\n");
	for (uint32_t i = 0; i < 60; ++i) {
		cprintf("-");
	}
	for (uint32_t i = 0; i < len; ++i) {
		if (!(i%cols)) {
			cprintf("\n%08x: ", ((uint8_t *)base + i));
		}
		cprintf("%02x ", base[i]);
	}
	cprintf("\n");
}

int mon_dumppmem(int argc, char **argv, struct Trapframe *tf)
{
	// dumppmem <phys_addr> <len>
	if (argc < 3) {
		return -1;
	}
	physaddr_t phys_base = (uint32_t)strtol(argv[1], NULL, 16);
	uint32_t len = strtol(argv[2], NULL, 10);
	// 仅支持内核物理地址空间(内核逻辑地址可以通过简单的运算得到). 用户态需要去查找页表来决定, 暂不实现
	// 在[0, 256MB)以外的空间不属于内核空间
	physaddr_t phys_top = (~(0UL)) - KERNBASE;
	if (phys_base > phys_top || (phys_base + len) > phys_top) {
		cprintf("phys_addr >= 0x%x is not supported\n", phys_top);
		return 0;
	}
	uint8_t *base = (void *)(phys_base + KERNBASE);
	dump_mem(base, len);
	return 0;
}
int mon_dumpvmem(int argc, char **argv, struct Trapframe *tf)
{
	// dumpvmem <base> <len> 
	if (argc < 3) {
		return -1;
	}
	uint8_t *base = (uint8_t *)(uint32_t)strtol(argv[1], NULL, 16);
	uint32_t len = strtol(argv[2], NULL, 10);
	dump_mem(base, len);
	return 0;
}

int mon_setmem(int argc, char **argv, struct Trapframe *tf)
{
	// setmem <base> <len> <n>
	if (argc < 4) {
		return -1;
	}
	uint8_t *base = (uint8_t *)(uint32_t)strtol(argv[1], NULL, 16);
	uint32_t len = strtol(argv[2], NULL, 10);
	uint8_t n = strtol(argv[3], NULL, 10);
	for (uint32_t i = 0; i < len; ++i) {
		base[i] = n;
	}
	return 0;
}
/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");


	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}

// return EIP of caller.
// does not work if inlined.
// putting at the end of the file seems to prevent inlining.
unsigned
read_eip()
{
	uint32_t callerpc;
	__asm __volatile("movl 4(%%ebp), %0" : "=r" (callerpc));
	return callerpc;
}
