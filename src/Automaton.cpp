#include "Automaton.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <utility>

namespace {

using Clock = std::chrono::steady_clock;

// Below this many cells per thread, waking a worker costs more than it saves.
constexpr std::size_t kMinCellsPerThread = 32'768;

constexpr std::uint8_t toByte(int value) {
    return static_cast<std::uint8_t>(value);
}

// Bitmask over alive-neighbor counts, from single values...
constexpr std::uint32_t counts(std::initializer_list<int> values) {
    std::uint32_t mask = 0;
    for (const int value : values)
        mask |= 1u << value;
    return mask;
}

// ...or from an inclusive range.
constexpr std::uint32_t countRange(int first, int last) {
    std::uint32_t mask = 0;
    for (int value = first; value <= last; ++value)
        mask |= 1u << value;
    return mask;
}

// A few well-known, good-looking rules, each with the seed that suits it.
// Clouds only survives and is born with 13+ alive neighbors, so it needs a
// dense seed across the whole grid; a sparse central one dies at once.
// clang-format off
constexpr auto kPresets = std::to_array<RulePreset>({
    // name          survive             birth                          states density full grid
    {"445",         {counts({4}),        counts({4}),                   5},    0.28f,  false},
    {"Pyroclastic", {countRange(4, 7),   countRange(6, 8),              10},   0.35f,  false},
    {"Amoeba",      {countRange(9, 26),  counts({5, 6, 7, 12, 13, 15}), 5},    0.30f,  false},
    {"Clouds",      {countRange(13, 26), counts({13, 14, 17, 18, 19}),  2},    0.50f,  true},
    {"Crystal",     {countRange(5, 8),   counts({1, 3}),                5},    0.12f,  false},
});
// clang-format on

double millisecondsSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Index of the row or plane next to `i`. Outside a non-wrapping grid this is
// `n`: the buffers it is used with keep an all-zero, i.e. dead, row or plane
// at that position.
constexpr int neighborIndex(int i, int n, bool wrap) {
    if (i >= 0 && i < n) return i;
    return wrap ? (i + n) % n : n;
}

// First pass, one z-plane of n*n cells: for every cell, the number of alive
// cells in the 3x3 block around it within the plane (the cell itself included).
void sumBlocksInPlane(const std::uint8_t* cells, std::uint8_t* sums, int n, std::uint8_t alive,
                      bool wrap) {
    // rowSums[y][x] = alive cells among x-1, x, x+1 of row y. Row n is the dead
    // row; the array is left uninitialized because every other row is written
    // before it is read.
    std::array<std::uint8_t, (Automaton::kMaxGridSize + 1) * Automaton::kMaxGridSize> rowSums;
    std::fill_n(rowSums.data() + n * n, n, std::uint8_t{0});

    for (int y = 0; y < n; ++y) {
        const std::uint8_t* row = cells + y * n;
        std::uint8_t* out = rowSums.data() + y * n;
        const auto isAlive = [=](int x) { return row[x] == alive ? 1 : 0; };
        out[0] = toByte(isAlive(0) + isAlive(1) + (wrap ? isAlive(n - 1) : 0));
        for (int x = 1; x < n - 1; ++x)
            out[x] = toByte(isAlive(x - 1) + isAlive(x) + isAlive(x + 1));
        out[n - 1] = toByte(isAlive(n - 2) + isAlive(n - 1) + (wrap ? isAlive(0) : 0));
    }

    for (int y = 0; y < n; ++y) {
        const std::uint8_t* above = rowSums.data() + neighborIndex(y - 1, n, wrap) * n;
        const std::uint8_t* here = rowSums.data() + y * n;
        const std::uint8_t* below = rowSums.data() + neighborIndex(y + 1, n, wrap) * n;
        std::uint8_t* out = sums + y * n;
        for (int x = 0; x < n; ++x)
            out[x] = toByte(above[x] + here[x] + below[x]);
    }
}

// Second pass, one z-plane: the block sums of the planes below, here and
// above add up to the 3x3x3 count; without the cell itself that is its number
// of alive neighbors, which selects the next state from the transition table.
void applyRuleToPlane(const std::uint8_t* cells, std::uint8_t* next, const std::uint8_t* below,
                      const std::uint8_t* here, const std::uint8_t* above, int cellCount,
                      std::uint8_t alive, const std::uint8_t* transitions) {
    for (int i = 0; i < cellCount; ++i) {
        const int neighbors = below[i] + here[i] + above[i] - (cells[i] == alive ? 1 : 0);
        next[i] = transitions[cells[i] * kNeighborCounts + neighbors];
    }
}

}  // namespace

std::span<const RulePreset> Automaton::presets() {
    return kPresets;
}

std::string_view Automaton::backendName(Backend backend) {
    switch (backend) {
        using enum Backend;
        case CpuSingle: return "CPU (single-threaded)";
        case CpuMulti:  return "CPU (multi-threaded)";
        case Cuda:      return "CUDA (GPU)";
    }
    std::unreachable();
}

