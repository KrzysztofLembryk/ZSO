# Libpf i programy w c
- bardzo spoko i użyteczne żeby zrozumieć jak działa bpf: ```man bpf```, są tam też duże snippety kodu użyteczne i przykłady

# Hello world (bez libbpf)
```c
/* hello_bpf.c — load a BPF program that prints via bpf_trace_printk */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <linux/bpf.h>

/* BPF_REG_10 is the frame-pointer register; define a readable alias */
#ifndef BPF_REG_FP
#define BPF_REG_FP BPF_REG_10
#endif

static inline int sys_bpf(enum bpf_cmd cmd, union bpf_attr *attr,
                           unsigned int size)
{
    // numer __NR_bpf - syscalla w bpf,
    return syscall(__NR_bpf, cmd, attr, size);
}

int main(void)
{
    /*
     * BPF program that calls bpf_trace_printk("Hello from BPF!\n").
     *
     * The string is pushed onto the stack in two 8-byte halves
     * (little-endian), then trace_printk is called with r1=stack ptr,
     * r2=length, and finally r0 is set to 0 and we exit.
     *
     * Equivalent pseudo-code:
     *   char fmt[] = "Hello from BPF!";
     *   bpf_trace_printk(fmt, sizeof(fmt));
     *   return 0;
     */
    // w tej tablicy mamy kod w języku kernelowym bpf, taki a'la assembler
    // ale są inne instrukcje przygotowane pod bpf
    struct bpf_insn prog[] = {
        /* store "Hello fr" (little-endian) at fp-16 */
        { .code = BPF_ST | BPF_DW | BPF_MEM,
          .dst_reg = BPF_REG_FP, .src_reg = 0,
          .off = -16, .imm = 0x6f727620 },       /* "o fr" (upper) — see note */
        /* We need two 32-bit stores for each 8-byte chunk because
         * BPF_ST | BPF_DW stores a 32-bit imm sign-extended to 64 bits.
         * Instead, we use 32-bit (BPF_W) stores for full control. */

        /* fp-16: "Hell" */
        { .code = BPF_ST | BPF_W | BPF_MEM,
          .dst_reg = BPF_REG_FP, .src_reg = 0,
          .off = -16, .imm = 0x6c6c6548 },       /* "lleH" LE = "Hell" */
        /* fp-12: "o fr" */
        { .code = BPF_ST | BPF_W | BPF_MEM,
          .dst_reg = BPF_REG_FP, .src_reg = 0,
          .off = -12, .imm = 0x7266206f },       /* "rf o" LE = "o fr" */
        /* fp-8: "om B" */
        { .code = BPF_ST | BPF_W | BPF_MEM,
          .dst_reg = BPF_REG_FP, .src_reg = 0,
          .off = -8, .imm = 0x42206d6f },        /* "B mo" LE = "om B" */
        /* fp-4: "PF!\0" */
        { .code = BPF_ST | BPF_W | BPF_MEM,
          .dst_reg = BPF_REG_FP, .src_reg = 0,
          .off = -4, .imm = 0x00214650 },        /* "\0!FP" LE = "PF!\0" */

        /* r1 = fp - 16  (pointer to format string) */
        { .code = BPF_ALU64 | BPF_MOV | BPF_X,
          .dst_reg = BPF_REG_1, .src_reg = BPF_REG_FP,
          .off = 0, .imm = 0 },
        { .code = BPF_ALU64 | BPF_ADD | BPF_K,
          .dst_reg = BPF_REG_1, .src_reg = 0,
          .off = 0, .imm = -16 },

        /* r2 = 17  (length including '\n' + '\0'... we use 16 for safety) */
        { .code = BPF_ALU64 | BPF_MOV | BPF_K,
          .dst_reg = BPF_REG_2, .src_reg = 0,
          .off = 0, .imm = 16 },

        /* call bpf_trace_printk (helper #6) */
        { .code = BPF_JMP | BPF_CALL,
          .dst_reg = 0, .src_reg = 0,
          .off = 0, .imm = 6 },

        /* r0 = 0 */
        { .code = BPF_ALU64 | BPF_MOV | BPF_K,
          .dst_reg = BPF_REG_0, .src_reg = 0,
          .off = 0, .imm = 0 },

        /* exit */
        { .code = BPF_JMP | BPF_EXIT,
          .dst_reg = 0, .src_reg = 0,
          .off = 0, .imm = 0 },
    };

    // Trzeba ten powyzszy bpf zzaladowac do kernela, wiec robimy pamiec (bufor)
    // do ktorego to zaladujemy
    char log_buf[4096];
    memset(log_buf, 0, sizeof(log_buf));

    union bpf_attr attr;
    memset(&attr, 0, sizeof(attr));
    // Podpięcie do eventu jest zaimplementowane przez ustawienei BPF_PROG_TYPE_SOCKET_FILTER
    // kazdy ruch na socketcie to event i wtedy nasz bpf sie uruchamia i wypisuje hello world,
    // przy kazdym WRITE na socket sie to wykona 
    attr.prog_type = BPF_PROG_TYPE_SOCKET_FILTER; // jesli ruch na socketcie jest 
    // to wtedy nasz bpf program sie wykona
    attr.insns     = (unsigned long)prog;
    attr.insn_cnt  = sizeof(prog) / sizeof(prog[0]);
    attr.license   = (unsigned long)"GPL";
    attr.log_level = 1;
    attr.log_size  = sizeof(log_buf);
    attr.log_buf   = (unsigned long)log_buf;

    // syscall zwraca nam file_descriptor, i dopóki jest otwarty ten file descriptor
    // to kernel trzyma przy życiu ten program bpf'owy. Ogólnie nasz program bpf uruchomi się
    // tylko gdy jest jakiś event na socketcie, więc jak chcemy wyłączyć nasz bpf program to zamykamy file_descirptor
    int prog_fd = sys_bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
    if (prog_fd < 0) {
        fprintf(stderr, "BPF_PROG_LOAD failed: %s\nVerifier log:\n%s\n",
                strerror(errno), log_buf);
        return EXIT_FAILURE;
    }
    printf("BPF program loaded, fd = %d\n", prog_fd);
    printf("Verifier log:\n%s\n", log_buf);

    /* Attach to a socket and send a packet to trigger the program */
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) < 0) {
        perror("socketpair");
        return EXIT_FAILURE;
    }

    if (setsockopt(sv[0], SOL_SOCKET, SO_ATTACH_BPF,
                    &prog_fd, sizeof(prog_fd)) < 0) {
        perror("SO_ATTACH_BPF");
        return EXIT_FAILURE;
    }

    /* Sending data through the socket triggers the BPF filter */
    write(sv[1], "x", 1);
    printf("Packet sent — check the trace log:\n");
    printf("  sudo cat /sys/kernel/debug/tracing/trace_pipe\n");

    close(sv[0]);
    close(sv[1]);
    close(prog_fd);
    return EXIT_SUCCESS;
}

```

