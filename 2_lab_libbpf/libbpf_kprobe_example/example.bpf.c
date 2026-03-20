/* example.bpf.c */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

// podlaczamy sie do kernel probe (dynamic, czyli moze byc zmienne api) do syscalla
// nanosleep, wtedy kiedy on zaczyna sie wykonywac
// Za każdym razem kiedy jakiś proces zrobi sleepa to to się wywoła
SEC("kprobe/do_nanosleep")
int handle(void *ctx)
{
    // Kernel functions (including those with variable argument list) get the first 6 arguments in rdi, rsi, rdx, rcx, r8, r9

    // The context pointer (void *ctx) is a struct pt_regs * — you read function arguments with PT_REGS_PARM1(ctx), PT_REGS_PARM2(ctx), etc. from <bpf/bpf_tracing.h>.

    // Part of struct pt_regs:
    // 
    // struct pt_regs {
    //     long unsigned int r15;
    //     long unsigned int r14;
    //     long unsigned int r13;
    //     long unsigned int r12;
    //     long unsigned int bp;
    //     long unsigned int bx;
    //     long unsigned int r11;
    //     long unsigned int r10;
    //     long unsigned int r9;
    //     long unsigned int r8;
    //     long unsigned int ax;
    //      ...
    // }

    // How to read kprobe function parameters hands-on:https://stackoverflow.com/questions/70905815/how-to-read-all-parameters-from-a-function-ebpf 

    struct timespec64 *ts = 
        (struct timespec64 *) PT_REGS_PARM1( (struct pt_regs *) ctx );
    long seconds = BPF_CORE_READ(ts, tv_sec);


    // task_struct ma poteznie wiele memberow w sobie
    struct task_struct *task = (void *)bpf_get_current_task();

    // Makro BPF_CORE_READ: to safely read a field (tgid - thread group ID) from a 
    // kernel structure at runtime.
    // BPF_CORE_READ(task, tgid) safely reads the tgid field using CO-RE relocations — if the field offset changes in a future kernel, libbpf adjusts it automatically at load time.
    int pid = BPF_CORE_READ(task, tgid);
    char comm[16];
    // wczytujemy comm - nazwa komendy/procesu
    BPF_CORE_READ_STR_INTO(&comm, task, comm);


    if (seconds == 2137)
    {
        bpf_printk("2137 sleep happened. PID %d (%s) is sleeping", pid, comm);
    }
    bpf_printk("PID %d (%s) is sleeping", pid, comm);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";