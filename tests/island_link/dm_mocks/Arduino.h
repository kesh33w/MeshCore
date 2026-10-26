#pragma once

#include <stdint.h>

extern uint32_t dm_test_millis;
inline uint32_t millis() { return dm_test_millis; }
