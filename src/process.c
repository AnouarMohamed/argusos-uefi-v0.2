#include "process.h"
#include "pmm.h"
#include "serial.h"
#include "user_abi.h"

#define PROCESS_PROBE_COUNT 2u
#define USER_CODE_OFFSET 0x1000ULL
#define USER_STACK_OFFSET 0x200000ULL
#define USER_SYSCALL_STACK_BYTES 32768u
#define USER_WRITE_LIMIT 128u
#define X86_PAGE_WRITABLE (1ULL << 1)
#define X86_PAGE_NO_EXECUTE (1ULL << 63)

typedef struct {
    uint64_t pid;
    argus_process_state_t state;
    paging_user_space_t address_space;
    uint64_t code_physical;
    uint64_t stack_physical;
    uint64_t code_virtual;
    uint64_t stack_virtual;
    arch_user_context_t context;
    uint64_t exit_status;
    uint64_t yields;
} process_t;

extern uint8_t user_probe_start[];
extern uint8_t user_probe_end[];

static process_t processes[ARGUS_PROCESS_MAX];
static const paging_info_t *kernel_paging;
static int current_process = -1;
static uint32_t configured_processes;
static uint32_t completed_processes;
static uint64_t syscall_count;
static uint64_t yield_count;
static uint64_t context_switch_count;
static uint64_t write_count;
static int address_spaces_isolated;
static int scheduler_ready;
static uint8_t syscall_stack[USER_SYSCALL_STACK_BYTES]
    __attribute__((aligned(16)));

static void zero_page(uint64_t physical) {
    uint8_t *bytes = (uint8_t *)(uintptr_t)physical;
    for (uint64_t index = 0; index < ARGUS_PAGE_SIZE; ++index) bytes[index] = 0;
}

static void release_process(process_t *process) {
    if (!process) return;
    paging_user_space_destroy(&process->address_space);
    if (process->code_physical) (void)pmm_free_page(process->code_physical);
    if (process->stack_physical) (void)pmm_free_page(process->stack_physical);
    *process = (process_t){0};
}

static int load_probe_process(process_t *process, uint64_t pid) {
    uint64_t image_size = (uint64_t)(user_probe_end - user_probe_start);
    if (!process || !kernel_paging || !image_size || image_size > ARGUS_PAGE_SIZE)
        return 0;
    *process = (process_t){0};
    process->pid = pid;
    if (!paging_user_space_create(kernel_paging, &process->address_space))
        return 0;

    process->code_physical = pmm_alloc_page();
    process->stack_physical = pmm_alloc_page();
    if (!process->code_physical || !process->stack_physical) {
        release_process(process);
        return 0;
    }
    zero_page(process->code_physical);
    zero_page(process->stack_physical);
    uint8_t *destination = (uint8_t *)(uintptr_t)process->code_physical;
    for (uint64_t index = 0; index < image_size; ++index)
        destination[index] = user_probe_start[index];

    process->code_virtual = process->address_space.user_base + USER_CODE_OFFSET;
    process->stack_virtual = process->address_space.user_base + USER_STACK_OFFSET;
    if (!paging_user_map_page(
            &process->address_space,
            process->code_virtual,
            process->code_physical,
            0,
            1) ||
        !paging_user_map_page(
            &process->address_space,
            process->stack_virtual,
            process->stack_physical,
            1,
            0)) {
        release_process(process);
        return 0;
    }

    process->context.rip = process->code_virtual;
    process->context.rsp = process->stack_virtual + ARGUS_PAGE_SIZE - 16u;
    process->context.rflags = 0x202u;
    process->context.rdi = pid;
    process->state = ARGUS_PROCESS_READY;
    return 1;
}

static int validate_address_space_isolation(void) {
    if (configured_processes != PROCESS_PROBE_COUNT) return 0;
    process_t *first = &processes[0];
    process_t *second = &processes[1];
    uint64_t first_code = 0;
    uint64_t second_code = 0;
    uint64_t first_stack = 0;
    uint64_t second_stack = 0;
    uint64_t flags = 0;

    int valid = first->address_space.root_table !=
                    second->address_space.root_table &&
                first->code_virtual == second->code_virtual &&
                first->stack_virtual == second->stack_virtual &&
                paging_user_translate(
                    &first->address_space,
                    first->code_virtual,
                    &first_code,
                    &flags) &&
                !(flags & X86_PAGE_WRITABLE) &&
                (!first->address_space.nx_enabled ||
                    !(flags & X86_PAGE_NO_EXECUTE)) &&
                paging_user_translate(
                    &second->address_space,
                    second->code_virtual,
                    &second_code,
                    0) &&
                paging_user_translate(
                    &first->address_space,
                    first->stack_virtual,
                    &first_stack,
                    &flags) &&
                (flags & X86_PAGE_WRITABLE) &&
                (!first->address_space.nx_enabled ||
                    (flags & X86_PAGE_NO_EXECUTE)) &&
                paging_user_translate(
                    &second->address_space,
                    second->stack_virtual,
                    &second_stack,
                    0) &&
                first_code != second_code && first_stack != second_stack &&
                !paging_user_translate(
                    &first->address_space,
                    first->stack_virtual - ARGUS_PAGE_SIZE,
                    0,
                    0) &&
                !paging_user_translate(
                    &first->address_space,
                    first->code_physical,
                    0,
                    0);
    return valid;
}