# Program odrzucony przez weryfikator

```c
/* verifier_errors.c — three programs the verifier will reject */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/bpf.h>

static inline int sys_bpf(enum bpf_cmd cmd, union bpf_attr *attr,
                           unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

// Sprobujemy zaladowac trzy programy
static void try_load(const char *label, struct bpf_insn *insns, int cnt)
{
    char log_buf[4096];
    memset(log_buf, 0, sizeof(log_buf));

    union bpf_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
    attr.insns     = (unsigned long)insns;
    attr.insn_cnt  = cnt;
    attr.license   = (unsigned long)"GPL";
    attr.log_level = 1;
    attr.log_size  = sizeof(log_buf);
    attr.log_buf   = (unsigned long)log_buf;

    int fd = sys_bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
    printf("=== %s ===\n", label);
    if (fd < 0)
        printf("Rejected: %s\nVerifier log:\n%s\n\n", strerror(errno), log_buf);
    else {
        printf("Unexpectedly accepted!\n\n");
        close(fd);
    }
}

int main(void)
{
    /* --- Case 1: empty program (zero instructions) --- */
    // Weryfikator odrzuci bo dalismy program ktory nic nie robi, a nie mozna takiego dac
    {
        printf("--- Case 1: empty program (0 instructions) ---\n");
        struct bpf_insn prog[] = {
            /* nothing */
            { 0 }  /* placeholder so the array is non-empty in C */
        };
        try_load("Empty program", prog, 0);
    }

    /* --- Case 2: no exit instruction (falls off the end) --- */
    // Programy sa zweryfikowane, wiec musza miec sekwencyjna linia po lini instrukcje
    // i na koncu MUSI BYC EXIT
    {
        printf("--- Case 2: no exit instruction ---\n");
        struct bpf_insn prog[] = {
            /* r0 = 0, but no BPF_EXIT follows */
            { .code = BPF_ALU64 | BPF_MOV | BPF_K,
              .dst_reg = BPF_REG_0, .src_reg = 0,
              .off = 0, .imm = 0 },
        };
        try_load("No exit", prog, 1);
    }

    /* --- Case 3: unreachable code after exit --- */
    // Dead code za exitem, unreachable, tez nie jest pozwolone
    {
        printf("--- Case 3: unreachable code after exit ---\n");
        struct bpf_insn prog[] = {
            { .code = BPF_ALU64 | BPF_MOV | BPF_K,
              .dst_reg = BPF_REG_0, .src_reg = 0,
              .off = 0, .imm = 0 },           /* r0 = 0 */
            { .code = BPF_JMP | BPF_EXIT,
              .dst_reg = 0, .src_reg = 0,
              .off = 0, .imm = 0 },           /* exit */
            /* dead code — verifier rejects this */
            { .code = BPF_ALU64 | BPF_MOV | BPF_K,
              .dst_reg = BPF_REG_0, .src_reg = 0,
              .off = 0, .imm = 1 },           /* r0 = 1 (unreachable) */
        };
        try_load("Unreachable code", prog, 3);
    }
    // Pointery są w bpf, ale weryfikator wie co pod nimi jest i na nieduzo pozwala

    return 0;
}
```

