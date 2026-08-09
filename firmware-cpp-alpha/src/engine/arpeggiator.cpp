#include "arpeggiator.hpp"

#include "key_filter.hpp"
#include <algorithm>

namespace drom {

// ---------------------------------------------------------------------------
// SimpleRng (xorshift32)
// ---------------------------------------------------------------------------

SimpleRng::SimpleRng(uint32_t seed) { this->seed(seed); }

void SimpleRng::seed(uint32_t s) {
    if (s == 0) { s = 0x9E3779B9u; }
    state_ = s;
}

uint32_t SimpleRng::next() {
    uint32_t x = state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state_ = x;
    return x;
}

uint32_t SimpleRng::range(uint32_t max) {
    if (max == 0) { return 0; }
    return next() % max;
}

int32_t SimpleRng::range_int(int32_t lo, int32_t hi) {
    if (hi <= lo) { return lo; }
    return lo + static_cast<int32_t>(range(static_cast<uint32_t>(hi - lo + 1)));
}

// ---------------------------------------------------------------------------
// Arpeggio build: keyboard-column replicas -> scale filter -> style pattern
// ---------------------------------------------------------------------------

namespace {

// Количество позиций: Steps (доп. транспозиций) + исходная; максимум 16.
uint8_t positions_of(const ArpCfg& cfg) {
    uint16_t p = static_cast<uint16_t>(cfg.steps) + 1u;
    if (p > 16u) { p = 16u; }
    return static_cast<uint8_t>(p);
}

// "Шаги по клавиатуре": каждая нота базы дублируется `steps+1` раз, шагом
// `distance` полутонов вверх (12 = октава), затем набор фильтруется по
// тональности и сортируется.
NoteSet build_keyboard_set(const NoteSet& base, const ArpCfg& cfg, const KeyFilterCfg& kf) {
    NoteSet out {};
    const uint8_t positions = positions_of(cfg);
    const uint8_t step_st = cfg.distance_semitones;
    if (positions <= 1) {  // Steps = 0: без расширения -> «как есть»
        for (uint8_t i = 0; i < base.count && out.count < out.notes.size(); ++i) {
            out.notes[out.count++] = base.notes[i];
        }
        return out;
    }
    const bool filter = kf.enabled &&
                        kf.scale != ScaleId::Off &&
                        static_cast<uint8_t>(kf.scale) < static_cast<uint8_t>(ScaleId::kCount);
    bool seen[128] = {};
    for (uint8_t i = 0; i < base.count; ++i) {
        for (uint8_t k = 0; k < positions; ++k) {
            const uint16_t cand =
                static_cast<uint16_t>(base.notes[i]) + static_cast<uint16_t>(k) * step_st;
            if (cand > 127U) { break; }
            uint8_t note = static_cast<uint8_t>(cand);
            bool muted = false;
            if (filter) {
                note = key_filter_apply(note, kf, muted);
                if (muted) { continue; }
            }
            if (!seen[note] && out.count < out.notes.size()) {
                seen[note] = true;
                out.notes[out.count++] = note;
            }
        }
    }
    for (uint8_t i = 1; i < out.count; ++i) {
        const uint8_t key = out.notes[i];
        int16_t j = static_cast<int16_t>(i) - 1;
        while (j >= 0 && out.notes[j] > key) { out.notes[j + 1] = out.notes[j]; --j; }
        out.notes[j + 1] = key;
    }
    return out;
}

void shuffle(uint8_t* arr, uint8_t n, SimpleRng& rng) {
    for (int16_t i = static_cast<int16_t>(n) - 1; i > 0; --i) {
        const uint8_t j = static_cast<uint8_t>(rng.range(static_cast<uint32_t>(i) + 1));
        const uint8_t t = arr[i];
        arr[i] = arr[j];
        arr[j] = t;
    }
}

}  // namespace

void build_arp_sequence(const NoteSet& base, const ArpCfg& cfg, const KeyFilterCfg& key_filter,
                        std::array<uint8_t, kMaxArpNotes>& out, uint8_t& out_count) {
    out_count = 0;
    if (base.count == 0) {
        return;
    }

    const NoteSet ext = build_keyboard_set(base, cfg, key_filter);
    const uint8_t n = ext.count;
    if (n == 0) {
        return;
    }
    const uint8_t steps = cfg.cycle > 0 ? cfg.cycle : 1;

    const auto push = [&](uint8_t note) {
        if (out_count < out.size()) { out[out_count++] = note; }
    };

    if (cfg.style == ArpStyle::ChordTrigger) {
        for (uint8_t i = 0; i < n && i < out.size(); ++i) { out[i] = ext.notes[i]; }
        out_count = n;
        return;
    }
    if (n == 1) {
        for (uint8_t i = 0; i < steps && out_count < out.size(); ++i) { push(ext.notes[0]); }
        return;
    }

    SimpleRng rng(0xAC010203u ^ static_cast<uint32_t>(base.notes[0]) ^
                  (static_cast<uint32_t>(cfg.distance_semitones) << 8u) ^
                  (static_cast<uint32_t>(cfg.steps) << 4u) ^
                  static_cast<uint32_t>(cfg.style));

    if (cfg.style == ArpStyle::RandomOnce) {
        std::array<uint8_t, kMaxArpNotes> perm {};
        for (uint8_t i = 0; i < n; ++i) { perm[i] = i; }
        shuffle(perm.data(), n, rng);
        for (uint8_t i = 0; i < n; ++i) { out[i] = ext.notes[perm[i]]; }
        out_count = n;
        return;
    }
    if (cfg.style == ArpStyle::RandomOther) {
        std::array<uint8_t, kMaxArpNotes> perm {};
        for (uint8_t i = 0; i < n; ++i) { perm[i] = i; }
        for (uint8_t i = 0; i < steps; ++i) {
            if (i % n == 0) { shuffle(perm.data(), n, rng); }
            push(ext.notes[perm[i % n]]);
        }
        return;
    }
    if (cfg.style == ArpStyle::Random) {
        for (uint8_t i = 0; i < steps; ++i) { push(ext.notes[rng.range(n)]); }
        return;
    }

    // Построение индеck-паттерна для остальных стилей.
    std::array<uint8_t, kMaxArpNotes * 2> pat {};
    uint16_t plen = 0;
    const auto pi = [&](uint8_t idx) {
        if (plen < pat.size()) { pat[plen++] = idx; }
    };

    switch (cfg.style) {
        case ArpStyle::Up:
        case ArpStyle::AsPlayed:
            for (uint8_t i = 0; i < n; ++i) { pi(i); }
            break;
        case ArpStyle::Down:
            for (uint8_t i = 0; i < n; ++i) { pi(static_cast<uint8_t>(n - 1 - i)); }
            break;
        case ArpStyle::UpDown:
            for (uint8_t i = 0; i < n; ++i) { pi(i); }
            for (int16_t i = static_cast<int16_t>(n) - 2; i > 0; --i) { pi(static_cast<uint8_t>(i)); }
            break;
        case ArpStyle::DownUp:
            for (int16_t i = static_cast<int16_t>(n) - 1; i >= 0; --i) { pi(static_cast<uint8_t>(i)); }
            for (int16_t i = 1; i + 1 < n; ++i) { pi(static_cast<uint8_t>(i)); }
            break;
        case ArpStyle::UpDownRep:
            for (int16_t i = 0; i < n; ++i) { pi(static_cast<uint8_t>(i)); }
            for (int16_t i = static_cast<int16_t>(n) - 1; i >= 0; --i) { pi(static_cast<uint8_t>(i)); }
            break;
        case ArpStyle::DownUpRep:
            for (int16_t i = static_cast<int16_t>(n) - 1; i >= 0; --i) { pi(static_cast<uint8_t>(i)); }
            for (int16_t i = 0; i < n; ++i) { pi(i); }
            break;
        case ArpStyle::Converge: {
            for (uint8_t l = 0, r = static_cast<uint8_t>(n - 1); l <= r; ++l, --r) {
                pi(l);
                if (l != r) { pi(r); }
            }
            break;
        }
        case ArpStyle::Diverge: {
            const uint8_t lo_c = static_cast<uint8_t>((n - 1) / 2);
            const uint8_t hi_c = static_cast<uint8_t>(n / 2);
            for (uint8_t d = 0; d <= lo_c; ++d) {
                const uint8_t lo = static_cast<uint8_t>(lo_c - d);
                const uint8_t hi = static_cast<uint8_t>(hi_c + d);
                if (hi < n) {
                    pi(lo);
                    if (hi != lo) { pi(hi); }
                }
            }
            break;
        }
        case ArpStyle::ConvergeDiverge: {
            for (uint8_t l = 0, r = static_cast<uint8_t>(n - 1); l <= r; ++l, --r) {
                pi(l);
                if (l != r) { pi(r); }
            }
            const uint16_t c_len = plen;
            if (c_len > 2) {
                for (uint16_t i = 1; i + 1 < c_len; ++i) { pi(pat[c_len - 1 - i]); }
            }
            break;
        }
        case ArpStyle::PinkyUp: {
            const uint8_t top = static_cast<uint8_t>(n - 1);
            for (uint8_t k = 0; k + 1 < n; ++k) { pi(top); pi(k); }
            pi(top);
            break;
        }
        case ArpStyle::PinkyUpDown: {
            const uint8_t top = static_cast<uint8_t>(n - 1);
            for (uint8_t k = 0; k + 1 < n; ++k) { pi(top); pi(k); }
            for (int16_t k = static_cast<int16_t>(n) - 3; k > 0; --k) { pi(top); pi(static_cast<uint8_t>(k)); }
            pi(top);
            break;
        }
        case ArpStyle::ThumbUp: {
            const uint8_t bottom = 0;
            for (uint8_t k = 1; k < n; ++k) { pi(bottom); pi(k); }
            pi(bottom);
            break;
        }
        case ArpStyle::ThumbUpDown: {
            const uint8_t bottom = 0;
            for (uint8_t k = 1; k < n; ++k) { pi(bottom); pi(k); }
            for (int16_t k = static_cast<int16_t>(n) - 2; k > 1; --k) { pi(bottom); pi(static_cast<uint8_t>(k)); }
            pi(bottom);
            break;
        }
        case ArpStyle::Off:
        case ArpStyle::ChordTrigger:
        case ArpStyle::Random:
        case ArpStyle::RandomOnce:
        case ArpStyle::RandomOther:
        default:
            for (uint8_t i = 0; i < n; ++i) { pi(i); }
            break;
    }

    if (plen == 0) {
        out[0] = ext.notes[0];
        out_count = 1;
        return;
    }
    for (uint8_t i = 0; i < steps; ++i) { push(ext.notes[pat[i % plen]]); }
}

void Arpeggiator::rebuild(const NoteSet& input, const ArpCfg& cfg) {
    reset();
    const KeyFilterCfg kf {};  // паттерн-арп не фильтруется тональностью
    build_arp_sequence(input, cfg, kf, sequence_, count_);
}

bool Arpeggiator::active() const {
    return count_ > 0;
}

uint8_t Arpeggiator::next_note() {
    if (count_ == 0) {
        return 0;
    }
    const uint8_t note = sequence_[index_ % count_];
    index_ = static_cast<uint8_t>((index_ + 1) % count_);
    return note;
}

}  // namespace drom
