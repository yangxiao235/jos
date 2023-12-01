#ifndef JOS_KERN_MONITOR_H
#define JOS_KERN_MONITOR_H
#ifndef JOS_KERNEL
# error "This is a JOS kernel header; user programs should not #include it"
#endif

struct Trapframe {
    unsigned eip;
    unsigned ebp;
    unsigned args[5];
};

// Activate the kernel monitor,
// optionally providing a trap frame indicating the current state
// (NULL if none).
void monitor(struct Trapframe *tf);

// Functions implementing monitor commands.
int mon_help(int argc, char **argv, struct Trapframe *tf);
int mon_kerninfo(int argc, char **argv, struct Trapframe *tf);
int mon_backtrace(int argc, char **argv, struct Trapframe *tf);
int mon_pagetable(int argc, char **argv, struct Trapframe *tf);
int mon_showmappings(int argc, char **argv, struct Trapframe *tf);
int mon_chgmapping(int argc, char **argv, struct Trapframe *tf);
int mon_dumppgtbl(int argc, char **argv, struct Trapframe *tf);
int mon_dumppmem(int argc, char **argv, struct Trapframe *tf);
int mon_dumpvmem(int argc, char **argv, struct Trapframe *tf);
int mon_setmem(int argc, char **argv, struct Trapframe *tf);
int mon_dumppgstru(int argc, char **argv, struct Trapframe *tf);

#endif	// !JOS_KERN_MONITOR_H
