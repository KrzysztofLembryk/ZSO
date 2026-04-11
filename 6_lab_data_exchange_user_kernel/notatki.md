# Czemu niemozemy pisac pamieci do uzytkownika
- funkcje stac() clac() - jawne powiedzenie że wykonujemy operacje na nie naszej pamieci,
    tylko usera

- copy_from_user itp przydatne do drugiego dużego zadania

- wszystkie schedulery działają ze sobą jednocześnie

- qemu desktop - zeby w grubie moc wybrac jadro

- pod koniec w SCHEDULERZE mamhy definicje structan np. enqueue_task_fair, w sched/fair.c
warto uzyc helpera, print_if_matches("Enqueueing\n", p);

- test prog to program co inkrementuje 1 zmienna i robi yielda (mozna znalezc w main simple w testach do 2 zdadania)
- ./test_prog wtedy zalaczony helper wyfiltruje ten proces
- pick_task_fair - wybiera ktore zadanie ma byc uruchomione  (picking)
- pick_next_task_fair - Picked
- dequeue_task_fair -  Dequeue
- migrate_task_rq_fair - migrate task
-   enqueue

- spawnowanie procesow, najpierw fork, potem trafai do kolejki schedulera i dopiero potem
zmieniana jest nazwa


# duze zadanie
- jak mamy calkowicie pierwszy proces systemu to pusta inicjalizacja moze nie byc dobra
- dodawanie syscalli do jadra, dobry początek wziąć istniejący syscall np SYSCALL_DEFINE6 sendto w net/
i prześledzić jak dodać syscall, samo dodanie tej definicji to nie wystarczy, nie trzeba przechodzic przerz wszystkie architektury, **wystarczy zeby dodac dla x86**

- zabezpieczyc sie przed znanymi atakami, np. pamietac zeby operacje kryptogradiczne z odpowiednimi
funkcjami z bibliotekami krypto dobrymi, jądro ma już zaimplementowane odpoweidnie mechanizmy

- pamiętać żeby ODPOWIEDNIO HANDLOWAĆ SYNCHRONIZACJĘ!!!!!!, robić mutexy/spinlocki czy coś,
zobaczyć jak inne operacje to wykonują

- dodać nowe helpery do bpf, przesledzic istniejace helpery i zrobic podobnie
update bid tylko z programu podpieteego jako update_bid, pamięteać zeby tylko z nowych programow mozna je bylo wołać?

- operacaj compare and exchange w auction  process

- zacząć od syscalli