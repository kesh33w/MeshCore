#pragma once

#include <stdint.h>

extern uint32_t island_link_test_millis;
inline uint32_t millis() { return island_link_test_millis; }
