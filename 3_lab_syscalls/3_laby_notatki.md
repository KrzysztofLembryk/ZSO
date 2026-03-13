- Nie uzywal kprobe do rozwiazania zadania z listy
- wszystkie struktury potrzebne do bpf są wylisotwane w pliku
i jego sie generuje (w labach jest, exmpl 4: probing do_nanosleep) i to bedzie potrzebne do rozw zadania:
vm.h generacja

- jakie syscalle są aktualnie wykonywane: ```strace```
- strace moze byc wpiety do PIDu (przydatne do pierewszego zadania), i dzieki temu mozemy znalezc
proces i potem dac ```strace -p wartosc_pid```

```c
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>

int main()
{
    int rc = syscall(SYS_chmod, "./f", 777); // oops
    if (rc == -1)
        return errno;
    return 0;
}
```
- kod błedu ```echo $?```

- ten program tak nie zadzialal ze dal read write i exec, ale czemu?
- zamiast 777, trzeba dac 0777 bo
- ale jak nie wiemy ze trzzeba dac 0777, to mozemy wywolac i zrobic strace na tym programie i zobaczyc ze
syscall zadzialal ale z wartoscia 01411


```c
#include <sys/time.h>
#include <stdio.h>

int main()
{
    struct timeval tv;
    printf("123\n");
    gettimeofday(&tv, NULL);
    printf("%d\n", tv.tv_sec); // wypisuje aktualna liczbe sekund, wydaje sie ze output git
}
```
- ale gdy zrobimy strace ./a.out, powinny byc jakies dwa write'y, ale nie ma gettimeofdate w dobrym miejscu
- ale to co dostalismy, nie ma zadnego syscalla zwiazanego z czasem, mozemy to sprawdzic:
```bash
strace ./a.out 2> err
greup -i time err
```

- ten program nie wywoluje gettimeofday i w dodatku skleił nasze dwa write'y
- wiec trzeba sie wpiać gdb do tego programu

```bash
gdb ./a.out

gdb# layout asm

ldd a.out
```

- plt - to tablica ktora pomoaga linkowac dynamiczne biblioteki
- zatrzymalismy sie na breakpointcie z funckjsą z prefixem vdso
- w linuxie jest mechanizm zeby nie wszystkie syscalle przez kernel byly obslugiwane (glownie takie co obsluguja czas),
a czemu nie chcemy zawsze wchodzic do kernela kiedy pobieramy czas, bo wywolanie syscalla kosztowne (zmiana contextu, cala masa rzeczy
przeladowac z cache'a trzeba itp)
- a aplikacje dosc czesto i uporczywie sprawdzaja zegar (np. apka co duzo loguje i kazda linijka ma dokladna date)

- wiec jesli jakies syscalle sie nie wykonuja w duzym zadaniu to duza szansa ze jest to vdso

# Jak przegladac kod kernela
- kernel.org
- ```wget https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.18.5.tar.xz```
- rozpakowac  i mamy naszego labowego linuxa
- najlepiej przegladac linuxa to po prostu zclonowac sobie wersje z gita w commitcie ktory zrobil wersje 6.18.5
- ```git grep``` najlepszy do repo gitowych
- sciagamy to zrodlo i pliki linuxa i tam robimy sobie git inita i git grep dobrze bardzo dziala
- **bootlin** - hostuje zrodla kernela i hostuje je we wszystkich wersjach, i mamy wszystkie pliki plus wyszukiwarke i dziala dobrze bardzo,
i np. naglowek z linus/syscalls.h to hiperlink i od razu nasz przenosi do niego i jest fajnie xd

- git grep -i 


- np dla syscall_write, wchodzimy i mamy makro SYSCALL_DEFINE3 (3 bo trzy argumenty), potem mamy typ nazwa parametru,
i następnie mamy ```__user```, podkreślenie podkreślenie
- program userspaceowy ma swoją pamięć i kernel chce się do niej dobrać, bo mowimy kernelowi zeby wział nasz bufor i z niego dane
i wyprintował na std
- ale jesli kazdy syscall moglby czytac kazda pamiec w userspacie to byloby fatalnie, bo moglibysmy zrobic nieskonczona rekursje,
jeden program kernelowy odwoluje sie do danej pamieci a drugi do pamieci tej pierwszej itp
- makro user sie uzywa zeby wlasnie dac dostep do userowej pamieci i zeby to bylo bezpieczniejsze
- to nasze makro jest zdefiniowane przy pomoyc SYSCALL_DEFINEx


- **jak mamy testy i nie wiemy czemu nie dzialaja, to warto zrobic strace na tym tescie i sprawdzic syscalle jakie są wykonywane**


- syscall plt to potem musimy przejsc przez tablice do linkowania, potem przez dl,, i dopiero potem dochodzimy tam gdzie chcemy

- rejestry msr (model specific) sluzy do obslugi syscalli

- w linuxiew katalogu arch bedziemy chcieli glownie folder x86, wiec jak mamy jakis plik w kernelu to on ma
wiele wersji (np. write czy cos), to chcemy uzyc tej z x86, zalezne od architektury np jest obslugo MSR rejestrow
