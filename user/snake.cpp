#include "../src/input_keys.h"
#include "../src/snake_abi.h"
#include "../src/user_abi.h"

extern "C" void *memset(void *destination, int value, __SIZE_TYPE__ length) {
    auto *bytes = static_cast<volatile unsigned char *>(destination);
    for (__SIZE_TYPE__ index = 0; index < length; ++index)
        bytes[index] = static_cast<unsigned char>(value);
    return destination;
}

namespace {

constexpr uint64_t kSnakePid = 3u;
constexpr uint64_t kStepTicks = 12u;

uint64_t syscall0(uint64_t number) {
    register uint64_t result __asm__("rax") = number;
    __asm__ volatile(
        "syscall"
        : "+a"(result)
        :
        : "rcx", "r11", "memory"
    );
    return result;
}

uint64_t syscall1(uint64_t number, uint64_t first) {
    register uint64_t result __asm__("rax") = number;
    register uint64_t argument __asm__("rdi") = first;
    __asm__ volatile(
        "syscall"
        : "+a"(result)
        : "D"(argument)
        : "rcx", "r11", "memory"
    );
    return result;
}

uint64_t syscall2(uint64_t number, uint64_t first, uint64_t second) {
    register uint64_t result __asm__("rax") = number;
    register uint64_t argument1 __asm__("rdi") = first;
    register uint64_t argument2 __asm__("rsi") = second;
    __asm__ volatile(
        "syscall"
        : "+a"(result)
        : "D"(argument1), "S"(argument2)
        : "rcx", "r11", "memory"
    );
    return result;
}

class SnakeGame {
public:
    explicit SnakeGame(uint32_t seed) : random_state_(seed ? seed : 1u) {
        reset();
    }

    void handle_input(uint64_t key) {
        if (key == 'r' || key == 'R') {
            reset();
            dirty_ = true;
            return;
        }
        uint8_t next = direction_;
        if (key == 'w' || key == 'W' || key == ARGUS_KEY_UP) next = kUp;
        else if (key == 's' || key == 'S' || key == ARGUS_KEY_DOWN) next = kDown;
        else if (key == 'a' || key == 'A' || key == ARGUS_KEY_LEFT) next = kLeft;
        else if (key == 'd' || key == 'D' || key == ARGUS_KEY_RIGHT) next = kRight;
        if (!opposite(direction_, next)) pending_direction_ = next;
    }

    void step() {
        if (state_ != ARGUS_SNAKE_STATE_PLAYING) return;
        direction_ = pending_direction_;
        int next_x = body_x_[0];
        int next_y = body_y_[0];
        if (direction_ == kUp) --next_y;
        else if (direction_ == kDown) ++next_y;
        else if (direction_ == kLeft) --next_x;
        else ++next_x;

        if (next_x < 0 || next_y < 0 ||
            next_x >= static_cast<int>(ARGUS_SNAKE_GRID_WIDTH) ||
            next_y >= static_cast<int>(ARGUS_SNAKE_GRID_HEIGHT) ||
            occupied(static_cast<uint8_t>(next_x), static_cast<uint8_t>(next_y),
                     length_ - 1u)) {
            state_ = ARGUS_SNAKE_STATE_GAME_OVER;
            dirty_ = true;
            return;
        }

        bool ate = static_cast<uint8_t>(next_x) == food_x_ &&
                   static_cast<uint8_t>(next_y) == food_y_;
        uint32_t new_length = length_;
        if (ate && length_ < ARGUS_SNAKE_MAX_LENGTH) ++new_length;
        for (uint32_t index = new_length - 1u; index > 0u; --index) {
            uint32_t source = index - 1u;
            if (source >= length_) source = length_ - 1u;
            body_x_[index] = body_x_[source];
            body_y_[index] = body_y_[source];
        }
        body_x_[0] = static_cast<uint8_t>(next_x);
        body_y_[0] = static_cast<uint8_t>(next_y);
        length_ = new_length;
        if (ate) {
            ++score_;
            place_food();
        }
        dirty_ = true;
    }

    bool dirty() const { return dirty_; }

