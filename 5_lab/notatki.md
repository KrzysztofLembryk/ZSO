# kmalloc
-  mozna nim ograniczoną ilość pamięci alokować
- jeśli chcemy alokować większe struktuty to trzeba skorzystać wtedy z czego innego

- !!! robic goto to error handling !!!

# syscall
- jak z niego przekazac info o bledzie, zwracamy syscalla inta, syscall zawsze musi zwracac inta,
zwracamy ZAWSZE MINUS WARTOĆŚÐ BŁĘDU


- używać spinlocka spin_lock_irqsave - bezpieczny,  bo blokuje przerwania

# zadanie male labowe

- znalezc odpowiedniego structa i dodac nowe pole i dodac obsluge go w syscallu

# WAŻNE komendy do bootwania linuxa

```bash
# zeby zdobyc nazwe mojej wersji jadra
grep "menuentry" /boot/grub/grub.cfg | grep -i custom

# zeby zbootowac sie na moja customowa wersje jadra w nastpenym bootcie
sudo grub-reboot "Advanced options for Debian GNU/Linux>Debian GNU/Linux, with Linux 6.18.5-myCustom-g1d72923c9b50-dirty"

# zeby sprawdzic jaki kernel zapisany jako default i jaki nastepny bedzie bootowany
sudo grub-editenv list

# zeby zrebootowac maszyne
sudo reboot
```