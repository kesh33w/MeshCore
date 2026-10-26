#include "helpers/bridges/IslandLinkFrame.h"
#include "frame_oracle.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// Can also be linked directly with Clang's -fsanitize=fuzzer,address,undefined.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const bool valid = island_link::validateFrame(data, size) == island_link::FrameError::None;
  if (valid != expectedValid(data, size)) std::abort();
  return 0;
}

#ifdef ISLAND_LINK_STANDALONE_FUZZ
#include <sanitizer/asan_interface.h>

static uint32_t state = 0x1a51a2d3;
static uint32_t randomWord() {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

int main() {
  // Fixed test-corpus storage. ASan poisons every byte beyond the supplied length
  // so a short frame cannot hide an over-read inside a larger backing array.
  alignas(8) uint8_t data[1024];
  uint8_t original[sizeof(data)];
  size_t accepted = 0;
  size_t runs = 0;
  for (size_t iteration = 0; iteration < 1000000; ++iteration) {
    const size_t size = randomWord() % (sizeof(data) + 1);
    for (size_t i = 0; i < size; ++i) data[i] = static_cast<uint8_t>(randomWord());
    // Alternate arbitrary frames with mutations of eligible header/path pairs.
    if (size >= 2 && iteration % 2 == 0) {
      data[0] = static_cast<uint8_t>((randomWord() % 16) * 4 + 1 + randomWord() % 2);
      data[1] = static_cast<uint8_t>((1 + randomWord() % 2) * 64 + randomWord() % 34);
    }
    std::memcpy(original, data, size);
    __asan_poison_memory_region(data + size, sizeof(data) - size);
    LLVMFuzzerTestOneInput(data, size);
    accepted += island_link::validateFrame(data, size) == island_link::FrameError::None;
    __asan_unpoison_memory_region(data, sizeof(data));
    if (std::memcmp(original, data, size) != 0) std::abort();
    ++runs;
  }
  std::printf("PASS: %zu fuzz inputs, seed 0x1a51a2d3; %zu accepted, %zu rejected\n",
              runs, accepted, runs - accepted);
}
#endif
