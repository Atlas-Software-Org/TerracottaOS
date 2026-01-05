bits 64
section .text
global pit_interrupt_handler
extern pit_c_handler

pit_interrupt_handler:
    endbr64

    pushfq
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rdi
    push rsi
    push rbx
    push rdx
    push rcx
    push rax

    sub rsp, 8
    mov rdi, rsp
    call pit_c_handler
    mov rsp, rax
    add rsp, 8

    pop rax
    pop rcx
    pop rdx
    pop rbx
    pop rsi
    pop rdi
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    popfq

    iretq
