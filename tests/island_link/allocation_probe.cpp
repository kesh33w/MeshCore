#include "helpers/bridges/IslandLinkFrame.h"

// Compile without optimization, then inspect undefined symbols. No external
// calls means this validator cannot reach an allocator, parser, or copy routine.
extern "C" island_link::FrameError island_link_allocation_probe(const uint8_t* data,
                                                               size_t length) {
  return island_link::validateFrame(data, length);
}
