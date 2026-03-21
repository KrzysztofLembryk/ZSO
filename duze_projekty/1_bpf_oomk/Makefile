# Makefile for building BPF applications

# Project configuration
APP := oomk
BPF_OBJ := $(APP).bpf.o
SKEL_OBJ := $(APP).skel.h
USER_SRC := $(APP).user.c

# Compiler and tools
CLANG ?= clang
BPFTOOL ?= bpftool
CC ?= gcc

# Compiler flags
CFLAGS := -g -O2 -Wall
# BPF flags: enable BPFv3 for atomic operations support
BPF_CFLAGS := -g -O2 -target bpf -D__TARGET_ARCH_x86 -mcpu=v3

# Libraries
LIBS := -lbpf -lelf -lz

.PHONY: all clean

all: $(APP)

# 1. Compile BPF program
#    Dependencies: source file and vmlinux.h
$(BPF_OBJ): $(APP).bpf.c vmlinux.h
	$(CLANG) $(BPF_CFLAGS) -c $< -o $@

# 2. Generate BPF skeleton
#    Dependencies: BPF object file
$(SKEL_OBJ): $(BPF_OBJ)
	$(BPFTOOL) gen skeleton $< name $(APP) > $@

# 3. Compile user-space program
#    Dependencies: user source and generated skeleton header
#    Note: Although we compile .c, the skeleton header is included inside it, 
#    so it must exist before compilation starts.
$(APP): $(USER_SRC) $(SKEL_OBJ)
	$(CC) $(CFLAGS) $< -o $@ $(LIBS)

clean:
	rm -f $(APP) $(BPF_OBJ) $(SKEL_OBJ)