bool Automaton::cudaAvailable() {
    return CudaAutomaton::available();
}

void Automaton::reset(int gridSize, int presetIndex) {
    assert(gridSize >= 3 && gridSize <= kMaxGridSize);
    assert(presetIndex >= 0 && presetIndex < static_cast<int>(kPresets.size()));
    const RulePreset& preset = kPresets[static_cast<std::size_t>(presetIndex)];

    m_size = gridSize;
    m_rule = preset.rule;

    const auto side = static_cast<std::size_t>(gridSize);
    const std::size_t planeCells = side * side;
    const std::size_t cellCount = planeCells * side;
    m_cells.assign(cellCount, 0);
    m_next.resize(cellCount);
    m_blockSums.assign(cellCount + planeCells, 0);  // the extra plane stays dead
    m_instances.resize(cellCount);

    // Tabulating the rule keeps branches out of the per-cell loop.
    m_transitions.resize(static_cast<std::size_t>(m_rule.states) * kNeighborCounts);
    auto transition = m_transitions.begin();
    for (int state = 0; state < m_rule.states; ++state)
        for (int neighbors = 0; neighbors < kNeighborCounts; ++neighbors)
            *transition++ = caNextState(toByte(state), neighbors, m_rule);

    const int first = preset.seedFull ? 0 : gridSize / 3;
    const int last = preset.seedFull ? gridSize : 2 * gridSize / 3;
    std::bernoulli_distribution startsAlive(preset.seedDensity);
    for (int z = first; z < last; ++z)
        for (int y = first; y < last; ++y) {
            std::uint8_t* row = m_cells.data() + (z * gridSize + y) * gridSize;
            for (int x = first; x < last; ++x)
                if (startsAlive(m_rng)) row[x] = m_rule.aliveState();
        }
}

void Automaton::stepCpu(bool multiThreaded) {
    const int n = m_size;
    const int planeCells = n * n;
    const std::uint8_t alive = m_rule.aliveState();
    const bool wrap = m_wrap;

    const auto plane = [planeCells](auto& buffer, int z) { return buffer.data() + z * planeCells; };
    const auto forEachPlane = [&](const std::function<void(int)>& body) {
        if (multiThreaded) {
            m_pool.parallelFor(n, body, static_cast<int>(m_cells.size() / kMinCellsPerThread));
        } else {
            for (int z = 0; z < n; ++z)
                body(z);
        }
    };

    // Counting the 26 neighbors is separable: sum 3x3 blocks inside every
    // plane first, then combine three planes per cell.
    forEachPlane(
        [&](int z) { sumBlocksInPlane(plane(m_cells, z), plane(m_blockSums, z), n, alive, wrap); });
    forEachPlane([&](int z) {
        applyRuleToPlane(plane(m_cells, z), plane(m_next, z),
                         plane(m_blockSums, neighborIndex(z - 1, n, wrap)), plane(m_blockSums, z),
                         plane(m_blockSums, neighborIndex(z + 1, n, wrap)), planeCells, alive,
                         m_transitions.data());
    });
    m_cells.swap(m_next);
}

StepResult Automaton::step(Backend backend, int steps) {
    std::string_view error;
    if (backend == Backend::Cuda) {
        const CudaStepResult gpu = m_cuda.step(m_cells.data(), m_size, m_rule, m_wrap, steps);
        if (!gpu.error)
            return {.backend = Backend::Cuda, .milliseconds = gpu.milliseconds, .error = {}};
        error = gpu.error;
        backend = Backend::CpuMulti;
    }

    const auto start = Clock::now();
    for (int i = 0; i < steps; ++i)
        stepCpu(backend == Backend::CpuMulti);
    return {.backend = backend, .milliseconds = millisecondsSince(start), .error = error};
}

std::vector<StepResult> Automaton::benchmark(int steps) {
    const std::vector<std::uint8_t> start = m_cells;
    std::vector<StepResult> results;
    for (const Backend backend : {Backend::CpuSingle, Backend::CpuMulti, Backend::Cuda}) {
        if (backend == Backend::Cuda && !cudaAvailable()) continue;
        // An untimed step first: it wakes the worker threads, and on the GPU
        // it creates the context and the device buffers.
        step(backend, 1);
        m_cells = start;
        results.push_back(step(backend, steps));
        m_cells = start;
    }
    return results;
}

std::span<const CellInstance> Automaton::collectInstances() {
    CellInstance* end = m_instances.data();
    const std::uint8_t* cell = m_cells.data();
    for (int z = 0; z < m_size; ++z)
        for (int y = 0; y < m_size; ++y)
            for (int x = 0; x < m_size; ++x, ++cell) {
                // Written unconditionally and kept only if the cell is not
                // dead: cheaper than a branch that is hard to predict.
                *end = {.x = toByte(x), .y = toByte(y), .z = toByte(z), .state = *cell};
                end += (*cell != 0) ? 1 : 0;
            }
    return {m_instances.data(), end};
}
