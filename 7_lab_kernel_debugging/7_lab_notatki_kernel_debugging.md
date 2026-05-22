# zadanie 2
- najprostszy błąd w kernelu: jak robimy kmalloc, to SPRAWDZIC CZY PAMIĘĆ ZOSTAŁĄ PRZYDZIELONA,
bo może nie być pamięci, zawsze sprawdzać czy nie ma błędu, plus zawsze ZWALNIAĆ PAMIĘĆ, bo wycieki
w kernellu to masakra


# Debuggowanie
- co zrobić z kernel panic, bo w terminalu np. sie poucina cos
- jak uruchamiamy QUEMU jest flaga ```-serial``` i ona pozwala przekierowac dane o systemie operacyjnym i 
 możemy zrobić ```-serial -file plik``` i wtedy wszystko co widziimy na ekranie quemu to on te logi tam zapisze
 więc jak będziemy mieć panic to będziemy mieć wszystkie z niego logi i będziemy wiedzieć co się dzieje, dostaniemy backtrace

 - często dużo funkcji którą myślimy że będzie w backtrace to ich nie ma, bo kompilator moze sie zorientowac ze zamiast
 wywolywac funkcje, bo to kosztowne, to kompilator wezmie i wklei kod naszej funkcji (zrobi inline) bo bedzie szybciej i taniej,
 wiec jej moze nie byc w backtrace ale mimo to ona sie wywolala

 - nie dodawac tylko jednego printa gdzie nam sie wydaje ze jest blad, bo jak sie scrashuje i nic sie nie wypisze, to nie wiemy
 czy sie nie wypisal bo crash, bo nie doszlismy do tego meijsca, bo zly kernel zbootowalismy, wiec warto dodac printa
 W PIERWSZEJ lini naszej funkcji przed potencjalnym miejscem crasha i w kilku innych miejscach, bo wtedy przynajmniej wiemy ze
 dobry kernel itp. Warto tez wypisac adres pointera i sprawdzic czy to nie NULL

 - nie dodawac print loga w vfs_write bo wtedy potezny nalot na wydajnosc, bo vfs_write w tysiacach na sekunde idzie
 i kompilacja też będzie znacznie dluzsza

 - overflow na int to undefined behaviour, ale na unsigned juz nie, wyzeruje sie nam wtedy calosc i od poczatku zacznie zliczac

 - jak mamy memory corruption to duża szansa że nam się zawiesi kernel i dostaniemy panic z miejsca/funkcji jakiejś w ogóle
 co się nie spodziewamy, bo cały czas leakujemy memory i finalnie w jakimś randomowym miejscu może nam się wywalić w randomowej funkcji.
 Podobnie z double free

 - katalog fd, dzieki temu mozna wylistowac wszystkie file descriptory danego programu

 - filesystem debugfs

 - sys/kernel/debug/kprobes do debuggowania, duzo rzeczy ktore mozna zobaczyc w ebpf mozna tez zobaczyc w debugfs w sys/kernel/debug/tracing

