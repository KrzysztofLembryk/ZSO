# kompilacja jadra
- pamietac zeby pobrac DOBRĄ WERSJĘ


- **Unreliable guide to hacking the linux kernel** - wstęp jak wygląda praca w jądrze
- najlepsze: ```make menuconfig```

- proces kompilacji wielowoatkowo, bo inaczej zajmie to kilkadziesiat minut ```make -j15```

- jak zmienimy cos w kernelu i udala sie kompilacja, zmieniamy ale nie widac tego w grubie, bo moze nam sie
nie zbootowac ta wersja ktora zmieniliśmy

- secure boot - każdy kolejny krok bootowania jest z podpisem, komputer ma zpaisane klucze którym wierzy i w każdym kroku
komputer sprawdza czy wszystko jest dobrze podpisane


# małe zadanie hinty
-  poszukac ```initial_code```, arch/x86/kernel
- dodac printk gdzie przejmuje część niezależna od architektruy jądra
- potem odapala pierwszy proces userspace
- drugie zadanie 200+ cykli kompilacji
- ```make LLVM=1 CC=clang compile_commands.json -j7```
- kompilacaj z clang: https://www.kernel.org/doc/html/v5.8/kbuild/llvm.html
- dodanie do workspace patha zeby lepsze podpowiedzi: https://stackoverflow.com/questions/49198816/how-to-use-the-visual-studio-code-to-navigate-linux-kernel-source