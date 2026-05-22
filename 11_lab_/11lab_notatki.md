lspci - wylistowac urzadzenia pci


- w adlerdev_init mamy wywołąnie pci_register_driver 

 w strukturze ID table umieszcamy wszystkie pasujace ID itp


 ważna rzecz ustawienie w strukturze pci device - ustawienie wskaznika do informacji o naszym urzadzneiu, jego stanie itp

 zeby zaalokowac jakas pamiec trzeba uzyc dma_alloc_coherent a nie kmalloc

