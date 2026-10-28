#pragma once
#include "Common.h"

namespace Glow {

struct Preset {
    const wchar_t* name;
    uint32_t from;
    uint32_t to;
    uint32_t halo[5];   // conic-gradient halo stops, ported from index.html
};

const Preset* GetPresets(uint32_t& count);

} // namespace Glow
