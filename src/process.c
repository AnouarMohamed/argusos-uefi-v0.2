#include "process.h"
#include "apic.h"
#include "app_abi.h"
#include "elf_loader.h"
#include "input_keys.h"
#include "pmm.h"
#include "serial.h"
#include "user_abi.h"

#define PROCESS_PROBE_COUNT 2u
#define APP_PROCESS_COUNT 3u
#define APP_PROCESS_FIRST PROCESS_PROBE_COUNT
#define PROCESS_TEST_SLOT APP_PROCESS_FIRST
#define USER_CODE_OFFSET 0x1000ULL
#define USER_STACK_OFFSET 0x200000ULL
#define USER_SURFACE_OFFSET 0x400000ULL
#define USER_SYSCALL_STACK_BYTES 32768u
#define USER_WRITE_LIMIT 128u
#define APP_INPUT_CAPACITY 32u
#define APP_START_DISPATCH_LIMIT 32u
#define ELF_FLAG_EXECUTE 1u
#define ELF_FLAG_WRITE 2u
#define X86_PAGE_WRITABLE (1ULL << 1)
#define X86_PAGE_NO_EXECUTE (1ULL << 63)

typedef struct {
    uint64_t pid;
    argus_process_state_t state;
    paging_user_space_t address_space;
    uint64_t image_physical[ARGUS_ELF_MAX_IMAGE_PAGES];
    uint32_t image_page_count;
    uint64_t stack_physical;
    uint64_t surface_physical;
    uint32_t surface_page_count;
    uint64_t code_virtual;
    uint64_t stack_virtual;
    uint32_t app_id;
    uint32_t present_sequence;
    int present_valid;
    int active;
    uint8_t input[APP_INPUT_CAPACITY];
    uint8_t input_head;
    uint8_t input_tail;
    uint64_t inputs_delivered;
    uint64_t wait_deadline;
    arch_user_context_t context;
    uint64_t exit_status;
    uint64_t yields;
    uint64_t fault_vector;
    uint64_t fault_error;
    uint64_t fault_rip;
    uint64_t fault_address;
} process_t;

typedef struct {
    uint32_t app_id;
    const uint8_t *image_start;
    const uint8_t *image_end;
} app_catalog_entry_t;

extern uint8_t user_probe_start[];
extern uint8_t user_probe_end[];
extern uint8_t user_fault_probe_start[];
extern uint8_t user_fault_probe_end[];
extern uint8_t user_spin_probe_start[];
extern uint8_t user_spin_probe_end[];
extern uint8_t user_snake_start[];
extern uint8_t user_snake_end[];
extern uint8_t user_calculator_start[];
extern uint8_t user_calculator_end[];
extern uint8_t user_notes_start[];
extern uint8_t user_notes_end[];
extern uint64_t cpu_read_cr2(void);

static process_t processes[ARGUS_PROCESS_MAX];
static const paging_info_t *kernel_paging;
static int current_process = -1;
static uint32_t configured_processes;
static uint32_t completed_processes;
static uint32_t scheduler_cursor;
static uint32_t focused_app_id;
static uint64_t next_pid;
static uint64_t syscall_count;
static uint64_t yield_count;
static uint64_t context_switch_count;
static uint64_t write_count;
static uint64_t fault_count;
static uint64_t preemption_count;
static uint64_t wait_count;
static uint64_t wakeup_count;
static int address_spaces_isolated;
static int scheduler_ready;
static int process_self_test_passed;
static int preemption_enabled;
static uint8_t syscall_stack[USER_SYSCALL_STACK_BYTES]
    __attribute__((aligned(16)));

static const app_catalog_entry_t app_catalog[APP_PROCESS_COUNT] = {
    {ARGUS_APP_ID_SNAKE, user_snake_start, user_snake_end},
    {ARGUS_APP_ID_CALCULATOR, user_calculator_start, user_calculator_end},
    {ARGUS_APP_ID_NOTES, user_notes_start, user_notes_end},
};

_Static_assert((APP_INPUT_CAPACITY & (APP_INPUT_CAPACITY - 1u)) == 0,
               "App input capacity must be a power of two");
_Static_assert(sizeof(argus_app_present_v1_t) == 16u,
               "Application present ABI layout changed");
_Static_assert(ARGUS_APP_SURFACE_BYTES == 71680u,
               "Application surface dimensions changed");