int process_init(const paging_info_t *kernel_space) {
    if (!kernel_space || !kernel_space->root_table) return 0;
    kernel_paging = kernel_space;
    current_process = -1;
    configured_processes = 0;
    completed_processes = 0;
    syscall_count = 0;
    yield_count = 0;
    context_switch_count = 0;
    write_count = 0;
    address_spaces_isolated = 0;
    scheduler_ready = 0;
    for (uint32_t index = 0; index < ARGUS_PROCESS_MAX; ++index)
        processes[index] = (process_t){0};

    uint64_t syscall_stack_top =
        (uint64_t)(uintptr_t)(syscall_stack + sizeof(syscall_stack));
    if (!arch_syscall_init(syscall_stack_top)) return 0;
    for (uint32_t index = 0; index < PROCESS_PROBE_COUNT; ++index) {
        if (!load_probe_process(&processes[index], index + 1u)) {
            for (uint32_t release = 0; release <= index; ++release)
                release_process(&processes[release]);
            return 0;
        }
        ++configured_processes;
    }
    address_spaces_isolated = validate_address_space_isolation();
    scheduler_ready = address_spaces_isolated;
    return scheduler_ready;
}

static int user_range_readable(
    const process_t *process,
    uint64_t address,
    uint64_t length
) {
    if (!process || !length || address > UINT64_MAX - (length - 1u)) return 0;
    uint64_t last = address + length - 1u;
    uint64_t page = address & ~(ARGUS_PAGE_SIZE - 1u);
    uint64_t last_page = last & ~(ARGUS_PAGE_SIZE - 1u);
    for (;;) {
        if (!paging_user_translate(&process->address_space, page, 0, 0)) return 0;
        if (page == last_page) return 1;
        if (page > UINT64_MAX - ARGUS_PAGE_SIZE) return 0;
        page += ARGUS_PAGE_SIZE;
    }
}

uint64_t syscall_dispatch(arch_user_context_t *context) {
    if (!context || current_process < 0 ||
        (uint32_t)current_process >= configured_processes)
        return ARCH_USER_ACTION_EXIT;
    process_t *process = &processes[current_process];
    ++syscall_count;

    switch (context->rax) {
        case ARGUS_SYSCALL_WRITE: {
            uint64_t address = context->rdi;
            uint64_t length = context->rsi;
            if (!length || length > USER_WRITE_LIMIT ||
                !user_range_readable(process, address, length)) {
                context->rax = UINT64_MAX;
                return ARCH_USER_ACTION_RETURN;
            }
            const char *text = (const char *)(uintptr_t)address;
            for (uint64_t index = 0; index < length; ++index)
                serial_putc(text[index]);
            ++write_count;
            context->rax = length;
            return ARCH_USER_ACTION_RETURN;
        }
        case ARGUS_SYSCALL_GETPID:
            context->rax = process->pid;
            return ARCH_USER_ACTION_RETURN;
        case ARGUS_SYSCALL_YIELD:
            context->rax = 0;
            process->context = *context;
            process->state = ARGUS_PROCESS_READY;
            ++process->yields;
            ++yield_count;
            return ARCH_USER_ACTION_YIELD;
        case ARGUS_SYSCALL_EXIT:
            process->exit_status = context->rdi;
            process->state = ARGUS_PROCESS_EXITED;
            ++completed_processes;
            return ARCH_USER_ACTION_EXIT;
        default:
            context->rax = UINT64_MAX;
            return ARCH_USER_ACTION_RETURN;
    }
}

static int next_ready_process(uint32_t start) {
    for (uint32_t offset = 0; offset < configured_processes; ++offset) {
        uint32_t index = (start + offset) % configured_processes;
        if (processes[index].state == ARGUS_PROCESS_READY) return (int)index;
    }
    return -1;
}

int process_run_self_test(void) {
    if (!scheduler_ready || current_process >= 0) return 0;
    uint32_t next = 0;
    for (uint32_t dispatches = 0; dispatches < 16u; ++dispatches) {
        int selected = next_ready_process(next);
        if (selected < 0) break;
        process_t *process = &processes[selected];
        current_process = selected;
        process->state = ARGUS_PROCESS_RUNNING;
        paging_user_activate(&process->address_space);
        uint64_t action = arch_resume_user(&process->context);
        paging_kernel_activate(kernel_paging);
        current_process = -1;
        ++context_switch_count;

        if ((action == ARCH_USER_ACTION_YIELD &&
             process->state != ARGUS_PROCESS_READY) ||
            (action == ARCH_USER_ACTION_EXIT &&
             process->state != ARGUS_PROCESS_EXITED) ||
            (action != ARCH_USER_ACTION_YIELD &&
             action != ARCH_USER_ACTION_EXIT))
            return 0;
        next = ((uint32_t)selected + 1u) % configured_processes;
    }

    if (completed_processes != configured_processes ||
        syscall_count != 10u || yield_count != 2u || write_count != 4u ||
        context_switch_count != 4u)
        return 0;
    for (uint32_t index = 0; index < configured_processes; ++index)
        if (processes[index].state != ARGUS_PROCESS_EXITED ||
            processes[index].exit_status != 0 || processes[index].yields != 1u)
            return 0;
    return address_spaces_isolated;
}

uint32_t process_count(void) { return configured_processes; }
uint32_t process_completed_count(void) { return completed_processes; }
uint64_t process_syscall_count(void) { return syscall_count; }
uint64_t process_yield_count(void) { return yield_count; }
uint64_t process_context_switch_count(void) { return context_switch_count; }
int process_address_space_isolated(void) { return address_spaces_isolated; }
int process_scheduler_online(void) { return scheduler_ready; }
