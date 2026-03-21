#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

/*
    Copy by ssh:  scp -P 2222 ./oomk.bpf.c root@127.0.0.1:~/bpf_oom_killer/
    #######################################################################
    Where to look for files:
    - kernel functions etc - /usr/src/linux-headers-$(uname -r)/
    - tracepoints - /sys/kernel/debug/tracing 
        check members of tracepoint ```args``` struct: 
        ```bpftrace -vl tracepoint:syscalls:sys_enter_openat```
    - kprobes - 
    - structs - /usr/src/linux-headers-$(uname -r)/arch/x86/include/asm/ptrace.h 
*/


// KMALLOC_MAX_SIZE = 1UL << 22 = 4,194,304 bytes = 4MB
// So with such number as max_entries each entry can have at most 400bytes
#define MAX_ENTRIES 8192

struct {
    // We use LRU map since if there is no free space left in our map it will remove
    // least used entry from it, and because we need to pre-allocate our map it will 
    // have fixed size, so such mechanism is useful.
    __uint(type, BPF_MAP_TYPE_LRU_HASH); 
    __uint(max_entries, MAX_ENTRIES);
    __type(key, __u32);
    __type(value, __u64);
} sysinfo_map SEC(".maps");


// KPROBE - gets function arguments, but before function execution, so these args are
// NOT SET YET
SEC("kprobe/si_meminfo")
int check_ram_usage_entry(void *ctx)
{
    struct task_struct *task = (void *)bpf_get_current_task();
    struct sysinfo *sys_info = (struct sysinfo *)PT_REGS_PARM1((struct pt_regs*)ctx);

    __u16 n_proc = BPF_CORE_READ(sys_info, procs); 
    __u64 free_ram_addr = (__u64)&(sys_info->freeram);
    __u64 total_ram_addr = (__u64)&(sys_info->totalram);
    // pid_t pid;
    // pid = bpf_get_current_pid_tgid() >> 32;

    __u32 key = 0;
    bpf_map_update_elem(
        &sysinfo_map, 
        &key, 
        &free_ram_addr,
        BPF_ANY
    );

    key = 1;
    bpf_map_update_elem(
        &sysinfo_map, 
        &key, 
        &total_ram_addr,
        BPF_ANY
    );

    return 0;
}

// KRETPROBE - gets ONLY RETURN VALUE, in ctx apart from that is trash, fires after
// function execution
// useful docs: https://docs.ebpf.io/ebpf-library/libbpf/ebpf/BPF_KRETPROBE/
SEC("kretprobe/si_meminfo")
int check_ram_usage_exit(void *ctx)
{
    struct task_struct *task = (void *)bpf_get_current_task();

    // pid_t pid;
    // pid = bpf_get_current_pid_tgid() >> 32;
    __u32 key = 0;
    __u64 *free_ram_addr_ptr = bpf_map_lookup_elem(&sysinfo_map, &key);

    if (!free_ram_addr_ptr)
    {
        bpf_printk("free_ram_addr is null");
        return 0;
    }

    __u64 free_ram_addr = *free_ram_addr_ptr;
    __u64 free_ram_val = 0;

    bpf_probe_read_kernel((void*)&free_ram_val, sizeof(free_ram_val), (void*)free_ram_addr);


    key = 1;
    __u64 *total_ram_addr_ptr = bpf_map_lookup_elem(&sysinfo_map, &key);

    if (!total_ram_addr_ptr)
    {
        bpf_printk("free_ram_addr is null");
        return 0;
    }

    __u64 total_ram_addr = *total_ram_addr_ptr;
    __u64 total_ram_val = 0;

    bpf_probe_read_kernel((void*)&total_ram_val, sizeof(total_ram_val), (void*)total_ram_addr);

    bpf_printk("Free Ram: %u / %u\n", free_ram_val, total_ram_val);

    return 0;
}

char LICENSE[] SEC("license") = "GPL";