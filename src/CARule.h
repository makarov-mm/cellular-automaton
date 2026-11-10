#pragma once

#include <cstdint>

// The rule itself is shared by the CPU back-ends and the CUDA device code, so
// every back-end produces exactly the same grid. CA_HD expands to
// "__host__ __device__" under nvcc and to nothing under a regular C++ compiler.
#ifdef __CUDACC__
    #define CA_HD __host__ __device__
#else
    #define CA_HD
#endif

// A "life-like" 3D rule over the 26-cell Moore neighborhood, written
// survive/birth/states in the notation popularized by Softology (Jason Rampe).
//
// Cell values: 0 is dead, `states - 1` is alive, and the values in between are
// decaying cells that fade by one every step. Only alive cells count as
// neighbors. With two states there is no decay: a cell that fails to survive
// dies at once.
struct CARule {
    std::uint32_t survive;  // bit n set: an alive cell with n alive neighbors stays alive
    std::uint32_t birth;    // bit n set: a dead cell with n alive neighbors is born
    int states;             // total number of cell states, 2..256

    CA_HD constexpr std::uint8_t aliveState() const {
        return static_cast<std::uint8_t>(states - 1);
    }
};

// A cell has 0..26 alive neighbors.
inline constexpr int kNeighborCounts = 27;

CA_HD constexpr bool caHas(std::uint32_t mask, int neighbors) {
    return ((mask >> neighbors) & 1u) != 0u;
}

// Next state of a cell, given its state and its number of alive neighbors.
CA_HD constexpr std::uint8_t caNextState(std::uint8_t state, int aliveNeighbors,
                                         const CARule& rule) {
    const std::uint8_t alive = rule.aliveState();
    const bool born = state == 0 && caHas(rule.birth, aliveNeighbors);
    const bool survives = state == alive && caHas(rule.survive, aliveNeighbors);
    if (born || survives) return alive;
    // Otherwise the dead stay dead and everything else fades by one.
    return state == 0 ? state : static_cast<std::uint8_t>(state - 1);
}
