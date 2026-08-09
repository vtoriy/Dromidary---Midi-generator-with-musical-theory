#include "persist.hpp"

#include <cstring>

#include "hardware/flash.h"
#include "pico/flash.h"
#include "pico/stdlib.h"

namespace drom {

namespace {

constexpr uint32_t kMagic = 0x44524F4D;  // "DROM"
constexpr uint32_t kVersion = 1;
constexpr uint32_t kFlashOffset = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;

struct PersistBlock {
    uint32_t magic;
    uint32_t version;
    ClickSettings click;
    uint32_t crc;
};

uint32_t crc32(const uint8_t* data, std::size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            const uint32_t mask = static_cast<uint32_t>(-static_cast<int32_t>(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

void write_block(PersistBlock* blk) {
    flash_range_erase(kFlashOffset, FLASH_SECTOR_SIZE);
    flash_range_program(kFlashOffset, reinterpret_cast<const uint8_t*>(blk),
                        sizeof(PersistBlock));
}

}  // namespace

void persist_load_click(ClickSettings& out) {
    const auto* blk = reinterpret_cast<const PersistBlock*>(XIP_BASE + kFlashOffset);
    if (blk->magic != kMagic || blk->version != kVersion) {
        return;  // first boot / wiped sector -> keep compiled defaults
    }
    const uint32_t expected = crc32(reinterpret_cast<const uint8_t*>(&blk->click),
                                    sizeof(blk->click));
    if (expected == blk->crc) {
        out = blk->click;
    }
}

void persist_save_click(const ClickSettings& in) {
    static PersistBlock blk {};
    blk.magic = kMagic;
    blk.version = kVersion;
    blk.click = in;
    blk.crc = crc32(reinterpret_cast<const uint8_t*>(&blk.click), sizeof(blk.click));
    flash_safe_execute(reinterpret_cast<void (*)(void*)>(write_block), &blk, 1000);
}

}  // namespace drom