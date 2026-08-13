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
