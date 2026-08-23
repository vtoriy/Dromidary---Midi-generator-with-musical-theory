#pragma once

#include <array>
#include <cstdint>

#include "../engine/arpeggiator.hpp"
#include "../platform/display_sh1106.hpp"

namespace drom {

// "Night Desert" animation for the Animation screen, mirroring the reference
// 132x64 JS prototype: twinkling stars, comets, two layered dunes and wind
// sand, with the "dromidary" title. Monochrome 2-colour, 12 FPS.
class AnimationRenderer {
public:
    // Re-seed and rebuild the scene so every entry into the Animation screen
    // starts from a different stage (star field, sand, dune phase, comet wait).
    void restart(uint32_t seed);

    void render(DisplaySh1106& display);

private:
    static constexpr int kWidth = DisplaySh1106::kWidth;    // 132
    static constexpr int kHeight = DisplaySh1106::kHeight;  // 64
    static constexpr int kStarCount = 55;
    static constexpr int kCometMax = 8;
    static constexpr int kSandCount = 30;

    struct Star {
        uint8_t x;
        uint8_t y;
        uint8_t phase;
        uint8_t freq;
        bool big;
    };

    struct Comet {
        bool active;
        float x;
        float y;
        float vx;
        float vy;
        uint8_t tail;
        int16_t age;
        uint8_t max_age;
    };

    struct Sand {
        float x;
        int16_t y;
        float speed;
        uint8_t len;
    };

    void init_state();
    void spawn_comet();
    void draw_stars(DisplaySh1106& d);
    void draw_comets(DisplaySh1106& d);
    void draw_dunes(DisplaySh1106& d);
    void draw_sand(DisplaySh1106& d);

    bool inited_ {false};
    int frame_ {0};
    int next_comet_ {40};

    std::array<Star, kStarCount> stars_ {};
    std::array<Comet, kCometMax> comets_ {};
    std::array<Sand, kSandCount> sand_ {};
    SimpleRng rng_ {0x7A1D90CEu};
};

}  // namespace drom