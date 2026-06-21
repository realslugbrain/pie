global _start
global pie_write
global pie_print_int
global pie_print_float
global pie_print_newline

global pie_int_wide_new
global pie_int_wide_free
global pie_int_wide_add
global pie_int_wide_sub
global pie_int_wide_mul
global pie_int_wide_div
global pie_int_wide_mod
global pie_int_wide_neg
global pie_int_wide_cmp
global pie_int_wide_to_i64
global pie_int_wide_print

global pie_float_wide_new
global pie_float_wide_free
global pie_float_wide_add
global pie_float_wide_sub
global pie_float_wide_mul
global pie_float_wide_div
global pie_float_wide_neg
global pie_float_wide_cmp
global pie_float_wide_to_f64
global pie_float_wide_print

extern pie_main

section .text

_start:
    call pie_main
    mov rdi, rax
    mov rax, 60
    syscall

pie_write:
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, 1
    mov rax, 1
    syscall
    ret

pie_print_newline:
    mov rdi, newline
    mov rsi, 1
    call pie_write
    ret

pie_print_int:
    push rbp
    mov rbp, rsp
    push rbx
    sub rsp, 64

    mov rax, rdi
    xor r8, r8
    lea r9, [rbp - 9]
    xor rcx, rcx

    cmp rax, 0
    jne .check_negative
    dec r9
    mov byte [r9], '0'
    mov rcx, 1
    jmp .emit

.check_negative:
    cmp rax, 0
    jge .digits
    mov r8, 1
    neg rax

.digits:
    xor rdx, rdx
    mov rbx, 10
    div rbx
    add dl, '0'
    dec r9
    mov [r9], dl
    inc rcx
    test rax, rax
    jnz .digits

    cmp r8, 0
    je .emit
    dec r9
    mov byte [r9], '-'
    inc rcx

.emit:
    mov rdi, r9
    mov rsi, rcx
    call pie_write

    add rsp, 64
    pop rbx
    leave
    ret

pie_print_float:
    push rbp
    mov rbp, rsp
    push rbx
    sub rsp, 96

    movq rax, xmm0
    mov rbx, 0x8000000000000000
    test rax, rbx
    jz .float_abs_ready
    mov byte [rbp - 80], '-'
    lea rdi, [rbp - 80]
    mov rsi, 1
    call pie_write
    mov rbx, 0x7fffffffffffffff
    and rax, rbx
    movq xmm0, rax

.float_abs_ready:
    movsd [rbp - 8], xmm0
    cvttsd2si rax, xmm0
    mov [rbp - 16], rax
    mov rdi, rax
    call pie_print_int

    mov byte [rbp - 80], '.'
    lea rdi, [rbp - 80]
    mov rsi, 1
    call pie_write

    movsd xmm0, [rbp - 8]
    mov rax, [rbp - 16]
    cvtsi2sd xmm1, rax
    subsd xmm0, xmm1
    mulsd xmm0, [rel pie_float_scale_6]
    cvttsd2si rax, xmm0

    lea r9, [rbp - 74]
    mov rcx, 6
    mov rbx, 10

.float_frac_digits:
    xor rdx, rdx
    div rbx
    add dl, '0'
    dec r9
    mov [r9], dl
    loop .float_frac_digits

    mov rdi, r9
    mov rsi, 6
    call pie_write

    add rsp, 96
    pop rbx
    leave
    ret

section .rodata
newline: db 10
pie_float_scale_6: dq 1000000.0
