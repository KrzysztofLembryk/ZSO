# sterowniki czym są
- ```/sys/class/leds/input``` - są sterowniki które kontrolują kontrolki ledów
- ```/dev/``` - 

- minor major - zmieniają się trzeba uważać podczas pisania sterowników
- w strukturz file_operations mówimy które funkcje mają być wykonane przy danych syscallach

- cat /prov/devices
- warto przejrze /drivers/char/memc.c
# małe zadanie

# duże 3 zdanie
- przyklad np drivers/block/ps3disk.c, np. funkcja init, blk_mq_ops przyjmuje strukture z funkcjami i nas najbardziej interesuje blk_status_t queue

- żądanie to jeden ciągły wektor bloków w maszynie, ale bufory w pamięci do których zapisujemy nie muszą być ciągłe, więc mamy użyteczne makro rq_for_eacch_segment

- przy inicjalizacji maszyny blokowej struktura queue_limits i można sobie ją optymalnie wypełmić żeby działały z testami

- zero-copy - wszystkie veci prosto do urzadzenia i zeby pisalo prosto do DMA



