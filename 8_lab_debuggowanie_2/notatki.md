# buggy mod 1
- dereferencja nulla, ale potem kernel nadal dziala i sobie z tym poradzil (
    mielismy zatrzymanie i brak responsywanośći, bo kdb był włączony i to zatrzymał
)

# buggy mod 2

- dereferencja nulla ale najpierw bierzemy spinlocka, znowu dostajemy blad dereferencji NULLA, ale
i nawet ze wzielismy spinlocka to proces ktory to zrobil zostal zabity i nadal wszystko dziala.

- ALE jakbysmy chcieli odładować buggy_mod2 to się nie da, bo używa spinlocka nadal, którego nie da się 
już zwolnić. Najłatwiej to odładować to robiąc reboot, ALE przez to że mamy ten spinlock zepsuty, to maszyna się
nam nie zrebootuje

# buggy mod 3 - uważać w zadaniach zaliczeniowych na to
- brak sprawdzenia czy kmalloc się udał
- co zrobić żeby alokacja się nie udała, najłatwiej zrobić failslab i task-filter i wtedy tylko moduły/taski 
które mają odpowiednią flagę failują