_Static_assert(ARGUS_USER_BASE + USER_CODE_OFFSET == 0x0000008000001000ULL,
               "C++ user link address must match the process image mapping");
_Static_assert(ARGUS_USER_BASE + USER_SURFACE_OFFSET == ARGUS_APP_SURFACE_ADDRESS,
               "App surface address must match its user mapping");

static void zero_page(uint64_t physical) {
    uint8_t *bytes = (uint8_t *)(uintptr_t)physical;
    for (uint64_t index = 0; index < ARGUS_PAGE_SIZE; ++index) bytes[index] = 0;
}

static void release_process(process_t *process) {
    if (!process) return;
    paging_user_space_destroy(&process->address_space);
    for (uint32_t index = 0; index < process->image_page_count; ++index)
        if (process->image_physical[index])
            (void)pmm_free_page(process->image_physical[index]);
    if (process->stack_physical) (void)pmm_free_page(process->stack_physical);
    if (process->surface_physical && process->surface_page_count)
        (void)pmm_release_pages(
            process->surface_physical,
            process->surface_page_count
        );
    *process = (process_t){0};
}

static int prepare_process(process_t *process, uint64_t pid, uint32_t app_id) {
    if (!process || !kernel_paging || !pid) return 0;
    *process = (process_t){0};
    process->pid = pid;
    process->app_id = app_id;
    if (!paging_user_space_create(kernel_paging, &process->address_space))
        return 0;
    process->stack_virtual = process->address_space.user_base + USER_STACK_OFFSET;
    process->stack_physical = pmm_alloc_page();
    if (!process->stack_physical) {
        release_process(process);
        return 0;
    }
    zero_page(process->stack_physical);
    if (!paging_user_map_page(
            &process->address_space,
            process->stack_virtual,
            process->stack_physical,
            1,
            0)) {
        release_process(process);
        return 0;
    }

    if (app_id) {
        process->surface_page_count = (ARGUS_APP_SURFACE_BYTES +
            ARGUS_PAGE_SIZE - 1u) / ARGUS_PAGE_SIZE;
        process->surface_physical = pmm_alloc_pages(
            process->surface_page_count
        );
        if (!process->surface_physical) {
            release_process(process);
            return 0;
        }
        for (uint32_t page = 0; page < process->surface_page_count; ++page) {
            uint64_t physical = process->surface_physical +
                (uint64_t)page * ARGUS_PAGE_SIZE;
            zero_page(physical);
            if (!paging_user_map_page(
                    &process->address_space,
                    process->address_space.user_base + USER_SURFACE_OFFSET +
                        (uint64_t)page * ARGUS_PAGE_SIZE,
                    physical,
                    1,
                    0)) {
                release_process(process);
                return 0;
            }
        }
    }
    return 1;
}

static void initialize_context(
    process_t *process,
    uint64_t instruction_pointer
) {
    process->context.rip = instruction_pointer;
    process->context.rsp = process->stack_virtual + ARGUS_PAGE_SIZE - 8u;
    process->context.rflags = 0x202u;
    process->context.rdi = process->pid;
    process->state = ARGUS_PROCESS_READY;
}

static int load_flat_probe(
    process_t *process,
    uint64_t pid,
    const uint8_t *image_start,
    const uint8_t *image_end
) {
    if (!process || !image_start || !image_end || image_end <= image_start)
        return 0;
    uint64_t image_size = (uint64_t)(image_end - image_start);
    uint32_t pages = (uint32_t)(
        (image_size + ARGUS_PAGE_SIZE - 1u) / ARGUS_PAGE_SIZE
    );
    if (!pages || pages > ARGUS_ELF_MAX_IMAGE_PAGES ||
        !prepare_process(process, pid, 0u))
        return 0;

    process->code_virtual = process->address_space.user_base + USER_CODE_OFFSET;
    uint64_t copied = 0;
    for (uint32_t page = 0; page < pages; ++page) {
        uint64_t physical = pmm_alloc_page();
        if (!physical) {
            release_process(process);
            return 0;
        }
        process->image_physical[process->image_page_count++] = physical;
        zero_page(physical);
        uint64_t remaining = image_size - copied;
        uint64_t amount = remaining < ARGUS_PAGE_SIZE
            ? remaining : ARGUS_PAGE_SIZE;
        uint8_t *destination = (uint8_t *)(uintptr_t)physical;
        for (uint64_t index = 0; index < amount; ++index)
            destination[index] = image_start[copied + index];
        if (!paging_user_map_page(
                &process->address_space,
                process->code_virtual + (uint64_t)page * ARGUS_PAGE_SIZE,
                physical,
                0,
                1)) {
            release_process(process);
            return 0;
        }
        copied += amount;
    }
    initialize_context(process, process->code_virtual);
    return 1;
}

