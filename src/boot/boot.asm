section .multiboot
align 8
header_start:
    dd 0xe85250d6
    dd 0
    dd header_end - header_start
    dd -(0xe85250d6 + 0 + (header_end - header_start))

    dw 0
    dw 0
    dd 8
header_end:

section .text
bits 32
global _start
extern kernel_main

_start:
    cli
    mov esp, stack_top
    xor ebp, ebp
    cld
    push ebx
    push eax
    call kernel_main
    add esp, 8

.hang:
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

section .note.GNU-stack noalloc noexec nowrite progbits
