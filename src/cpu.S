.intel_syntax noprefix
.text

.globl cpu_vendor
cpu_vendor:
    # Microsoft x64 ABI: RCX = output pointer
    # RBX is nonvolatile, so preserve it across CPUID.
    push rbx
    xor eax, eax
    cpuid
    mov dword ptr [rcx + 0], ebx
    mov dword ptr [rcx + 4], edx
    mov dword ptr [rcx + 8], ecx
    mov byte ptr [rcx + 12], 0
    pop rbx
    ret

.globl cpu_read_tsc
cpu_read_tsc:
    rdtsc
    shl rdx, 32
    or rax, rdx
    ret

.globl cpu_cpuid1
cpu_cpuid1:
    # RCX = uint32_t *ecx_out, RDX = uint32_t *edx_out
    push rbx
    mov r8, rdx
    mov r9, rcx
    mov eax, 1
    cpuid
    mov dword ptr [r9], ecx
    mov dword ptr [r8], edx
    pop rbx
    ret

.globl cpu_out8
cpu_out8:
    # Microsoft x64 ABI: RCX = port, RDX = value
    mov eax, edx
    mov dx, cx
    out dx, al
    ret

.globl cpu_in8
cpu_in8:
    # Microsoft x64 ABI: RCX = port
    mov dx, cx
    in al, dx
    movzx eax, al
    ret

.globl cpu_out32
cpu_out32:
    # Microsoft x64 ABI: RCX = port, RDX = value
    mov eax, edx
    mov dx, cx
    out dx, eax
    ret

.globl cpu_out16
cpu_out16:
    # Microsoft x64 ABI: RCX = port, RDX = value
    mov eax, edx
    mov dx, cx
    out dx, ax
    ret

.globl cpu_in32
cpu_in32:
    # Microsoft x64 ABI: RCX = port
    mov dx, cx
    in eax, dx
    ret

.globl cpu_switch_stack_and_call
cpu_switch_stack_and_call:
    # RCX = stack top, RDX = entry, R8 = boot information
    mov rsp, rcx
    and rsp, -16
    sub rsp, 32
    mov rcx, r8
    call rdx

.globl cpu_halt_forever
cpu_halt_forever:
    cli
.Lhalt:
    hlt
    jmp .Lhalt

.globl cpu_pause
cpu_pause:
    pause
    ret

.globl cpu_wait_for_interrupt
cpu_wait_for_interrupt:
    hlt
    ret

.globl cpu_trigger_breakpoint
cpu_trigger_breakpoint:
    int3
    ret

.globl cpu_read_cr2
cpu_read_cr2:
    mov rax, cr2
    ret

.globl cpu_load_cr3
cpu_load_cr3:
    mov cr3, rcx
    ret

.globl cpu_enable_write_protect
cpu_enable_write_protect:
    mov rax, cr0
    bts rax, 16
    mov cr0, rax
    ret

.globl cpu_has_nx
cpu_has_nx:
    push rbx
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .Lno_nx
    mov eax, 0x80000001
    cpuid
    bt edx, 20
    setc al
    movzx eax, al
    pop rbx
    ret
.Lno_nx:
    xor eax, eax
    pop rbx
    ret

.globl cpu_enable_nx
cpu_enable_nx:
    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x800
    wrmsr
    ret

.globl cpu_rdmsr
cpu_rdmsr:
    rdmsr
    shl rdx, 32
    or rax, rdx
    ret

.globl cpu_wrmsr
cpu_wrmsr:
    mov r8, rdx
    mov eax, r8d
    shr r8, 32
    mov edx, r8d
    wrmsr
    ret

.globl arch_load_gdt
arch_load_gdt:
    lgdt [rcx]
    push 0x08
    lea rax, [rip + .Lgdt_loaded]
    push rax
    retfq
.Lgdt_loaded:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor eax, eax
    mov fs, ax
    mov gs, ax
    ret

.globl arch_load_idt
arch_load_idt:
    lidt [rcx]
    ret

.globl arch_load_task_register
arch_load_task_register:
    mov ax, 0x28
    ltr ax
    ret

.globl arch_read_task_register
arch_read_task_register:
    str ax
    movzx eax, ax
    ret

.globl arch_enable_interrupts
arch_enable_interrupts:
    sti
    ret

