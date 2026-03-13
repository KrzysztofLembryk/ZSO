# SOLVE zadania i trampolinie
- **trampolina**: mała funkcja, która zapamiętuje odpowiednie wartości zmiennych/rejestrów (może dodaje 
jakieś swoje), a następnie przekazuje kontrolę wykonania do innego miejsca.
- np. jeśli chcemy wywołać w Pythonie funkcję zaimplementowaną w C, która bierze jako argument pointer 
do funkcji, wtedy: 'In C, a callback is just a raw function pointer. But in Python, functions are dynamic objects — you can't just take "a pointer to a function" when the function doesn't exist until runtime. Thus we generate a small piece of machine code — the trampoline — that acts as the stable C-callable address, but immediately forwards execution: 
- trampoline will remember the appropriate registers containing parameters, write the trampoline identifier (to select the appropriate high-level function to invocation), and then call the FFI library function that starts the dynamic language interpreter with the appropriate parameters.'

- chcemy zaimplementować:
    ```c
    // zwracamy wskaźnik void*, który ma wskazywać na poprzedni handler tego sygnału
    // generujemy i ustawiamy nowy handler, który wypisze wartość signum używając write(1,...)
    // i zignoruje wartość sygnału, który dostał
    typedef void (*sighandler_t)(int);
    sighandler_t make_signal_handler(int signum);
    ```
- przykładowo, poniższe wywołania mają dać ten sam wynik:

    ```c
    int main() {
        sighandler_t old_2 = make_signal_handler(2);
        sighandler_t old_12 = make_signal_handler(12);
        for (;;) pause();
    }
    ```

- co te:
    ```c
    void write2(int signum) {
        write(1, "2", 1);
    }
    void write12(int signum) {
        write(1, "12", 2);
    }

    int main() {
        sighandler_t old_2 = signal(2, write2);
        sighandler_t old_12 = signal(12, write12);
        for (;;) pause();
    }
    ```



- kompilujemy za pomocą ```gcc -no-pie -mcmodel=large -fno-pie -c ``` plik:

    ```c
    void writer(int signum) {
        write(1, "12", 2);
    }
    ```

- deasemblujemy plik .o ```objdump -d .o > plik.asm```


# Opcodes dla x86_64: https://shell-storm.org/x86doc/ - tylko tutaj mov dst, src
- używamy AT&T syntax, gdzie mov src, dst
- stanford introduction to asm: https://web.stanford.edu/class/cs107/guide/x86-64.html 
## *mov src, dst* - przenosimy src do dst
- ```MOV imm32, r32``` | ```B8+rd id``` | Move imm32 to r32  (WARTOŚĆ do rejestru)
    - przenosimy 32 bitową wartość do rejestru 32 bitowego
    - ```B8+rd``` - to opcode dla mov, który od razu koduje do którego rejestru przenosimy wartość
    - EAX=0, ECX=1, EDX=2,... czyli ```mov EAX = B8```, ```mov ECX = B9```, ```mov EDX = BA```
    - przykład: ```0xB8,0xFF,0x00,0x00,0x00``` = ```mov     0xFF, eax``` (używamy little endian)
- Tabela kodów rejestrów:
    | Register | Code |
    |----------|------|
    | EAX/RAX  | 0    |
    | ECX/RCX  | 1    |
    | EDX/RDX  | 2    |
    | EBX/RBX  | 3    |
    | ESP/RSP  | 4    |
    | EBP/RBP  | 5    |
    | ESI/RSI  | 6    |
    | EDI/RDI  | 7    |
- ```MOV r32, r/m32``` | ```8B /r``` | ```Move r/m32 to r32.``` 
- ```MOV r/m32, r32``` | ```89 /r``` | ```Move r32 to r/m32.``` 
    - przenosimy 32 bitową wartość danego rejestru do innego rejestru/miejsca w pamięci
    - przykład:
    ```
    0x48 0x89 0xc6      mov %rax,%rsi
    ```
    - **0x48** - REX prefix; zmienia operację z **32-bitowej na 64-bitową**, więc pracujemy z RAX i RSI zamiast EAX i ESI
    - **0x89** - czyli używamy MOV r/m32, r32
    - **0xC6** - ModR/M byte - 0xC6 = 11*00 0***110**
        - 11 - mod = 11 = register (robimy mov do rejestru)
        - 000 - source = EAX/RAX (w tabeli rax = 0)
        - 110 - dest = ESI/RSI (w tabeli rsi = 6 = 110)

## Przypomnienie rejestrów
- Parametry do funkcji przekazywane są po kolei w rejestrach: ```rdi, rsi, rdx, rcx, r8, and r9```.
- Funkcje muszą zachować wartości rejestrów: ```rbx, rsp, rbp, r12, r13, r14, and r15```
- po tych rejestrach można pisać dowolnie: ```rax, rdi, rsi, rdx, rcx, r8, r9, r10, r11```
- Stos zawsze musi być dorównany 16-bajtowo
- Na przykład dlatego w zdumpowanym pliku mamy poniższe linijki:
    ```asm
    55                   	push   %rbp         ; Save address of previous stack frame
    48 89 e5             	mov    %rsp,%rbp    ; Address of current stack frame to rbp
    48 83 ec 10          	sub    $0x10,%rsp   ; Reserve 16 bytes for local variables 
    ```
- ```rbp``` (base pointer) -  is used to mark the start of the current stack frame for a function. It helps access local variables and function parameters reliably.