    void present() {
        argus_snake_frame_v1_t frame{};
        frame.magic = ARGUS_SNAKE_FRAME_MAGIC;
        frame.abi_version = ARGUS_SNAKE_ABI_VERSION;
        frame.sequence = ++sequence_;
        frame.state = state_;
        frame.score = score_;
        frame.length = length_;
        frame.width = ARGUS_SNAKE_GRID_WIDTH;
        frame.height = ARGUS_SNAKE_GRID_HEIGHT;
        for (uint32_t index = 0; index < length_; ++index) {
            uint32_t cell = static_cast<uint32_t>(body_y_[index]) *
                ARGUS_SNAKE_GRID_WIDTH + body_x_[index];
            frame.cells[cell] = index == 0u
                ? ARGUS_SNAKE_CELL_HEAD : ARGUS_SNAKE_CELL_BODY;
        }
        frame.cells[static_cast<uint32_t>(food_y_) * ARGUS_SNAKE_GRID_WIDTH +
                    food_x_] = ARGUS_SNAKE_CELL_FOOD;
        (void)syscall2(
            ARGUS_SYSCALL_SNAKE_PRESENT,
            reinterpret_cast<uint64_t>(&frame),
            sizeof(frame)
        );
        dirty_ = false;
    }

private:
    static constexpr uint8_t kUp = 0u;
    static constexpr uint8_t kRight = 1u;
    static constexpr uint8_t kDown = 2u;
    static constexpr uint8_t kLeft = 3u;

    static bool opposite(uint8_t first, uint8_t second) {
        return ((first + 2u) & 3u) == second;
    }

    uint32_t random() {
        random_state_ ^= random_state_ << 13;
        random_state_ ^= random_state_ >> 17;
        random_state_ ^= random_state_ << 5;
        return random_state_;
    }

    bool occupied(uint8_t x, uint8_t y, uint32_t count) const {
        for (uint32_t index = 0; index < count; ++index)
            if (body_x_[index] == x && body_y_[index] == y) return true;
        return false;
    }

    void place_food() {
        for (uint32_t attempt = 0; attempt < ARGUS_SNAKE_CELL_COUNT; ++attempt) {
            uint32_t cell = random() % ARGUS_SNAKE_CELL_COUNT;
            uint8_t x = static_cast<uint8_t>(cell % ARGUS_SNAKE_GRID_WIDTH);
            uint8_t y = static_cast<uint8_t>(cell / ARGUS_SNAKE_GRID_WIDTH);
            if (!occupied(x, y, length_)) {
                food_x_ = x;
                food_y_ = y;
                return;
            }
        }
        food_x_ = 0u;
        food_y_ = 0u;
    }

    void reset() {
        state_ = ARGUS_SNAKE_STATE_PLAYING;
        score_ = 0u;
        length_ = 4u;
        direction_ = kRight;
        pending_direction_ = kRight;
        uint8_t center_x = ARGUS_SNAKE_GRID_WIDTH / 2u;
        uint8_t center_y = ARGUS_SNAKE_GRID_HEIGHT / 2u;
        for (uint32_t index = 0; index < length_; ++index) {
            body_x_[index] = static_cast<uint8_t>(center_x - index);
            body_y_[index] = center_y;
        }
        place_food();
        dirty_ = true;
    }

    uint8_t body_x_[ARGUS_SNAKE_MAX_LENGTH]{};
    uint8_t body_y_[ARGUS_SNAKE_MAX_LENGTH]{};
    uint32_t random_state_;
    uint32_t sequence_ = 0u;
    uint32_t state_ = ARGUS_SNAKE_STATE_PLAYING;
    uint32_t score_ = 0u;
    uint32_t length_ = 0u;
    uint8_t direction_ = kRight;
    uint8_t pending_direction_ = kRight;
    uint8_t food_x_ = 0u;
    uint8_t food_y_ = 0u;
    bool dirty_ = false;
};

} // namespace

extern "C" [[noreturn]] __attribute__((section(".text.start")))
void argus_user_start(uint64_t pid) {
    if (pid != kSnakePid || syscall0(ARGUS_SYSCALL_GETPID) != pid)
        (void)syscall1(ARGUS_SYSCALL_EXIT, 1u);

    uint64_t now = syscall0(ARGUS_SYSCALL_CLOCK_TICKS);
    SnakeGame game(static_cast<uint32_t>(now ^ (pid * 0x9E3779B9u)));
    game.present();
    uint64_t previous_step = now;

    for (;;) {
        for (;;) {
            uint64_t key = syscall0(ARGUS_SYSCALL_INPUT_POLL);
            if (!key) break;
            game.handle_input(key);
        }
        now = syscall0(ARGUS_SYSCALL_CLOCK_TICKS);
        if (now - previous_step >= kStepTicks) {
            previous_step = now;
            game.step();
        }
        if (game.dirty()) game.present();
        (void)syscall0(ARGUS_SYSCALL_YIELD);
    }
}