static int load_elf_app(
    process_t *process,
    uint64_t pid,
    const app_catalog_entry_t *app
) {
    if (!process || !app || !app->image_start || !app->image_end ||
        app->image_end <= app->image_start)
        return 0;
    uint64_t image_size = (uint64_t)(app->image_end - app->image_start);
    argus_elf_image_t image;
    uint64_t image_min = ARGUS_USER_BASE + USER_CODE_OFFSET;
    uint64_t image_max = ARGUS_USER_BASE + USER_STACK_OFFSET - ARGUS_PAGE_SIZE;
    if (!elf_validate_user_image(
            app->image_start,
            image_size,
            image_min,
            image_max,
            &image) ||
        !prepare_process(process, pid, app->app_id))
        return 0;

    process->code_virtual = image.segments[0].virtual_address;
    for (uint32_t segment_index = 0;
         segment_index < image.segment_count;
         ++segment_index) {
        const argus_elf_segment_t *segment = &image.segments[segment_index];
        uint64_t copied = 0;
        for (uint32_t page = 0; page < segment->page_count; ++page) {
            uint64_t physical = pmm_alloc_page();
            if (!physical) {
                release_process(process);
                return 0;
            }
            process->image_physical[process->image_page_count++] = physical;
            zero_page(physical);
            if (copied < segment->file_size) {
                uint64_t remaining = segment->file_size - copied;
                uint64_t amount = remaining < ARGUS_PAGE_SIZE
                    ? remaining : ARGUS_PAGE_SIZE;
                uint8_t *destination = (uint8_t *)(uintptr_t)physical;
                const uint8_t *source = app->image_start +
                    segment->file_offset + copied;
                for (uint64_t index = 0; index < amount; ++index)
                    destination[index] = source[index];
                copied += amount;
            }
            if (!paging_user_map_page(
                    &process->address_space,
                    segment->virtual_address +
                        (uint64_t)page * ARGUS_PAGE_SIZE,
                    physical,
                    (segment->flags & ELF_FLAG_WRITE) != 0,
                    (segment->flags & ELF_FLAG_EXECUTE) != 0)) {
                release_process(process);
                return 0;
            }
        }
    }
    initialize_context(process, image.entry);
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

    return first->address_space.root_table != second->address_space.root_table &&
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
               first->image_physical[0],
               0,
               0);
}