.globl arch_resume_user
arch_resume_user:
    # RCX = arch_user_context_t pointer. Preserve the Microsoft x64 nonvolatile
    # state so a yield or exit can return to the C scheduler like a normal call.
    pushfq
    push rbx
    push rbp
    push rdi
    push rsi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 160
    movdqu xmmword ptr [rsp + 0], xmm6
    movdqu xmmword ptr [rsp + 16], xmm7
    movdqu xmmword ptr [rsp + 32], xmm8
    movdqu xmmword ptr [rsp + 48], xmm9
    movdqu xmmword ptr [rsp + 64], xmm10
    movdqu xmmword ptr [rsp + 80], xmm11
    movdqu xmmword ptr [rsp + 96], xmm12
    movdqu xmmword ptr [rsp + 112], xmm13
    movdqu xmmword ptr [rsp + 128], xmm14
    movdqu xmmword ptr [rsp + 144], xmm15
    mov qword ptr [rip + arch_user_return_rsp], rsp

    mov r10, rcx
    xor eax, eax
    mov ds, ax
    mov es, ax
    push 0x1b
    push qword ptr [r10 + 136]
    push qword ptr [r10 + 128]
    push 0x23
    push qword ptr [r10 + 120]
    mov r15, qword ptr [r10 + 0]
    mov r14, qword ptr [r10 + 8]
    mov r13, qword ptr [r10 + 16]
    mov r12, qword ptr [r10 + 24]
    mov r9, qword ptr [r10 + 48]
    mov r8, qword ptr [r10 + 56]
    mov rdi, qword ptr [r10 + 64]
    mov rsi, qword ptr [r10 + 72]
    mov rbp, qword ptr [r10 + 80]
    mov rdx, qword ptr [r10 + 88]
    mov rbx, qword ptr [r10 + 104]
    mov rax, qword ptr [r10 + 112]
    mov rcx, qword ptr [r10 + 96]
    mov r11, qword ptr [r10 + 32]
    mov r10, qword ptr [r10 + 40]
    iretq

.globl syscall_entry
syscall_entry:
    # SYSCALL leaves the user return RIP in RCX, flags in R11, and does not
    # switch stacks. Capture a single-CPU user context before touching either.
    mov qword ptr [rip + syscall_context + 0], r15
    mov qword ptr [rip + syscall_context + 8], r14
    mov qword ptr [rip + syscall_context + 16], r13
    mov qword ptr [rip + syscall_context + 24], r12
    mov qword ptr [rip + syscall_context + 32], r11
    mov qword ptr [rip + syscall_context + 40], r10
    mov qword ptr [rip + syscall_context + 48], r9
    mov qword ptr [rip + syscall_context + 56], r8
    mov qword ptr [rip + syscall_context + 64], rdi
    mov qword ptr [rip + syscall_context + 72], rsi
    mov qword ptr [rip + syscall_context + 80], rbp
    mov qword ptr [rip + syscall_context + 88], rdx
    mov qword ptr [rip + syscall_context + 96], rcx
    mov qword ptr [rip + syscall_context + 104], rbx
    mov qword ptr [rip + syscall_context + 112], rax
    mov qword ptr [rip + syscall_context + 120], rcx
    mov qword ptr [rip + syscall_context + 128], r11
    mov qword ptr [rip + syscall_context + 136], rsp

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov rsp, qword ptr [rip + syscall_kernel_rsp]
    and rsp, -16
    sub rsp, 32
    lea rcx, [rip + syscall_context]
    call syscall_dispatch
    mov qword ptr [rip + arch_user_return_action], rax
    test rax, rax
    jne .Luser_return_to_kernel

    xor eax, eax
    mov ds, ax
    mov es, ax
    lea r11, [rip + syscall_context]
    mov r15, qword ptr [r11 + 0]
    mov r14, qword ptr [r11 + 8]
    mov r13, qword ptr [r11 + 16]
    mov r12, qword ptr [r11 + 24]
    mov r10, qword ptr [r11 + 40]
    mov r9, qword ptr [r11 + 48]
    mov r8, qword ptr [r11 + 56]
    mov rdi, qword ptr [r11 + 64]
    mov rsi, qword ptr [r11 + 72]
    mov rbp, qword ptr [r11 + 80]
    mov rdx, qword ptr [r11 + 88]
    mov rbx, qword ptr [r11 + 104]
    mov rcx, qword ptr [r11 + 120]
    mov rsp, qword ptr [r11 + 136]
    mov rax, qword ptr [r11 + 112]
    mov r11, qword ptr [r11 + 128]
    sysretq

