#pragma once

#include "types.hpp"

namespace drom {

// Persistent copy of the click-timing settings stored in the last flash sector.
// The device boots into the stored values; defaults apply on first (empty) boot.
void persist_load_click(ClickSettings& out);
void persist_save_click(const ClickSettings& in);

}  // namespace drom