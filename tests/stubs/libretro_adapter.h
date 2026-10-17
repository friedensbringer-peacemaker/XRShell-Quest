// Test-Stub: ersetzt den echten LibretroAdapter für Host-Tests von key_profile.cpp.
// Zeichnet jede Tastenänderung auf, statt sie an einen Core zu geben.
#pragma once

#include "libretro.h"

#include <utility>
#include <vector>

class LibretroAdapter {
public:
    void setKey(enum retro_key key, bool pressed) { events.emplace_back(key, pressed); }
    std::vector<std::pair<enum retro_key, bool>> events;
};
