#pragma once
#ifdef PRIVATE_BENCH_STARTUP
#ifdef PRIVATE_OFFSITE_REPEATER
#include "PrivateRemoteRadioGenerated.h"
#else
// Sacramento defaults for new installations. Saved preferences take precedence.
#undef LORA_FREQ
#undef LORA_BW
#undef LORA_SF
#undef LORA_CR
#undef LORA_TX_POWER
#ifndef PRIVATE_INSTALL_IDENTITY
#undef MAX_LORA_TX_POWER
#endif
#define LORA_FREQ 909.875f
#define LORA_BW 62.5f
#define LORA_SF 9
#define LORA_CR 5
#define LORA_TX_POWER 10
#ifndef PRIVATE_INSTALL_IDENTITY
#define MAX_LORA_TX_POWER 10
#endif
#endif
#endif
