#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

// Definujemy mape
struct {
    // __uint to makro
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 64);
}