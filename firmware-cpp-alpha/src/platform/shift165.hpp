#pragma once

#include <cstdint>

namespace drom {

class Shift165 {
public:
    void init();
    uint32_t read_all() const;
};

}  // namespace drom
