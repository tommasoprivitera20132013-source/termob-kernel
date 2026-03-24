bits 32

global idt_load
global isr0_stub
global isr1_stub
global isr2_stub
global isr3_stub
global isr4_stub
global isr5_stub
global isr6_stub
global isr7_stub
global isr8_stub
global isr9_stub
global isr10_stub
global isr11_stub
global isr12_stub
global isr13_stub
global isr14_stub
global isr15_stub
global isr16_stub
global isr17_stub
global isr18_stub
global isr19_stub
global isr20_stub
global isr21_stub
global isr22_stub
global isr23_stub
global isr24_stub
global isr25_stub
global isr26_stub
global isr27_stub
global isr28_stub
global isr29_stub
global isr30_stub
global isr31_stub
global irq0_stub
global irq1_stub

extern isr_fault_handler_c
extern irq0_handler_c
extern irq1_handler_c

idt_load:
    mov eax, [esp + 4]
    lidt [eax]
    ret

%macro ISR_NOERR 1
isr%1_stub:
    cli
    push dword 0
    push dword %1
    jmp isr_fault_common
%endmacro

%macro ISR_ERR 1
isr%1_stub:
    cli
    push dword %1
    jmp isr_fault_common
%endmacro

isr_fault_common:
    pusha
    push ds
    push es
    push fs
    push gs

    mov eax, esp
    push eax
    call isr_fault_handler_c
    add esp, 4

.halt:
    hlt
    jmp .halt

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31

irq0_stub:
    cli
    pusha
    push ds
    push es
    push fs
    push gs

    call irq0_handler_c

    pop gs
    pop fs
    pop es
    pop ds
    popa
    iretd

irq1_stub:
    cli
    pusha
    push ds
    push es
    push fs
    push gs

    call irq1_handler_c

    pop gs
    pop fs
    pop es
    pop ds
    popa
    iretd

section .note.GNU-stack noalloc noexec nowrite progbits