.Luser_return_to_kernel:
    mov rsp, qword ptr [rip + arch_user_return_rsp]
    movdqu xmm6, xmmword ptr [rsp + 0]
    movdqu xmm7, xmmword ptr [rsp + 16]
    movdqu xmm8, xmmword ptr [rsp + 32]
    movdqu xmm9, xmmword ptr [rsp + 48]
    movdqu xmm10, xmmword ptr [rsp + 64]
    movdqu xmm11, xmmword ptr [rsp + 80]
    movdqu xmm12, xmmword ptr [rsp + 96]
    movdqu xmm13, xmmword ptr [rsp + 112]
    movdqu xmm14, xmmword ptr [rsp + 128]
    movdqu xmm15, xmmword ptr [rsp + 144]
    add rsp, 160
    pop r15
    pop r14
    pop r13
    pop r12
    pop rsi
    pop rdi
    pop rbp
    pop rbx
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    popfq
    mov rax, qword ptr [rip + arch_user_return_action]
    ret

.p2align 4
.globl user_probe_start
user_probe_start:
    mov r12, rdi
    mov qword ptr [rsp - 8], rdi
    mov eax, 2
    syscall
    cmp rax, r12
    jne .Luser_probe_fail

    mov eax, 1
    lea rdi, [rip + .Luser_started]
    mov esi, 21
    syscall

    mov eax, 3
    syscall
    cmp qword ptr [rsp - 8], r12
    jne .Luser_probe_fail

    mov eax, 1
    lea rdi, [rip + .Luser_resumed]
    mov esi, 21
    syscall

    mov eax, 4
    xor edi, edi
    syscall

.Luser_probe_fail:
    mov eax, 4
    mov edi, 1
    syscall
    ud2

.Luser_started:
    .ascii "USER_PROCESS_STARTED\n"
.Luser_resumed:
    .ascii "USER_PROCESS_RESUMED\n"
.globl user_probe_end
user_probe_end:

.p2align 4
.globl user_fault_probe_start
user_fault_probe_start:
    ud2
.globl user_fault_probe_end
user_fault_probe_end:

.p2align 4
.globl user_spin_probe_start
user_spin_probe_start:
    pause
    jmp user_spin_probe_start
.globl user_spin_probe_end
user_spin_probe_end:

.bss
.p2align 4
.globl syscall_kernel_rsp
syscall_kernel_rsp:
    .quad 0
arch_user_return_rsp:
    .quad 0
arch_user_return_action:
    .quad 0
syscall_context:
    .zero 144

.text

.macro ISR_NO_ERROR vector
.globl isr_\vector
isr_\vector:
    push 0
    push \vector
    jmp isr_common
.endm

.macro ISR_ERROR vector
.globl isr_\vector
isr_\vector:
    push \vector
    jmp isr_common
.endm

ISR_NO_ERROR 0
ISR_NO_ERROR 1
ISR_NO_ERROR 2
ISR_NO_ERROR 3
ISR_NO_ERROR 4
ISR_NO_ERROR 5
ISR_NO_ERROR 6
ISR_NO_ERROR 7
ISR_ERROR 8
ISR_NO_ERROR 9
ISR_ERROR 10
ISR_ERROR 11
ISR_ERROR 12
ISR_ERROR 13
ISR_ERROR 14
ISR_NO_ERROR 15
ISR_NO_ERROR 16
ISR_ERROR 17
ISR_NO_ERROR 18
ISR_NO_ERROR 19
ISR_NO_ERROR 20
ISR_ERROR 21
ISR_NO_ERROR 22
ISR_NO_ERROR 23
ISR_NO_ERROR 24
ISR_NO_ERROR 25
ISR_NO_ERROR 26
ISR_NO_ERROR 27
ISR_NO_ERROR 28
ISR_ERROR 29
ISR_ERROR 30
ISR_NO_ERROR 31
ISR_NO_ERROR 64
ISR_NO_ERROR 65
ISR_NO_ERROR 66
ISR_NO_ERROR 255

isr_common:
    cld
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov r15, rsp
    mov rcx, rsp
    and rsp, -16
    sub rsp, 32
    call interrupt_dispatch
    mov rsp, r15

    test rax, rax
    je .Linterrupt_iret
    mov qword ptr [rip + arch_user_return_action], rax
    jmp .Luser_return_to_kernel

.Linterrupt_iret:
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    iretq

.data
.p2align 3
.globl isr_stub_table
isr_stub_table:
    .quad isr_0, isr_1, isr_2, isr_3, isr_4, isr_5, isr_6, isr_7
    .quad isr_8, isr_9, isr_10, isr_11, isr_12, isr_13, isr_14, isr_15
    .quad isr_16, isr_17, isr_18, isr_19, isr_20, isr_21, isr_22, isr_23
    .quad isr_24, isr_25, isr_26, isr_27, isr_28, isr_29, isr_30, isr_31
    .quad isr_64, isr_65, isr_66, isr_255
