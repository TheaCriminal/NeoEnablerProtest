// Per-game NEO (PS4 Pro) launch hook for SceShellCore.
// Replaces the call to getAppInfo that ps-patch-system already redirected to its own hook. The shim calls
// that hook first (prev), so ps-patch-system keeps working. Then, for PS4 title IDs (prefix table below) whose
// ATTRIBUTE is exactly 2, it asks the manager: it posts the title ID (req_title, req_seq) and waits up to
// wait_cycles TSC cycles for the answer (answer, ack_seq). The manager switches the memory budget the game needs
// before answering; the kernel reads it when it creates the game process, after this call. Answer bit 0 sets
// bit 0x00800000 (kPS4Pro). Without an answer (wait_cycles 0: manager stopped, or timeout) only force_all applies.
// Every Pro launch is logged as "[NEO <title>] old>new a<answer>" with printf.
// Position independent: the manager copies it to a cave and fills the data fields below.
// Arguments: rdi = title ID string, pInfo = rdx (fw < 9.00) or rcx (fw 9.00+, four_args = 1).
.intel_syntax noprefix
.section .shim,"ax",@progbits   // custom section: alignment 1 keeps the data offsets exact
.global _start
_start:
 push rbx
 push r12
 push r13
 push r14
 sub rsp, 8                    // stack 16-byte aligned for the calls below
 mov rbx, rdi
 mov r12, rdx
 cmp byte ptr [rip+four_args], 0
 je 1f
 mov r12, rcx
1:
 call qword ptr [rip+prev]     // argument registers untouched
 mov r13, rax
 test rbx, rbx
 jz done
 test r12, r12
 jz done
 mov eax, dword ptr [rbx]
 lea r10, [rip+prefixes]
 lea r11, [r10+28]             // 7 prefixes
2:
 cmp eax, dword ptr [r10]
 je match
 add r10, 4
 cmp r10, r11
 jb 2b
 jmp done
match:
 mov ecx, dword ptr [rip+attr_off]
 add r12, rcx
 cmp dword ptr [r12], 2
 jne done
 movzx r14d, byte ptr [rip+force_all]   // answer when the manager does not reply
 mov rcx, qword ptr [rip+wait_cycles]
 test rcx, rcx
 jz apply
 mov rax, qword ptr [rbx]
 mov qword ptr [rip+req_title], rax
 mov al, byte ptr [rbx+8]
 mov byte ptr [rip+req_title+8], al
 mov r8d, 1
 lock xadd dword ptr [rip+req_seq], r8d
 inc r8d                       // this request's sequence number
 rdtsc
 shl rdx, 32
 or rax, rdx
 mov r9, rax
3:
 pause
 cmp dword ptr [rip+ack_seq], r8d
 je 4f
 rdtsc
 shl rdx, 32
 or rax, rdx
 sub rax, r9
 cmp rax, qword ptr [rip+wait_cycles]
 jb 3b
 or r14d, 0x80                 // timed out: logged as a8x
 jmp apply
4:
 movzx r14d, byte ptr [rip+answer]
apply:
 test r14d, 1
 jz done
 mov edx, dword ptr [r12]
 mov ecx, edx
 or ecx, 0x00800000
 mov dword ptr [r12], ecx
 mov r10, qword ptr [rip+printf_ptr]
 test r10, r10
 jz done
 lea rdi, [rip+fmt]
 mov rsi, rbx
 mov r8d, r14d
 xor eax, eax
 call r10
done:
 mov rax, r13
 add rsp, 8
 pop r14
 pop r13
 pop r12
 pop rbx
 ret
.balign 8
.global prev, printf_ptr, wait_cycles, attr_off, req_seq, ack_seq, req_title, answer, four_args, force_all, magic
prev:
 .quad 0
printf_ptr:
 .quad 0
wait_cycles:
 .quad 0
attr_off:
 .long 0
req_seq:
 .long 0
ack_seq:
 .long 0
req_title:
 .space 9
answer:
 .byte 0
four_args:
 .byte 0
force_all:
 .byte 0
magic:
 .ascii "NEOENBL3"
prefixes:
 .ascii "CUSA", "PLAS", "PCAS", "PCJS", "PLJS", "PLJM", "PCKS"
fmt:
 .asciz "[NEO %s] %x>%x a%x\n"
