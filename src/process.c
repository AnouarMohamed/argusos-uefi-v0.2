#include "process.h"
#include "apic.h"
#include "input_keys.h"
#include "pmm.h"
#include "serial.h"
#include "user_abi.h"

#define PROCESS_PROBE_COUNT 2u
#define SNAKE_PROCESS_INDEX PROCESS_PROBE_COUNT
#define SNAKE_PROCESS_PID 3u
#define USER_CODE_OFFSET 0x1000ULL
#define USER_CODE_MAX_PAGES 4u
#define USER_STACK_OFFSET 0x200000ULL
#define USER_SYSCALL_STACK_BYTES 32768u
#define USER_WRITE_LIMIT 128u
#define SNAKE_INPUT_CAPACITY 16u
#define X86_PAGE_WRITABLE (1ULL << 1)
#define X86_PAGE_NO_EXECUTE (1ULL << 63)

typedef struct {
    uint64_t pid;
    argus_process_state_t state;
    paging_user_space_t address_space;
    uint64_t code_physical[USER_CODE_MAX_PAGES];
    uint32_t code_page_count;
    uint64_t stack_physical;
    uint64_t code_virtual;
    uint64_t stack_virtual;
    arch_user_context_t context;
    uint64_t exit_status;
    uint64_t yields;
} process_t;

extern uint8_t user_probe_start[];
extern uint8_t user_probe_end[];
extern uint8_t user_snake_start[];
extern uint8_t user_snake_end[];

static process_t processes[ARGUS_PROCESS_MAX];
static const paging_info_t *kernel_paging;
static int current_process = -1;
static uint32_t configured_processes;
static uint32_t completed_processes;
static uint32_t scheduler_cursor;
static uint64_t syscall_count;
static uint64_t yield_count;
static uint64_t context_switch_count;
static uint64_t write_count;
static int address_spaces_isolated;
static int scheduler_ready;
static int process_self_test_passed;
static int snake_online;
static int snake_active;
static argus_snake_frame_v1_t snake_frame;
static int snake_frame_valid;
static uint8_t snake_input[SNAKE_INPUT_CAPACITY];
static uint8_t snake_input_head;
static uint8_t snake_input_tail;
static uint64_t snake_inputs_delivered;
static uint8_t syscall_stack[USER_SYSCALL_STACK_BYTES]
    __attribute__((aligned(16)));

_Static_assert((SNAKE_INPUT_CAPACITY & (SNAKE_INPUT_CAPACITY - 1u)) == 0,
               "Snake input capacity must be a power of two");
_Static_assert(sizeof(argus_snake_frame_v1_t) == 312u,
               "Snake ABI frame layout changed");
_Static_assert(ARGUS_USER_BASE + USER_CODE_OFFSET == 0x0000008000001000ULL,
               "C++ user link address must match the process image mapping");

static void zero_page(uint64_t physical) {
    uint8_t *bytes = (uint8_t *)(uintptr_t)physical;
    for (uint64_t index = 0; index < ARGUS_PAGE_SIZE; ++index) bytes[index] = 0;
}

static void release_process(process_t *process) {
    if (!process) return;
    paging_user_space_destroy(&process->address_space);
    for (uint32_t index = 0; index < process->code_page_count; ++index)
        if (process->code_physical[index])
            (void)pmm_free_page(process->code_physical[index]);
    if (process->stack_physical) (void)pmm_free_page(process->stack_physical);
    *process = (process_t){0};
}