# Uzycie mapy
Bardzo przydatna w zadaniach

```c
/* map_demo.c — create a map, write a value, pause for inspection */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/bpf.h>

static inline int sys_bpf(enum bpf_cmd cmd, union bpf_attr *attr,
                           unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

int main(void)
{
    /* 1. Create a BPF_MAP_TYPE_HASH: up to 64 entries, key=u32, value=u32 */
    union bpf_attr map_attr;
    memset(&map_attr, 0, sizeof(map_attr));
    map_attr.map_type    = BPF_MAP_TYPE_HASH;
    map_attr.key_size    = sizeof(__u32);
    map_attr.value_size  = sizeof(__u32);
    // Musimy podac jak duza ta mapa jest, co moze byc dosyc niewygodne, bo czesto trzeba dynamicznie
    // tworzzyc rzeczy ktorych wielkosci nie znamy
    map_attr.max_entries = 64;

    int map_fd = sys_bpf(BPF_MAP_CREATE, &map_attr, sizeof(map_attr));
    if (map_fd < 0) {
        perror("BPF_MAP_CREATE");
        return EXIT_FAILURE;
    }
    printf("Map created, fd = %d\n", map_fd);

    /* 2. Write key=42 → value=12345 */
    __u32 key   = 42;
    __u32 value = 12345;

    union bpf_attr upd;
    memset(&upd, 0, sizeof(upd));
    upd.map_fd = map_fd;
    upd.key    = (unsigned long)&key;
    upd.value  = (unsigned long)&value;
    upd.flags  = BPF_ANY;

    if (sys_bpf(BPF_MAP_UPDATE_ELEM, &upd, sizeof(upd)) < 0) {
        perror("BPF_MAP_UPDATE_ELEM");
        return EXIT_FAILURE;
    }
    printf("Set map[%u] = %u\n", key, value);

    /* 3. Pause so bpftool can inspect the map */
    printf("\nProcess paused — use bpftool in another terminal.\n");
    printf("Press Enter to exit and clean up...\n");
    getchar();

    close(map_fd);
    return EXIT_SUCCESS;
}
```

!!! MAPY są globalne, w zad1 trzeba bedzie zliczac rzeczy w mapie i potem z uzyciem bpftool bedziemy mogli
sobie podejrzec warrosci w tej mapie

- Jak wyłączymy program i fd sie zamknie to potem ta mapa zniknie i jest usuwana
- jak potem wlaczymy znowu nasz program to dostaniemy mape ALE JEJ ID SIE ZMIENI

# libbpf

```c
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>


// Dzieki makru SEC mozemy napisac latwo do jakiej funkcji w kernelu chcemy sie wpiac
SEC("kprobe/do_nanosleep")
int handle(void *ctx)
{
    // funkcja zwraca int64 i w nim dwie wartosci, jedne 32 bity to jeden kawalek, drugie to drugi
    // a my chcemy tylko jeden
    int pid = bpf_get_current_pid_tgid() >> 32;
    bpf_printk("PID %d is sleeping", pid);

    return 0;
}

// GPL zaraza twoj kod GPLem
char LICENSE[] SEC("license") = "GPL";
```

Plik example.bpf.o to ekwiwalent tego kodu maszynowego ktory z palca wczesniej pisalismy,
teraz ten kod musimy jeszcze zaladowac.

## LOADER
```c
#include <unistd.h>
// Nie mam tego naglowka na poczatku, ale go generuje bpftool
#include "example.skel.h"

int main()
{
    // W C nie ma wyjatkow, wiec jak cos nie pojdzie dobrze, to musimy zrobic tryb a'la wyjatki 
    // bo musimy posprzatac i wtedy uzywamy goto zeby pojsc do cleanup ktory zawsze jest na koncu.
    // A na koncu po pause to cleanup tez sie sam wykona bo jest nastepny w kolejnosci 
    struct example *skel;
    int err = 0;

    skel = example__open();
    if (!skel)
            goto cleanup;

    err = example__load(skel);
    if (err)
            goto cleanup;

    err = example__attach(skel);
    if (err)
            goto cleanup;

    pause();

cleanup:
      example__destroy(skel);
      return err;
}
```

W zadaniu 1 duzym trzeba bedzie sie powpinac do tracepointow jak i kprobe np. sys_write


Żeby uniknąć problemów z racing conditions na mapie musimy użyć ```__sync_fetch_and_add```
warto sprawdzic ```man``` zeby zobaczyc jak dokladnie ja uzyc i kiedy co gwarantuje.


Nagłówek vmlinux.h zalozeniecbpf programow to compile once run everywhere i w tym naglowku sa 
struktury jakie sa w danej wersji kernela

Lepiej zamiast zrobic wlasna strukture (bo moze nie zadzialac), to warto zrobic po prostu kilka map