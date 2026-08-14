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
    mov ax, 0x18
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
