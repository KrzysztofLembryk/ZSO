#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

// jak kompilowac
// flaga -mcpu=v3 potrzebna żeby funkcje __sync_fetch działały tak jak w dokumentacji
// link: https://github.com/cilium/pwru/issues/389
// clang --target=bpf -D__TARGET_ARCH_x86 -mcpu=v3 -g -O2 -c mapex.bpf.c -o mapex.bpf.o 
// bpftool gen skeleton mapex.bpf.o name mapex > mapex.skel.h 
// gcc mapex.user.c -o mapex -lbpf -lelf -lz 
// ./mapex


/* Array map: key (u32 index) → value (u32) */
struct {
    // specyfikujemy typ mapy, array_type znaczy, ze w pamieci nasza mapa to bedzie
    // zwykla tablica i indeks to bedzie klucz
    __uint(type, BPF_MAP_TYPE_ARRAY); 
    // maksymalnie 64 elementy, kernel prealokuje dokladnie tyle miejsca
    __uint(max_entries, 4);
    // klucz musi byc u32 dla powyzszego typu mapy i z zakresu [0, 63]
    __type(key, __u32);
    __type(value, __u32);
} my_map SEC(".maps");

// Global variable, should work like in normal program (recently introduced to bpf,
// earlier we would need to create a MAP_ARRAY with size 1, now its done 
// automatically)
// but we MIGHT have RACE CONDITIONS
__u32 free_idx = 0;

SEC("kprobe/do_nanosleep")
int set_map_entry(void *ctx)
{
    u64 pid_tgid = bpf_get_current_pid_tgid();
    __u32 pid = pid_tgid >> 32;
    // zwiekszamy atomowo free_idx, __sync_fetch_and_add zwraca stara wartosc,
    // uzywamy jej jako key i updateujemy mape, bpf_map_update_elem jest ATOMOWE
    __u32 key = __sync_fetch_and_add(&free_idx, 1) % 4;


    bpf_printk("arr idx: %u, user-PID %u is sleeping", key, pid);
    // BPF_ANY - create new element or update existing 
    bpf_map_update_elem(&my_map, &key, &pid, BPF_ANY);

    return 0;
}

char LICENSE[] SEC("license") = "GPL";