int process_init(const paging_info_t *kernel_space) {
    if (!kernel_space || !kernel_space->root_table) return 0;
    kernel_paging = kernel_space;
    current_process = -1;
    configured_processes = 0;
    completed_processes = 0;
    scheduler_cursor = 0;
    focused_app_id = 0;
    next_pid = 1u;
    syscall_count = 0;
    yield_count = 0;
    context_switch_count = 0;
    write_count = 0;
    fault_count = 0;
    preemption_count = 0;
    wait_count = 0;
    wakeup_count = 0;
    address_spaces_isolated = 0;
    scheduler_ready = 0;
    process_self_test_passed = 0;
    preemption_enabled = 0;
    for (uint32_t index = 0; index < ARGUS_PROCESS_MAX; ++index)
        processes[index] = (process_t){0};

    uint64_t syscall_stack_top =
        (uint64_t)(uintptr_t)(syscall_stack + sizeof(syscall_stack));
    if (!arch_syscall_init(syscall_stack_top)) return 0;
    for (uint32_t index = 0; index < PROCESS_PROBE_COUNT; ++index) {
        if (!load_flat_probe(
                &processes[index],
                next_pid++,
                user_probe_start,
                user_probe_end)) {
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

static uint64_t app_input_poll(process_t *process) {
    if (!process || process->input_tail == process->input_head) return 0;
    uint8_t key = process->input[process->input_tail];
    process->input_tail = (uint8_t)(
        (process->input_tail + 1u) & (APP_INPUT_CAPACITY - 1u)
    );
    ++process->inputs_delivered;
    return key;
}

static int app_surface_valid(const process_t *process) {
    if (!process || !process->surface_physical) return 0;
    const uint8_t *pixels =
        (const uint8_t *)(uintptr_t)process->surface_physical;
    for (uint32_t index = 0; index < ARGUS_APP_SURFACE_BYTES; ++index)
        if (pixels[index] >= ARGUS_APP_PALETTE_COUNT) return 0;
    return 1;
}

static int app_mapping_isolated(const process_t *process) {
    if (!process || !process->app_id || !process->surface_physical) return 0;
    uint64_t physical = 0;
    uint64_t flags = 0;
    uint64_t last_physical = 0;
    return paging_user_translate(
               &process->address_space,
               ARGUS_APP_SURFACE_ADDRESS,
               &physical,
               &flags) &&
           physical == process->surface_physical &&
           (flags & X86_PAGE_WRITABLE) &&
           (!process->address_space.nx_enabled ||
               (flags & X86_PAGE_NO_EXECUTE)) &&
           paging_user_translate(
               &process->address_space,
               ARGUS_APP_SURFACE_ADDRESS + ARGUS_APP_SURFACE_BYTES - 1u,
               &last_physical,
               0) &&
           last_physical == process->surface_physical +
               ARGUS_APP_SURFACE_BYTES - 1u;
}

static void save_interrupt_context(
    process_t *process,
    const interrupt_frame_t *frame
) {
    process->context.r15 = frame->r15;
    process->context.r14 = frame->r14;
    process->context.r13 = frame->r13;
    process->context.r12 = frame->r12;
    process->context.r11 = frame->r11;
    process->context.r10 = frame->r10;
    process->context.r9 = frame->r9;
    process->context.r8 = frame->r8;
    process->context.rdi = frame->rdi;
    process->context.rsi = frame->rsi;
    process->context.rbp = frame->rbp;
    process->context.rdx = frame->rdx;
    process->context.rcx = frame->rcx;
    process->context.rbx = frame->rbx;
    process->context.rax = frame->rax;
    process->context.rip = frame->rip;
    process->context.rflags = frame->rflags;
    process->context.rsp = frame->rsp;
}

uint64_t process_on_user_interrupt(interrupt_frame_t *frame) {
    if (!frame || (frame->cs & 3u) != 3u || current_process < 0 ||
        (uint32_t)current_process >= configured_processes)
        return ARCH_USER_ACTION_RETURN;
    process_t *process = &processes[current_process];
    if (process->state != ARGUS_PROCESS_RUNNING)
        return ARCH_USER_ACTION_RETURN;

    if (frame->vector < 32u) {
        save_interrupt_context(process, frame);
        process->fault_vector = frame->vector;
        process->fault_error = frame->error_code;
        process->fault_rip = frame->rip;
        process->fault_address = frame->vector == 14u ? cpu_read_cr2() : 0u;
        process->state = ARGUS_PROCESS_FAULTED;
        process->active = 0;
        ++fault_count;
        return ARCH_USER_ACTION_FAULT;
    }
    if (frame->vector == ARGUS_APIC_TIMER_VECTOR && preemption_enabled) {
        save_interrupt_context(process, frame);
        process->state = ARGUS_PROCESS_READY;
        ++preemption_count;
        return ARCH_USER_ACTION_PREEMPT;
    }
    return ARCH_USER_ACTION_RETURN;
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
            process->active = 0;
            ++completed_processes;
            return ARCH_USER_ACTION_EXIT;
        case ARGUS_SYSCALL_CLOCK_TICKS:
            context->rax = process->app_id ? apic_timer_ticks() : UINT64_MAX;
            return ARCH_USER_ACTION_RETURN;
        case ARGUS_SYSCALL_INPUT_POLL:
            context->rax = process->app_id
                ? app_input_poll(process) : UINT64_MAX;
            return ARCH_USER_ACTION_RETURN;
        case ARGUS_SYSCALL_APP_PRESENT: {
            if (!process->app_id ||
                context->rsi != sizeof(argus_app_present_v1_t) ||
                !user_range_readable(process, context->rdi, context->rsi)) {
                context->rax = UINT64_MAX;
                return ARCH_USER_ACTION_RETURN;
            }
            const argus_app_present_v1_t *request =
                (const argus_app_present_v1_t *)(uintptr_t)context->rdi;
            if (request->magic != ARGUS_APP_PRESENT_MAGIC ||
                request->abi_version != ARGUS_APP_ABI_VERSION ||
                request->flags != 0u || request->sequence == 0u ||
                (process->present_valid &&
                 request->sequence <= process->present_sequence) ||
                !app_surface_valid(process)) {
                context->rax = UINT64_MAX;
                return ARCH_USER_ACTION_RETURN;
            }
            process->present_sequence = request->sequence;
            process->present_valid = 1;
            context->rax = 0;
            return ARCH_USER_ACTION_RETURN;
        }
        case ARGUS_SYSCALL_EVENT_WAIT: {
            if (!process->app_id) {
                context->rax = UINT64_MAX;
                return ARCH_USER_ACTION_RETURN;
            }
            uint64_t deadline = context->rdi;
            uint64_t now = apic_timer_ticks();
            if (process->input_tail != process->input_head ||
                (deadline != UINT64_MAX && deadline <= now)) {
                context->rax = 0;
                return ARCH_USER_ACTION_RETURN;
            }
            context->rax = 0;
            process->context = *context;
            process->wait_deadline = deadline;
            process->state = ARGUS_PROCESS_BLOCKED;
            ++wait_count;
            return ARCH_USER_ACTION_WAIT;
        }
        default:
            context->rax = UINT64_MAX;
            return ARCH_USER_ACTION_RETURN;
    }
}

static int dispatch_process(uint32_t index) {
    if (index >= configured_processes ||
        processes[index].state != ARGUS_PROCESS_READY)
        return 0;
    process_t *process = &processes[index];
    current_process = (int)index;
    process->state = ARGUS_PROCESS_RUNNING;
    paging_user_activate(&process->address_space);
    uint64_t action = arch_resume_user(&process->context);
    paging_kernel_activate(kernel_paging);
    current_process = -1;
    ++context_switch_count;

    if ((action == ARCH_USER_ACTION_YIELD &&
         process->state == ARGUS_PROCESS_READY) ||
        (action == ARCH_USER_ACTION_EXIT &&
         process->state == ARGUS_PROCESS_EXITED) ||
        (action == ARCH_USER_ACTION_WAIT &&
         process->state == ARGUS_PROCESS_BLOCKED) ||
        (action == ARCH_USER_ACTION_PREEMPT &&
         process->state == ARGUS_PROCESS_READY) ||
        (action == ARCH_USER_ACTION_FAULT &&
         process->state == ARGUS_PROCESS_FAULTED))
        return 1;
    return 0;
}

static void wake_blocked_processes(void) {
    uint64_t now = apic_timer_ticks();
    for (uint32_t index = APP_PROCESS_FIRST;
         index < configured_processes;
         ++index) {
        process_t *process = &processes[index];
        if (process->state != ARGUS_PROCESS_BLOCKED) continue;
        if (process->input_tail == process->input_head &&
            (process->wait_deadline == UINT64_MAX ||
             process->wait_deadline > now))
            continue;
        process->state = ARGUS_PROCESS_READY;
        ++wakeup_count;
    }
}

static int next_ready_process(uint32_t start) {
    if (!configured_processes) return -1;
    for (uint32_t offset = 0; offset < configured_processes; ++offset) {
        uint32_t index = (start + offset) % configured_processes;
        if (processes[index].state != ARGUS_PROCESS_READY) continue;
        if (processes[index].app_id && !processes[index].active) continue;
        return (int)index;
    }
    return -1;
}

static const app_catalog_entry_t *find_catalog(uint32_t app_id) {
    for (uint32_t index = 0; index < APP_PROCESS_COUNT; ++index)
        if (app_catalog[index].app_id == app_id) return &app_catalog[index];
    return 0;
}

static uint32_t app_slot(uint32_t app_id) {
    for (uint32_t index = 0; index < APP_PROCESS_COUNT; ++index)
        if (app_catalog[index].app_id == app_id)
            return APP_PROCESS_FIRST + index;
    return ARGUS_PROCESS_MAX;
}

static process_t *find_app(uint32_t app_id) {
    uint32_t slot = app_slot(app_id);
    if (slot >= ARGUS_PROCESS_MAX ||
        processes[slot].state == ARGUS_PROCESS_UNUSED)
        return 0;
    return &processes[slot];
}

static int start_app_internal(uint32_t app_id) {
    const app_catalog_entry_t *app = find_catalog(app_id);
    uint32_t slot = app_slot(app_id);
    if (!app || slot >= ARGUS_PROCESS_MAX ||
        processes[slot].state != ARGUS_PROCESS_UNUSED ||
        !load_elf_app(&processes[slot], next_pid++, app))
        return 0;
    if (configured_processes <= slot) configured_processes = slot + 1u;
    if (!app_mapping_isolated(&processes[slot])) {
        release_process(&processes[slot]);
        return 0;
    }
    for (uint32_t attempt = 0; attempt < APP_START_DISPATCH_LIMIT; ++attempt) {
        process_t *process = &processes[slot];
        if (process->present_valid && process->state == ARGUS_PROCESS_BLOCKED)
            return 1;
        if (process->state != ARGUS_PROCESS_READY || !dispatch_process(slot))
            break;
    }
    return processes[slot].present_valid &&
           (processes[slot].state == ARGUS_PROCESS_READY ||
            processes[slot].state == ARGUS_PROCESS_BLOCKED);
}

int process_run_self_test(void) {
    if (!scheduler_ready || current_process >= 0) return 0;
    uint32_t next = 0;
    for (uint32_t dispatches = 0; dispatches < 16u; ++dispatches) {
        int selected = next_ready_process(next);
        if (selected < 0) break;
        if (!dispatch_process((uint32_t)selected)) return 0;
        next = ((uint32_t)selected + 1u) % configured_processes;
    }

    if (completed_processes != PROCESS_PROBE_COUNT ||
        configured_processes != PROCESS_PROBE_COUNT ||
        syscall_count != 10u || yield_count != 2u || write_count != 4u ||
        context_switch_count != 4u)
        return 0;
    for (uint32_t index = 0; index < PROCESS_PROBE_COUNT; ++index)
        if (processes[index].state != ARGUS_PROCESS_EXITED ||
            processes[index].exit_status != 0 || processes[index].yields != 1u)
            return 0;
    if (!address_spaces_isolated) return 0;

    if (!load_flat_probe(
            &processes[PROCESS_TEST_SLOT],
            next_pid++,
            user_fault_probe_start,
            user_fault_probe_end))
        return 0;
    configured_processes = PROCESS_TEST_SLOT + 1u;
    if (!dispatch_process(PROCESS_TEST_SLOT) || fault_count != 1u ||
        processes[PROCESS_TEST_SLOT].state != ARGUS_PROCESS_FAULTED ||
        processes[PROCESS_TEST_SLOT].fault_vector != 6u)
        return 0;
    release_process(&processes[PROCESS_TEST_SLOT]);
    configured_processes = PROCESS_PROBE_COUNT;

    if (!load_flat_probe(
            &processes[PROCESS_TEST_SLOT],
            next_pid++,
            user_spin_probe_start,
            user_spin_probe_end))
        return 0;
    configured_processes = PROCESS_TEST_SLOT + 1u;
    preemption_enabled = 1;
    if (!dispatch_process(PROCESS_TEST_SLOT) || preemption_count != 1u ||
        processes[PROCESS_TEST_SLOT].state != ARGUS_PROCESS_READY)
        return 0;
    release_process(&processes[PROCESS_TEST_SLOT]);
    configured_processes = PROCESS_PROBE_COUNT;

    for (uint32_t app = 0; app < APP_PROCESS_COUNT; ++app) {
        if (!start_app_internal(app_catalog[app].app_id)) return 0;
        uint32_t index = APP_PROCESS_FIRST + app;
        if (app && processes[index].surface_physical ==
                processes[index - 1u].surface_physical)
            return 0;
    }
    process_self_test_passed = 1;
    scheduler_cursor = 0;
    return 1;
}

int process_run_ready_once(void) {
    if (!scheduler_ready || !process_self_test_passed || current_process >= 0)
        return 0;
    wake_blocked_processes();
    int selected = next_ready_process(scheduler_cursor);
    if (selected < 0) return 0;
    scheduler_cursor = ((uint32_t)selected + 1u) % configured_processes;
    return dispatch_process((uint32_t)selected);
}

uint32_t process_count(void) {
    uint32_t count = 0;
    for (uint32_t index = 0; index < configured_processes; ++index)
        if (processes[index].state != ARGUS_PROCESS_UNUSED) ++count;
    return count;
}
uint32_t process_completed_count(void) { return completed_processes; }
uint64_t process_syscall_count(void) { return syscall_count; }
uint64_t process_yield_count(void) { return yield_count; }
uint64_t process_context_switch_count(void) { return context_switch_count; }
uint64_t process_fault_count(void) { return fault_count; }
uint64_t process_preemption_count(void) { return preemption_count; }
uint64_t process_wait_count(void) { return wait_count; }
uint64_t process_wakeup_count(void) { return wakeup_count; }
int process_address_space_isolated(void) { return address_spaces_isolated; }
int process_scheduler_online(void) {
    return scheduler_ready && process_self_test_passed && preemption_enabled;
}

uint32_t process_app_count(void) { return APP_PROCESS_COUNT; }

int process_app_online(uint32_t app_id) {
    process_t *process = find_app(app_id);
    return process &&
           (process->state == ARGUS_PROCESS_READY ||
            process->state == ARGUS_PROCESS_BLOCKED ||
            process->state == ARGUS_PROCESS_RUNNING) &&
           process->present_valid;
}

int process_app_start(uint32_t app_id) {
    if (!process_self_test_passed || current_process >= 0) return 0;
    process_t *existing = find_app(app_id);
    if (existing && process_app_online(app_id)) return 1;
    if (existing) release_process(existing);
    if (!start_app_internal(app_id)) return 0;
    if (focused_app_id == app_id) process_app_set_active(app_id);
    return 1;
}

int process_app_stop(uint32_t app_id) {
    if (!process_self_test_passed || current_process >= 0) return 0;
    if (!find_catalog(app_id)) return 0;
    process_t *process = find_app(app_id);
    if (!process) return 1;
    release_process(process);
    return 1;
}

int process_app_restart(uint32_t app_id) {
    if (!process_self_test_passed || current_process >= 0) return 0;
    process_t *process = find_app(app_id);
    int was_active = focused_app_id == app_id;
    if (process) release_process(process);
    if (!start_app_internal(app_id)) return 0;
    if (was_active) process_app_set_active(app_id);
    return 1;
}

int process_app_status(uint32_t app_id, argus_process_app_status_t *status) {
    if (!status || !find_catalog(app_id)) return 0;
    *status = (argus_process_app_status_t){0};
    process_t *process = find_app(app_id);
    if (!process) return 1;
    status->state = process->state;
    status->pid = process->pid;
    status->fault_vector = process->fault_vector;
    status->fault_error = process->fault_error;
    status->fault_rip = process->fault_rip;
    status->fault_address = process->fault_address;
    return 1;
}

void process_app_set_active(uint32_t app_id) {
    focused_app_id = app_id;
    for (uint32_t index = APP_PROCESS_FIRST;
         index < configured_processes;
         ++index)
        processes[index].active = processes[index].app_id == app_id &&
            process_app_online(app_id);
}

int process_app_input(uint32_t app_id, uint8_t key) {
    process_t *process = find_app(app_id);
    if (!process || !process->active || !process->present_valid || !key ||
        (process->state != ARGUS_PROCESS_READY &&
         process->state != ARGUS_PROCESS_BLOCKED))
        return 0;
    uint8_t next = (uint8_t)(
        (process->input_head + 1u) & (APP_INPUT_CAPACITY - 1u)
    );
    if (next == process->input_tail) return 0;
    process->input[process->input_head] = key;
    process->input_head = next;
    if (process->state == ARGUS_PROCESS_BLOCKED) {
        process->state = ARGUS_PROCESS_READY;
        ++wakeup_count;
    }
    return 1;
}

uint64_t process_app_input_count(uint32_t app_id) {
    process_t *process = find_app(app_id);
    return process ? process->inputs_delivered : 0u;
}

int process_app_surface(
    uint32_t app_id,
    const uint8_t **pixels,
    uint32_t *sequence
) {
    process_t *process = find_app(app_id);
    if (!process || !process_app_online(app_id) || !pixels || !sequence)
        return 0;
    *pixels = (const uint8_t *)(uintptr_t)process->surface_physical;
    *sequence = process->present_sequence;
    return 1;
}