static int load_user_image(
    process_t *process,
    uint64_t pid,
    const uint8_t *image_start,
    const uint8_t *image_end
) {
    if (!process || !kernel_paging || !image_start || !image_end ||
        image_end <= image_start)
        return 0;
    uint64_t image_size = (uint64_t)(image_end - image_start);
    uint32_t pages = (uint32_t)(
        (image_size + ARGUS_PAGE_SIZE - 1u) / ARGUS_PAGE_SIZE
    );
    if (!pages || pages > USER_CODE_MAX_PAGES) return 0;

    *process = (process_t){0};
    process->pid = pid;
    if (!paging_user_space_create(kernel_paging, &process->address_space))
        return 0;
    process->code_virtual = process->address_space.user_base + USER_CODE_OFFSET;
    process->stack_virtual = process->address_space.user_base + USER_STACK_OFFSET;

    uint64_t copied = 0;
    for (uint32_t page = 0; page < pages; ++page) {
        uint64_t physical = pmm_alloc_page();
        if (!physical) {
            release_process(process);
            return 0;
        }
        process->code_physical[process->code_page_count++] = physical;
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

    process->context.rip = process->code_virtual;
    process->context.rsp = process->stack_virtual + ARGUS_PAGE_SIZE - 8u;
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
               first->code_physical[0],
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
    syscall_count = 0;
    yield_count = 0;
    context_switch_count = 0;
    write_count = 0;
    address_spaces_isolated = 0;
    scheduler_ready = 0;
    process_self_test_passed = 0;
    snake_online = 0;
    snake_active = 0;
    snake_frame = (argus_snake_frame_v1_t){0};
    snake_frame_valid = 0;
    snake_input_head = 0;
    snake_input_tail = 0;
    snake_inputs_delivered = 0;
    for (uint32_t index = 0; index < ARGUS_PROCESS_MAX; ++index)
        processes[index] = (process_t){0};

    uint64_t syscall_stack_top =
        (uint64_t)(uintptr_t)(syscall_stack + sizeof(syscall_stack));
    if (!arch_syscall_init(syscall_stack_top)) return 0;
    for (uint32_t index = 0; index < PROCESS_PROBE_COUNT; ++index) {
        if (!load_user_image(
                &processes[index],
                index + 1u,
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

static int snake_frame_is_valid(const argus_snake_frame_v1_t *frame) {
    if (!frame || frame->magic != ARGUS_SNAKE_FRAME_MAGIC ||
        frame->abi_version != ARGUS_SNAKE_ABI_VERSION ||
        frame->width != ARGUS_SNAKE_GRID_WIDTH ||
        frame->height != ARGUS_SNAKE_GRID_HEIGHT ||
        frame->length < 1u || frame->length > ARGUS_SNAKE_MAX_LENGTH ||
        (frame->state != ARGUS_SNAKE_STATE_PLAYING &&
         frame->state != ARGUS_SNAKE_STATE_GAME_OVER))
        return 0;
    uint32_t snake_cells = 0;
    uint32_t heads = 0;
    uint32_t food = 0;
    for (uint32_t index = 0; index < ARGUS_SNAKE_CELL_COUNT; ++index) {
        uint8_t cell = frame->cells[index];
        if (cell > ARGUS_SNAKE_CELL_FOOD) return 0;
        if (cell == ARGUS_SNAKE_CELL_BODY) ++snake_cells;
        else if (cell == ARGUS_SNAKE_CELL_HEAD) {
            ++snake_cells;
            ++heads;
        } else if (cell == ARGUS_SNAKE_CELL_FOOD) ++food;
    }
    return snake_cells == frame->length && heads == 1u && food == 1u;
}

static uint64_t snake_input_poll(void) {
    if (snake_input_tail == snake_input_head) return 0;
    uint8_t key = snake_input[snake_input_tail];
    snake_input_tail = (uint8_t)(
        (snake_input_tail + 1u) & (SNAKE_INPUT_CAPACITY - 1u)
    );
    ++snake_inputs_delivered;
    return key;
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
        case ARGUS_SYSCALL_CLOCK_TICKS:
            context->rax = process->pid == SNAKE_PROCESS_PID
                ? apic_timer_ticks() : UINT64_MAX;
            return ARCH_USER_ACTION_RETURN;
        case ARGUS_SYSCALL_INPUT_POLL:
            context->rax = process->pid == SNAKE_PROCESS_PID
                ? snake_input_poll() : UINT64_MAX;
            return ARCH_USER_ACTION_RETURN;
        case ARGUS_SYSCALL_SNAKE_PRESENT: {
            if (process->pid != SNAKE_PROCESS_PID ||
                context->rsi != sizeof(argus_snake_frame_v1_t) ||
                !user_range_readable(process, context->rdi, context->rsi)) {
                context->rax = UINT64_MAX;
                return ARCH_USER_ACTION_RETURN;
            }
            const argus_snake_frame_v1_t *candidate =
                (const argus_snake_frame_v1_t *)(uintptr_t)context->rdi;
            if (!snake_frame_is_valid(candidate) ||
                (snake_frame_valid && candidate->sequence <= snake_frame.sequence)) {
                context->rax = UINT64_MAX;
                return ARCH_USER_ACTION_RETURN;
            }
            snake_frame = *candidate;
            snake_frame_valid = 1;
            context->rax = 0;
            return ARCH_USER_ACTION_RETURN;
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
         process->state != ARGUS_PROCESS_READY) ||
        (action == ARCH_USER_ACTION_EXIT &&
         process->state != ARGUS_PROCESS_EXITED) ||
        (action != ARCH_USER_ACTION_YIELD && action != ARCH_USER_ACTION_EXIT))
        return 0;
    return 1;
}

static int next_ready_process(uint32_t start) {
    for (uint32_t offset = 0; offset < configured_processes; ++offset) {
        uint32_t index = (start + offset) % configured_processes;
        if (processes[index].state != ARGUS_PROCESS_READY) continue;
        if (index == SNAKE_PROCESS_INDEX && !snake_active) continue;
        return (int)index;
    }
    return -1;
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
    if (!address_spaces_isolated ||
        !load_user_image(
            &processes[SNAKE_PROCESS_INDEX],
            SNAKE_PROCESS_PID,
            user_snake_start,
            user_snake_end))
        return 0;
    ++configured_processes;
    if (!dispatch_process(SNAKE_PROCESS_INDEX) ||
        processes[SNAKE_PROCESS_INDEX].state != ARGUS_PROCESS_READY ||
        !snake_frame_valid)
        return 0;
    snake_online = 1;
    process_self_test_passed = 1;
    scheduler_cursor = 0;
    return 1;
}

int process_run_ready_once(void) {
    if (!scheduler_ready || !process_self_test_passed || current_process >= 0)
        return 0;
    int selected = next_ready_process(scheduler_cursor);
    if (selected < 0) return 0;
    scheduler_cursor = ((uint32_t)selected + 1u) % configured_processes;
    return dispatch_process((uint32_t)selected);
}

uint32_t process_count(void) { return configured_processes; }
uint32_t process_completed_count(void) { return completed_processes; }
uint64_t process_syscall_count(void) { return syscall_count; }
uint64_t process_yield_count(void) { return yield_count; }
uint64_t process_context_switch_count(void) { return context_switch_count; }
int process_address_space_isolated(void) { return address_spaces_isolated; }
int process_scheduler_online(void) {
    return scheduler_ready && process_self_test_passed;
}
int process_snake_online(void) { return snake_online && snake_frame_valid; }

void process_snake_set_active(int active) {
    snake_active = active != 0 && snake_online;
}

int process_snake_input(uint8_t key) {
    if (!snake_online || !snake_active) return 0;
    if (key >= 'A' && key <= 'Z') key = (uint8_t)(key - 'A' + 'a');
    if (key != 'w' && key != 'a' && key != 's' && key != 'd' && key != 'r' &&
        key != ARGUS_KEY_UP && key != ARGUS_KEY_DOWN &&
        key != ARGUS_KEY_LEFT && key != ARGUS_KEY_RIGHT)
        return 0;
    uint8_t next = (uint8_t)(
        (snake_input_head + 1u) & (SNAKE_INPUT_CAPACITY - 1u)
    );
    if (next == snake_input_tail) return 0;
    snake_input[snake_input_head] = key;
    snake_input_head = next;
    return 1;
}

uint64_t process_snake_input_count(void) { return snake_inputs_delivered; }

int process_snake_frame(argus_snake_frame_v1_t *frame) {
    if (!frame || !snake_frame_valid) return 0;
    *frame = snake_frame;
    return 1;
}
