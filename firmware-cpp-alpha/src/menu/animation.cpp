#include "animation.hpp"

#include <cmath>

namespace drom {

namespace {

// Dunes drift horizontally at ~1.4 px/s (0.12 px per 12 FPS frame) while the
// t-terms keep the gentle vertical breathing from the reference prototype.
float dune_back_y(float x, int t) {
    const float p = x + static_cast<float>(t) * 0.12f;
    return (DisplaySh1106::kHeight - 14)
         + std::sin(p * 0.035f + static_cast<float>(t) * 0.002f) * 5.0f
         + std::sin(p * 0.09f + static_cast<float>(t) * 0.003f + 1.2f) * 2.5f
         + std::sin(p * 0.17f + static_cast<float>(t) * 0.001f + 3.0f) * 1.0f;
}

float dune_front_y(float x, int t) {
    const float p = x + static_cast<float>(t) * 0.12f;
    return (DisplaySh1106::kHeight - 8)
         + std::sin(p * 0.055f + static_cast<float>(t) * 0.0025f + 2.0f) * 3.5f
         + std::sin(p * 0.13f + static_cast<float>(t) * 0.004f + 0.7f) * 1.8f;
}

void draw_px(DisplaySh1106& d, int x, int y) {
    d.set_pixel(x, y, true);
}

}  // namespace

void AnimationRenderer::init_state() {
    for (auto& s : stars_) {
        s.x = static_cast<uint8_t>(rng_.range(static_cast<uint32_t>(kWidth)));
        s.y = static_cast<uint8_t>(rng_.range(static_cast<uint32_t>(kHeight - 22)));
        s.phase = static_cast<uint8_t>(rng_.range(256));
        s.freq = static_cast<uint8_t>(30 + rng_.range(150));  // 0.3..1.8 in 0.01 units
        s.big = (rng_.range(100) > 85);
    }

    for (auto& s : sand_) {
        s.x = static_cast<float>(rng_.range(static_cast<uint32_t>(kWidth)));
        s.y = static_cast<int16_t>(kHeight - 18 + static_cast<int>(rng_.range(14)));
        s.speed = 0.15f + static_cast<float>(rng_.range(100)) * 0.0045f;
        s.len = static_cast<uint8_t>(1 + rng_.range(3));
    }

    comets_.fill(Comet{});
    next_comet_ = 40;
    inited_ = true;
}

void AnimationRenderer::spawn_comet() {
    const bool from_left = rng_.range(100) > 30;
    for (auto& c : comets_) {
        if (c.active) {
            continue;
        }
        c.active = true;
        c.x = from_left
                  ? -5.0f + static_cast<float>(rng_.range(80))
                  : static_cast<float>(kWidth) * 0.4f + static_cast<float>(rng_.range(kWidth * 2)) * 0.25f;
        c.y = static_cast<float>(rng_.range(12));
        c.vx = from_left ? 0.8f + static_cast<float>(rng_.range(80)) * 0.01f
                         : -(0.6f + static_cast<float>(rng_.range(60)) * 0.01f);
        c.vy = 0.25f + static_cast<float>(rng_.range(35)) * 0.01f;
        c.tail = static_cast<uint8_t>(10 + rng_.range(10));
        c.age = 0;
        c.max_age = static_cast<uint8_t>(50 + rng_.range(40));
        break;
    }
}

void AnimationRenderer::draw_stars(DisplaySh1106& d) {
    for (const auto& s : stars_) {
        const float v = std::sin(static_cast<float>(frame_) * 0.05f
                                     * static_cast<float>(s.freq) * 0.01f
                                 + static_cast<float>(s.phase) * 0.0245f);
        if (v < -0.4f) {
            continue;
        }
        const int x = s.x;
        const int y = s.y;
        draw_px(d, x, y);
        if (s.big && v > 0.6f) {
            draw_px(d, x - 1, y);
            draw_px(d, x + 1, y);
            draw_px(d, x, y - 1);
            draw_px(d, x, y + 1);
        }
    }
}

void AnimationRenderer::draw_comets(DisplaySh1106& d) {
    --next_comet_;
    if (next_comet_ <= 0) {
        spawn_comet();
        next_comet_ = 48 + static_cast<int>(rng_.range(60));
    }

    for (auto& c : comets_) {
        if (!c.active) {
            continue;
        }
        c.x += c.vx;
        c.y += c.vy;
        ++c.age;
        if (c.age > c.max_age || c.x > kWidth + 15 || c.x < -15 || c.y > kHeight - 16) {
            c.active = false;
            continue;
        }
        const int hx = static_cast<int>(c.x);
        const int hy = static_cast<int>(c.y);
        draw_px(d, hx, hy);
        for (int t = 1; t < c.tail; ++t) {
            if ((t & 1) == 0) {
                const int tx = static_cast<int>(c.x - c.vx * static_cast<float>(t) * 0.6f);
                const int ty = static_cast<int>(c.y - c.vy * static_cast<float>(t) * 0.6f);
                if (tx >= 0 && tx < kWidth && ty >= 0 && ty < kHeight) {
                    draw_px(d, tx, ty);
                }
            }
        }
    }
}

void AnimationRenderer::draw_dunes(DisplaySh1106& d) {
    // Back dune: dithered fill (mirrors the JS (x+y)%2 pattern).
    for (int x = 0; x < kWidth; ++x) {
        const int dy = static_cast<int>(dune_back_y(static_cast<float>(x), frame_));
        for (int y = dy; y < kHeight; ++y) {
            if (((x + y) & 1) == 0) {
                draw_px(d, x, y);
            }
        }
    }
    // Front dune: solid fill.
    for (int x = 0; x < kWidth; ++x) {
        const int dy = static_cast<int>(dune_front_y(static_cast<float>(x), frame_));
        for (int y = dy; y < kHeight; ++y) {
            draw_px(d, x, y);
        }
    }
    // Crest of the back dune.
    for (int x = 0; x < kWidth; ++x) {
        const int cy = static_cast<int>(dune_back_y(static_cast<float>(x), frame_)) - 1;
        if (cy >= 0) {
            draw_px(d, x, cy);
        }
    }
}

void AnimationRenderer::draw_sand(DisplaySh1106& d) {
    for (auto& s : sand_) {
        s.x += s.speed;
        if (s.x > kWidth + 5) {
            s.x = -3.0f;
            s.y = static_cast<int16_t>(kHeight - 18 + static_cast<int>(rng_.range(14)));
        }
        for (int l = 0; l < s.len; ++l) {
            const int px = static_cast<int>(s.x) - l;
            if (px >= 0 && px < kWidth) {
                draw_px(d, px, s.y);
            }
        }
    }
}

void AnimationRenderer::render(DisplaySh1106& display) {
    if (!inited_) {
        init_state();
    }

    ++frame_;
    display.clear();

    draw_stars(display);
    draw_comets(display);
    display.draw_text_scaled("dromidary", (kWidth - 9 * 6 * 2) / 2, 22, 2);
    draw_dunes(display);
    draw_sand(display);
}

}  // namespace drom