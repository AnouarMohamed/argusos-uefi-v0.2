#include "app_runtime.h"
#include "snake_game.h"
#include "../src/app_abi.h"
#include "../src/input_keys.h"

namespace {

constexpr uint64_t kSnakePid = 3u;
constexpr uint64_t kStepTicks = 12u;

class SnakeGame {
public:
    explicit SnakeGame(uint32_t seed) : random_state_(seed ? seed : 1u) { reset(); }

    void input(uint64_t key) {
        if (key == 'r' || key == 'R') { reset(); return; }
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
        if (ate) { ++score_; place_food(); }
        dirty_ = true;
    }

    bool dirty() const { return dirty_; }

    void draw() {
        argus::clear(ARGUS_APP_COLOR_LCD);
        argus::text("SCORE", 8u, 8u, 1u, ARGUS_APP_COLOR_LCD_INK);
        char score[12];
        argus::unsigned_decimal(score_, score, sizeof(score));
        argus::text(score, 50u, 8u, 1u, ARGUS_APP_COLOR_LCD_INK);
        argus::text("ARROWS OR WASD   R RESET", 166u, 8u, 1u,
                    ARGUS_APP_COLOR_LCD_INK);

        constexpr uint32_t cell = 10u;
        constexpr uint32_t board_width = ARGUS_SNAKE_GRID_WIDTH * cell + 2u;
        constexpr uint32_t board_height = ARGUS_SNAKE_GRID_HEIGHT * cell + 2u;
        constexpr uint32_t board_x = (ARGUS_APP_SURFACE_WIDTH - board_width) / 2u;
        constexpr uint32_t board_y = 42u;
        argus::frame(board_x, board_y, board_width, board_height,
                     ARGUS_APP_COLOR_LCD_INK);
        for (uint32_t index = 0; index < length_; ++index) {
            uint32_t x = board_x + 1u + body_x_[index] * cell;
            uint32_t y = board_y + 1u + body_y_[index] * cell;
            uint32_t inset = index == 0u ? 1u : 2u;
            argus::rect(x + inset, y + inset, cell - inset * 2u,
                        cell - inset * 2u, ARGUS_APP_COLOR_LCD_INK);
        }
        uint32_t food_x = board_x + 1u + food_x_ * cell;
        uint32_t food_y = board_y + 1u + food_y_ * cell;
        argus::frame(food_x + 2u, food_y + 2u, cell - 4u, cell - 4u,
                     ARGUS_APP_COLOR_LCD_INK);
        if (state_ == ARGUS_SNAKE_STATE_GAME_OVER) {
            constexpr const char *message = "GAME OVER   R RESET";
            uint32_t width = argus::text_width(message, 1u);
            uint32_t x = (ARGUS_APP_SURFACE_WIDTH - width) / 2u;
            argus::rect(x - 5u, 106u, width + 10u, 15u, ARGUS_APP_COLOR_LCD);
            argus::frame(x - 5u, 106u, width + 10u, 15u,
                         ARGUS_APP_COLOR_LCD_INK);
            argus::text(message, x, 110u, 1u, ARGUS_APP_COLOR_LCD_INK);
        }
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
            uint32_t position = random() % ARGUS_SNAKE_CELL_COUNT;
            uint8_t x = static_cast<uint8_t>(position % ARGUS_SNAKE_GRID_WIDTH);
            uint8_t y = static_cast<uint8_t>(position / ARGUS_SNAKE_GRID_WIDTH);
            if (!occupied(x, y, length_)) { food_x_ = x; food_y_ = y; return; }
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
    if (pid != kSnakePid || argus::pid() != pid) argus::exit(1u);
    uint64_t now = argus::ticks();
    SnakeGame game(static_cast<uint32_t>(now ^ (pid * 0x9E3779B9u)));
    uint32_t sequence = 0u;
    uint64_t previous_step = now;
    game.draw();
    if (!argus::present(++sequence)) argus::exit(2u);
    for (;;) {
        for (;;) {
            uint64_t key = argus::input_poll();
            if (!key) break;
            game.input(key);
        }
        now = argus::ticks();
        if (now - previous_step >= kStepTicks) {
            previous_step = now;
            game.step();
        }
        if (game.dirty()) { game.draw(); (void)argus::present(++sequence); }
        argus::yield();
    }
}
