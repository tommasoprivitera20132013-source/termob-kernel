section .text
bits 32

global scheduler_context_switch

; void scheduler_context_switch(scheduler_context_t* from, scheduler_context_t* to);
; struct layout:
;   0  esp
;   4  ebp
;   8  ebx
;   12 esi
;   16 edi
;   20 eflags
;   24 eip

scheduler_context_switch:
    mov eax, [esp + 4]
    mov edx, [esp + 8]

    mov [eax + 0], esp
    mov [eax + 4], ebp
    mov [eax + 8], ebx
    mov [eax + 12], esi
    mov [eax + 16], edi
    pushfd
    pop ecx
    mov [eax + 20], ecx
    mov ecx, .resume
    mov [eax + 24], ecx

    mov esp, [edx + 0]
    mov ebp, [edx + 4]
    mov ebx, [edx + 8]
    mov esi, [edx + 12]
    mov edi, [edx + 16]
    mov ecx, [edx + 20]
    push ecx
    popfd
    jmp [edx + 24]

.resume:
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
