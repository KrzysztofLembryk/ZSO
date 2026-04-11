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

- gdzie jest struct_file: ```fs.h```
- gdzie jest init i free dla file: 
    - plik: ```file_table.c``` 
    - funkcje: ```static inline void file_free()``` i ```static int init_file(struct file *f, int flags, const struct cred *cred) ```
- llseek: definicja w ```fs.h```, impl: ```read_write.c```

- llseek implementacja: ```loff_t generic_file_llseek(struct file *file, loff_t offset, int whence)```
    - ona używa: ```generic_file_llseek_size```
    - który używa: ```	ret = must_set_pos(file, &offset, whence, eof);```
    - w którym robimy switch case na flagach: SEEK_END, SEEK_CUR, ..., SEEK_BOOKMARK 

# Jak uzywac list w kernelu (np. do zaimplementowania stacka)

- **WAŻNY** przykłady z docsów: https://docs.kernel.org/core-api/list.html#traversing-the-list 
- ok przykład z linkedin: https://www.linkedin.com/pulse/comprehensive-guide-struct-listhead-listforeachentry-linux-david-zhu-2gcrc


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