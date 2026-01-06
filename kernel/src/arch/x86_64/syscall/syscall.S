.section .text
.global syscall_func
.extern syscall_handler

syscall_func:
    swapgs
    pushq %rcx
    pushq %r11
    
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    
    movq %r10, %rcx
    
    call syscall_handler
    
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    
    popq %r11
    popq %rcx
    swapgs
    sysretq
