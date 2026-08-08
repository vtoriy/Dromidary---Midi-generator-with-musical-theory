#pragma once

#include <cstdint>

namespace drom {

// SH1106 132x64 monochrome OLED (I2C) with a CPU-side framebuffer.
// Text is drawn into the framebuffer; flush() pushes the whole frame over I2C.
class DisplaySh1106 {
public:
    static constexpr int kWidth = 132;
    static constexpr int kHeight = 64;

    void init();
    void clear();
    void set_pixel(int x, int y, bool on);
    void fill_rect(int x, int y, int w, int h, bool on);
    void draw_char(char c, int x, int y);   // 5x7 glyph, advance 6 px
    void draw_text(const char* text, int x, int y);
    void draw_text_px(const char* text, int x, int y, bool value); // glyphs set to `value`
    void draw_text_scaled(const char* text, int x, int y, int scale);
    void flush();

    static constexpr int kTextAdvance = 6;

private:
    uint8_t fb_[kWidth * kHeight / 8] {};
    void cmd(uint8_t c);
    void data(const uint8_t* buf, int len);
};

}  // namespace drom