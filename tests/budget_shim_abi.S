// GPL-3.0-only. Host SysV ABI check around the generated memory-step shim.
.intel_syntax noprefix
.text
.global test_callee_saved
.type test_callee_saved,@function
test_callee_saved:
 push rbx
 push r12
 push r13
 push r14
 push r15
 mov r8, rdi
 mov rdi, rsi
 mov rsi, rdx
 mov rdx, rcx
 mov rbx, 0x111
 mov r12, 0x222
 mov r13, 0x333
 mov r14, 0x444
 mov r15, 0x555
 call r8
 xor eax, eax
 cmp rbx, 0x111
 je 1f
 or eax, 1
1: cmp r12, 0x222
 je 2f
 or eax, 2
2: cmp r13, 0x333
 je 3f
 or eax, 4
3: cmp r14, 0x444
 je 4f
 or eax, 8
4: cmp r15, 0x555
 je 5f
 or eax, 16
5:
 pop r15
 pop r14
 pop r13
 pop r12
 pop rbx
 ret
.section .note.GNU-stack,"",@progbits
