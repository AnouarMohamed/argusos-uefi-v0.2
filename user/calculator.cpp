#include "app_runtime.h"
#include "../src/app_abi.h"

namespace {

constexpr int64_t kLimit = 999999999;

class Calculator {
public:
    void input(uint64_t key) {
        if (key >= '0' && key <= '9') {
            int64_t digit = static_cast<int64_t>(key - '0');
            if (!entering_) { current_ = 0; entering_ = true; }
            if (current_ <= (kLimit - digit) / 10) current_ = current_ * 10 + digit;
            error_ = false;
            dirty_ = true;
            return;
        }
        if (key == '\b') { current_ /= 10; dirty_ = true; return; }
        if (key == 'c' || key == 'C') { clear(); return; }
        if (key == '+' || key == '-' || key == '*' || key == '/') {
            commit();
            pending_ = static_cast<char>(key);
            entering_ = false;
            dirty_ = true;
            return;
        }
        if (key == '=' || key == '\n' || key == '\r') {
            commit();
            pending_ = 0;
            entering_ = false;
            dirty_ = true;
        }
    }

    bool dirty() const { return dirty_; }

    void draw() {
        argus::clear(ARGUS_APP_COLOR_TERMINAL);
        argus::text("KEYBOARD CALCULATOR", 8u, 8u, 1u, ARGUS_APP_COLOR_TEXT);
        argus::text("C CLEAR   BACKSPACE DELETE", 8u, 20u, 1u,
                    ARGUS_APP_COLOR_TEXT);
        argus::rect(8u, 38u, 304u, 42u, ARGUS_APP_COLOR_LCD);
        argus::frame(8u, 38u, 304u, 42u, ARGUS_APP_COLOR_LCD_INK);
        if (error_) {
            argus::text("ERROR", 266u, 52u, 1u, ARGUS_APP_COLOR_LCD_INK);
        } else {
            char number[24];
            argus::signed_decimal(current_, number, sizeof(number));
            uint32_t width = argus::text_width(number, 2u);
            uint32_t x = width + 18u < 304u ? 304u - width : 14u;
            argus::text(number, x, 51u, 2u, ARGUS_APP_COLOR_LCD_INK);
        }
        if (pending_) {
            char operation[2] = {pending_, 0};
            argus::text(operation, 16u, 52u, 1u, ARGUS_APP_COLOR_LCD_INK);
        }
        constexpr char keys[4][5] = {"789/", "456*", "123-", "C0=+"};
        for (uint32_t row = 0; row < 4u; ++row) {
            for (uint32_t column = 0; column < 4u; ++column) {
                uint32_t x = 8u + column * 77u;
                uint32_t y = 90u + row * 31u;
                argus::rect(x, y, 72u, 26u, ARGUS_APP_COLOR_CHROME);
                argus::frame(x, y, 72u, 26u, ARGUS_APP_COLOR_HIGHLIGHT);
                char label[2] = {keys[row][column], 0};
                argus::text(label, x + 33u, y + 9u, 1u,
                            ARGUS_APP_COLOR_TERMINAL);
            }
        }
        dirty_ = false;
    }

private:
    void clear() {
        current_ = 0;
        accumulator_ = 0;
        pending_ = 0;
        entering_ = true;
        has_accumulator_ = false;
        error_ = false;
        dirty_ = true;
    }
    void commit() {
        if (!has_accumulator_) {
            accumulator_ = current_;
            has_accumulator_ = true;
            return;
        }
        if (!pending_ || !entering_) return;
        int64_t result = accumulator_;
        if (pending_ == '+') result = accumulator_ + current_;
        else if (pending_ == '-') result = accumulator_ - current_;
        else if (pending_ == '*') result = accumulator_ * current_;
        else if (pending_ == '/') {
            if (!current_) error_ = true;
            else result = accumulator_ / current_;
        }
        if (result > kLimit || result < -kLimit) error_ = true;
        if (error_) {
            current_ = 0;
            accumulator_ = 0;
            has_accumulator_ = false;
        } else {
            current_ = result;
            accumulator_ = result;
        }
    }

    int64_t current_ = 0;
    int64_t accumulator_ = 0;
    char pending_ = 0;
    bool entering_ = true;
    bool has_accumulator_ = false;
    bool error_ = false;
    bool dirty_ = true;
};

} // namespace

extern "C" [[noreturn]] __attribute__((section(".text.start")))
void argus_user_start(uint64_t pid) {
    if (!pid || argus::pid() != pid) argus::exit(1u);
    Calculator calculator;
    uint32_t sequence = 0u;
    calculator.draw();
    if (!argus::present(++sequence)) argus::exit(2u);
    for (;;) {
        for (;;) {
            uint64_t key = argus::input_poll();
            if (!key) break;
            calculator.input(key);
        }
        if (calculator.dirty()) {
            calculator.draw();
            (void)argus::present(++sequence);
        }
        argus::wait_for_input();
    }
}
