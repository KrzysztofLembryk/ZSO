# Q&A
## Clang acting up
- If clangd doesnt see kernel modules/funcs/etc., we need to create compile_commands.json
in our project (```tapedev/``` dir)

```bash
# First, clean the previous build so `bear` can capture the full compilation process
make clean

# Then, run make wrapped with bear
bear -- make
```

- in root of project create ```.vscode/settings.json```, inside ```settings.json```
```json
{
  "clangd.arguments": [
    "--compile-commands-dir=./tape_dev"
  ]
}
```

- remember to have kernel source cloned and compiled 

- probably you also must have ```.clangd``` file:
```bash
CompileFlags:
  Add:
    - -D__KERNEL__
    - -DMODULE
    - -nostdinc
    - -include
    - /root/zso_linux_kernel/include/linux/compiler-version.h
    - -include
    - /root/zso_linux_kernel/include/linux/kconfig.h
    - -I/root/zso_linux_kernel/include
    - -I/root/zso_linux_kernel/arch/x86/include
    - -I/root/zso_linux_kernel/arch/x86/include/generated
    - -I/root/zso_linux_kernel/include/uapi
    - -I/root/zso_linux_kernel/arch/x86/include/uapi
    - -I/root/zso_linux_kernel/include/generated/uapi
    - -I/root/zso_linux_kernel/arch/x86/include/generated/uapi
    - -I/root/zso_linux_kernel/..
```

- then clangd: Restart language server by ```Ctrl+Shift+P```