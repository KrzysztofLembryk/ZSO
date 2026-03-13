
writer.o:     file format elf64-x86-64


Disassembly of section .text:

0000000000000000 <writer>:
   0:	f3 0f 1e fa          	endbr64
   4:	55                   	push   %rbp
   5:	48 89 e5             	mov    %rsp,%rbp
   8:	48 83 ec 10          	sub    $0x10,%rsp
   c:	89 7d fc             	mov    %edi,-0x4(%rbp)
   f:	ba 02 00 00 00       	mov    $0x2,%edx
  14:	48 b8 00 00 00 00 00 	movabs $0x0,%rax
  1b:	00 00 00 
  1e:	48 89 c6             	mov    %rax,%rsi
  21:	bf 01 00 00 00       	mov    $0x1,%edi
  26:	48 b8 00 00 00 00 00 	movabs $0x0,%rax
  2d:	00 00 00 
  30:	ff d0                	call   *%rax
  32:	90                   	nop
  33:	c9                   	leave
  34:	c3                   	ret
