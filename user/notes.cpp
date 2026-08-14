#include "app_runtime.h"
#include "../src/app_abi.h"

namespace {

constexpr uint64_t kNotesPid = 5u;
constexpr uint32_t kCapacity = 512u;
constexpr uint32_t kColumns = 48u;
constexpr uint32_t kRows = 17u;

class Notes {
public:
    void input(uint64_t key) {
        if (key == '\b') {
            if (length_) --length_;
            dirty_ = true;
            return;
        }
        if (key == '\r') key = '\n';
        if ((key == '\n' || (key >= 32u && key <= 126u)) &&
            length_ < kCapacity) {
            content_[length_++] = static_cast<char>(key);
            dirty_ = true;
        }
    }
    bool dirty() const { return dirty_; }
    void draw() {
        argus::clear(ARGUS_APP_COLOR_TERMINAL);
        argus::rect(0u, 0u, ARGUS_APP_SURFACE_WIDTH, 24u,
                    ARGUS_APP_COLOR_CHROME);
        argus::text("NOTES", 8u, 8u, 1u, ARGUS_APP_COLOR_TERMINAL);
        argus::text("VOLATILE MEMORY", 220u, 8u, 1u,
                    ARGUS_APP_COLOR_TERMINAL);
        uint32_t row = 0u;
        uint32_t column = 0u;
        for (uint32_t index = 0; index < length_ && row < kRows; ++index) {
            char character = content_[index];
            if (character == '\n') { ++row; column = 0u; continue; }
            char glyph[2] = {character, 0};
            argus::text(glyph, 8u + column * 6u, 34u + row * 10u, 1u,
                        ARGUS_APP_COLOR_TEXT);
            ++column;
            if (column == kColumns) { ++row; column = 0u; }
        }
        if (row < kRows)
            argus::rect(8u + column * 6u, 34u + row * 10u + 7u, 5u, 1u,
                        ARGUS_APP_COLOR_TEXT);
        char count[12];
        argus::unsigned_decimal(length_, count, sizeof(count));
        argus::text(count, 8u, 211u, 1u, ARGUS_APP_COLOR_TEXT);
        argus::text("/ 512 BYTES", 8u + argus::text_width(count, 1u), 211u, 1u,
                    ARGUS_APP_COLOR_TEXT);
        dirty_ = false;
    }

private:
    char content_[kCapacity]{};
    uint32_t length_ = 0u;
    bool dirty_ = true;
};

} // namespace

extern "C" [[noreturn]] __attribute__((section(".text.start")))
void argus_user_start(uint64_t pid) {
    if (pid != kNotesPid || argus::pid() != pid) argus::exit(1u);
    Notes notes;
    uint32_t sequence = 0u;
    notes.draw();
    if (!argus::present(++sequence)) argus::exit(2u);
    for (;;) {
        for (;;) {
            uint64_t key = argus::input_poll();
            if (!key) break;
            notes.input(key);
        }
        if (notes.dirty()) { notes.draw(); (void)argus::present(++sequence); }
        argus::yield();
    }
}
