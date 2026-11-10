#pragma once

#include <cstdint>

// One cube to draw: the grid coordinates of a cell and its state. The
// simulation produces these and the viewport uploads them to the GPU as they
// are, 4 bytes per instance.
struct CellInstance {
    std::uint8_t x, y, z;
    std::uint8_t state;
};

static_assert(sizeof(CellInstance) == 4);
