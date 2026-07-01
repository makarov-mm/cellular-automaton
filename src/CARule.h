#pragma once
#include <cstdint>

// The 3D cellular-automaton rule and step logic, shared by the CPU and CUDA
// back-ends so both produce identical results. CA_HD expands to
// __host__ __device__ under nvcc and to nothing under a normal C++ compiler.
#ifdef __CUDACC__
    #define CA_HD __host__ __device__
#else
    #define CA_HD
#endif

// A "life-like" 3D rule over the 26-cell Moore neighborhood, using the
// state-decay model popularized by Softology / Jason Rampe:
//   survive/birth are bitmasks over neighbor counts 0..26
//   maxState = number of states (a dying cell decays maxState-1 -> ... -> 0)
struct CARule {
    uint32_t survive;   // bit c set => a live cell with c live neighbors survives
    uint32_t birth;     // bit c set => a dead cell with c live neighbors is born
    int      maxState;  // >= 2
};

CA_HD inline bool caHas(uint32_t mask, int count) {
    return ((mask >> count) & 1u) != 0u;
}

// Count Moore-neighborhood cells that are fully alive (state == maxState).
// Only fully-alive cells count as living neighbors; decaying cells do not.
CA_HD inline int caCountAlive(const uint8_t* g, int x, int y, int z,
                              int N, int maxState, bool wrap) {
    int count = 0;
    for (int dz = -1; dz <= 1; ++dz)
    for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dy == 0 && dz == 0) continue;
        int nx = x + dx, ny = y + dy, nz = z + dz;
        if (wrap) {
            if (nx < 0) nx += N; else if (nx >= N) nx -= N;
            if (ny < 0) ny += N; else if (ny >= N) ny -= N;
            if (nz < 0) nz += N; else if (nz >= N) nz -= N;
        } else {
            if (nx < 0 || nx >= N || ny < 0 || ny >= N || nz < 0 || nz >= N)
                continue;                          // outside = dead
        }
        if (g[(nz * N + ny) * N + nx] == maxState)
            ++count;
    }
    return count;
}

// Next state for one cell.
CA_HD inline uint8_t caNextState(uint8_t s, int aliveCount, const CARule& r) {
    if (s == r.maxState)                            // fully alive
        return caHas(r.survive, aliveCount) ? (uint8_t)r.maxState
                                            : (uint8_t)(r.maxState - 1);
    if (s == 0)                                     // dead
        return caHas(r.birth, aliveCount) ? (uint8_t)r.maxState : (uint8_t)0;
    return (uint8_t)(s - 1);                        // decaying -> keep fading
}
