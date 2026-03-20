# Map Lives in One Place 

Jak deklarujemy mape (np. BPF_MAP_TYPE_ARRAY) w naszym programie to mimo że ten program może być wykonany na wielu threadach/cpu przez wiele różnych procesów to ta mapa jest w **jendym miejscu w kernelu** i wszystkie te taski odwołują się do tego jednego miejsca.
Nie ma kopii dla danego CPU (można zrobić dla kazdego CPU kopie jedynie poprzez BPF_MAP_TYPE_PERCPU_*).

Dodatkowo funkcja update'ująca elemnty w mapie ma sygnaturę: 
```bpf_map_update_elem(int fd, const void *key, const void *value,__u64 flags)```.
Czyli dostaje pointer do value i key, funkcja która nam zwraca wartość pod kluczem w mapie ma sygnaturę:
```bpf_map_lookup_elem(int fd, const void *key, void *value);```
Również pointery, więc jak będziemy chcieli zmienić klucz to możemy dostać **RACE CONDITION**.

```
CPU 0          CPU 1          CPU 2
  │              │              │   
  └──────────────┴──────────────┘   
     │                  
   kernel memory              
   ┌──────────────────┐       
   │  .bss map        │       
   │  key=0 → {       │       
   │    total_calls=5 │  ← single physical location
   │  }               │       
   └──────────────────┘       
```

Również to zachodzi ze zmiennymi globalnymi. Zmienna globalna zadeklarowana jak poniżej:

```c
u32 free_idx = 0;

SEC("kprobe/do_nanosleep")
int set_map_entry(void *ctx)
{
    free_idx = free_idx + 1;
}
```

To kompilator tak naprawdę tłumaczy to na jednoelemenotwe array, które jest jedno dostępne
dla wszystkich cpu, również C prostą operację ++ tłumaczy na:
```
 What the CPU actually does:
 1. LOAD  — read value from memory into register
 2. ADD   — increment register
 3. STORE — write register back to memory
```

Więc mamy również RACE CONDITION.


# Co zrobić żeby zapobiegać race condition

1) Funkcje operujące na mapach **SĄ ATOMOWE**
```
stackoverflow: https://stackoverflow.com/questions/72191414/thread-safe-operations-on-xdp
Yes, both the bpf_map_update_elem command via the syscall and the helper function update the maps 'atomically', which in this case means that if we go from value 'A' to value 'B' that the program always sees either 'A' or 'B' not some combination of the two(first bytes of 'B' and last bytes of 'A' for example). This is true for all map types. This holds true for all map modifying syscall commands(including bpf_map_delete_elem).
```

Wystarczy używać: ```__sync_fetch_and_add(&free_idx, 1);``` (bądź innych funkcji __sync_*)
Jest to jedna atomowa instrukcja.

```c
__sync_fetch_and_add(&free_idx, 1);
// Compiles to a single atomic CPU instruction:
// x86:   LOCK XADD [mem], 1
// arm64: LDADD / STADD
// The `LOCK` prefix on x86 **locks the memory bus** for that address for the duration of read-modify-write. 
// It's **one indivisible hardware instruction** — no other CPU can touch that memory location mid-operation.
```

## Mimo sync nadal możemy mieć race condition dla struktur

```c
// Source - https://stackoverflow.com/a/72192140
// Posted by Dylan Reimerink
// Retrieved 2026-03-20, License - CC BY-SA 4.0

value = bpf_map_lookup_elem(&hash_map, &key);
if (value) {
    __sync_fetch_and_add(&value->packets, 1);
    __sync_fetch_and_add(&value->bytes, skb->len);
} else {
    struct pair val = {1, skb->len};

    bpf_map_update_elem(&hash_map, &key, &val, BPF_ANY);
}

```
In this example, the two struct fields are atomically updated, but not together, it is still possible for the packets to have incremented but bytes not yet. If you want to avoid this you need to use a spinlock(using the bpf_spin_lock and bpf_spin_unlock helpers).

# SPin Locks

```c
struct concurrent_element {
    struct bpf_spin_lock semaphore;
    int count;
}

// Mapa która jako value ma strukturę
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, int);
    __type(value, struct concurrent_element);
    __uint(max_entries, 100);
} concurrent_map SEC(".maps");

SEC("tp_btf/sys_enter")
int sys_enter_count(void *ctx) {
    int key = 0;
    struct concurrent_element init_value = {};
    struct concurrent_element *read_value;
    bpf_map_update_elem(&concurrent_map, &key, &init_value, BPF_NOEXIST);

    read_value = bpf_map_lookup_elem(&concurrent_map, &key);
    if(!read_value)
    {
        return 0;
    }

    bpf_spin_lock(&read_value->semaphore);
    read_value->count += 1;
    bpf_spin_unlock(&read_value->semaphore);
    return 0;
}

```