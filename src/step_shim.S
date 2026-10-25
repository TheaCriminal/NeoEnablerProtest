// GPL-3.0-only. Serializes the ShellCore BeginAppMount2 GOT import.
// No kernel addresses/syscalls here. After CLAIM, fail closed until controller
// responds; there is deliberately no timed fall-through to an unguarded write.
.intel_syntax noprefix
.section .shim,"ax",@progbits
.global _start
_start:
 push rbx
 push r12
 push r13
 push r14
 push r15
 sub rsp, 32                     // aligned; retain the three original args
 mov [rsp], rdi
 mov [rsp+8], rsi
 mov [rsp+16], rdx
 lock inc dword ptr [rip+active]
 test rdi, rdi                   // only game mounts (category 0xf000) are serialized;
 jz gated                        // system dialogs mount while the game holds the gate
 cmp dword ptr [rdi], 0xf000
 je gated
 call qword ptr [rip+original]   // argument registers untouched
 mov r14, rax
 jmp done
gated:
 rdtsc
 shl rdx, 32
 or rax, rdx
 mov r12, rax
 xor r13d, r13d
acquire:
 xor eax, eax
 mov ecx, 1
 lock cmpxchg dword ptr [rip+gate], ecx
 je acquired
 test r13d, r13d
 jnz contended
 lock inc dword ptr [rip+blocked]
 mov r13d, 1
contended:
 pause
 rdtsc
 shl rdx, 32
 or rax, rdx
 sub rax, r12
 cmp rax, [rip+wait_cycles]
 jb acquire
 lock inc dword ptr [rip+refused]
 mov r14d, 0x80020010             // SCE errno EBUSY; do not call original
 jmp done
acquired:
 mov rdi, [rsp]
 mov rsi, [rsp+8]
 mov rdx, [rsp+16]
 call qword ptr [rip+original]    // direct libkernel wrapper, never GOT stub
 mov r14, rax
 cmp dword ptr [rip+enabled], 0
 je unlock
 mov rbx, [rsp]
 test rbx, rbx
 jz unlock
 mov dword ptr [rip+response_seq], 0
 mov dword ptr [rip+command], 0
 mov eax, [rbx]
 mov [rip+category], eax
 mov [rip+result], r14d
 mov rax, [rbx+4]
 mov [rip+title], rax
 mov ax, [rbx+12]
 mov [rip+title+8], ax
 mov dword ptr [rip+phase], 1     // POST, before publishing sequence
 mov r15d, 1
 lock xadd dword ptr [rip+seq], r15d
 inc r15d
 rdtsc
 shl rdx, 32
 or rax, rdx
 mov r12, rax
post_wait:
 pause
 cmp [rip+response_seq], r15d
 jne post_deadline
 mov eax, [rip+command]
 cmp eax, 1                      // CLAIM -> echo HELD before any kernel write
 je claimed
 cmp eax, 2                      // normal release, no mutation
 je finished
post_deadline:
 rdtsc
 shl rdx, 32
 or rax, rdx
 sub rax, r12
 cmp rax, [rip+wait_cycles]
 jb post_wait
 mov dword ptr [rip+phase], 4     // EXPIRED; late CLAIM cannot authorize a write
 lock inc dword ptr [rip+expired]
 jmp unlock
claimed:
 mov dword ptr [rip+phase], 2     // HELD: no expiry/fall-through after confirmation
held_wait:
 pause
 cmp [rip+response_seq], r15d
 jne held_wait
 mov eax, [rip+command]
 cmp eax, 2
 je finished
 cmp eax, 3                      // release owner but retain gate until rollback
 jne held_wait
 mov dword ptr [rip+phase], 3     // RUNNING, controller now owns gate
 jmp done
finished:
 mov dword ptr [rip+phase], 5
unlock:
 mov dword ptr [rip+gate], 0
done:
 mov rax, r14
 lock dec dword ptr [rip+active]
 add rsp, 32
 pop r15
 pop r14
 pop r13
 pop r12
 pop rbx
 ret
.balign 8
.global original, wait_cycles, enabled, active, gate, seq, response_seq, command
.global phase, category, result, title, blocked, refused, expired, dirty, magic
original: .quad 0
wait_cycles: .quad 100000000
enabled: .long 0
active: .long 0
gate: .long 0
seq: .long 0
response_seq: .long 0
command: .long 0
phase: .long 0
category: .long 0
result: .long 0
title: .space 12
blocked: .long 0
refused: .long 0
expired: .long 0
dirty: .long 0
magic: .ascii "NEOMS001"
