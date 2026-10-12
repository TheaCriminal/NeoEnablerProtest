// Generic NEO (PS4 Pro) launch hook for SceShellCore.
// Replaces the call to getAppInfo that ps-patch-system already redirected to its own hook. The shim calls
// that hook first (prev), so ps-patch-system keeps working, then for PS4 title IDs (prefix table below)
// whose ATTRIBUTE is exactly 2 sets bit 0x00800000 (kPS4Pro) and logs "[NEO <title>] old>new" with printf.
// Position independent: the manager copies it to a cave and fills the data fields below.
// Arguments: rdi = title ID string, pInfo = rdx (fw < 9.00) or rcx (fw 9.00+, four_args = 1).
.intel_syntax noprefix
.section .shim,"ax",@progbits   // custom section: alignment 1 keeps the data offsets exact
.global _start
_start:
 push rbx
 push r12
 push r13                      // three pushes: stack is 16-byte aligned for the calls below
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
 mov edx, dword ptr [r12]
 cmp edx, 2
 jne done
 mov ecx, edx
 or ecx, 0x00800000
 mov dword ptr [r12], ecx
 mov r10, qword ptr [rip+printf_ptr]
 test r10, r10
 jz done
 lea rdi, [rip+fmt]
 mov rsi, rbx
 xor eax, eax
 call r10
done:
 mov rax, r13
 pop r13
 pop r12
 pop rbx
 ret
.global prev, printf_ptr, attr_off, four_args, magic
prev:
 .quad 0
printf_ptr:
 .quad 0
attr_off:
 .long 0
four_args:
 .byte 0
magic:
 .ascii "NEOENBL2"
prefixes:
 .ascii "CUSA", "PLAS", "PCAS", "PCJS", "PLJS", "PLJM", "PCKS"
fmt:
 .asciz "[NEO %s] %x>%x\n"
