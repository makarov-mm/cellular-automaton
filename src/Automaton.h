#pragma once

#include "CARule.h"
#include "CellInstance.h"
#include "CudaAutomaton.h"
#include "ThreadPool.h"

#include <cstdint>
#include <random>
#include <span>
#include <string_view>
#include <vector>

enum class Backend {
    CpuSingle,
    CpuMulti,
    Cuda,
};

struct RulePreset {
    std::string_view name;
    CARule rule;
    float seedDensity;  // fraction of the seeded cells that start alive (0..1)
    bool seedFull;      // seed the whole grid instead of its central third
};

struct StepResult {
    Backend backend;         // back-end that actually ran
    double milliseconds;     // wall time of the whole batch of steps
    std::string_view error;  // set when CUDA was requested but failed
};

// Owns the 3D grid and advances it with the chosen back-end. The grid lives on
// the host; the CUDA back-end uploads and downloads it around each batch.
class Automaton {
public:
    // CellInstance stores coordinates in single bytes.
    static constexpr int kMaxGridSize = 256;

    static std::span<const RulePreset> presets();
    static std::string_view backendName(Backend backend);
    static bool cudaAvailable();

    // Allocates a gridSize^3 grid and seeds it the way the preset recommends.
    void reset(int gridSize, int presetIndex);

    // Toroidal edges instead of dead ones; takes effect on the next step.
    void setWrap(bool wrap) { m_wrap = wrap; }

    // Advances the grid by `steps` steps. If CUDA is requested but fails, the
    // multi-threaded CPU back-end takes over so the simulation keeps running.
    StepResult step(Backend backend, int steps);

    // Times `steps` steps on every available back-end, each starting from the
    // current grid, and leaves the grid as it was.
    std::vector<StepResult> benchmark(int steps);

    // Lists every alive or decaying cell. The result stays valid until the
    // next call or reset().
    std::span<const CellInstance> collectInstances();

    int gridSize() const { return m_size; }
    int aliveState() const { return m_rule.aliveState(); }

private:
    void stepCpu(bool multiThreaded);

    int m_size = 0;
    CARule m_rule{.survive = 0, .birth = 0, .states = 2};
    bool m_wrap = false;

    std::vector<std::uint8_t> m_cells;        // current state, x varies fastest
    std::vector<std::uint8_t> m_next;         // the step being computed
    std::vector<std::uint8_t> m_blockSums;    // per-plane 3x3 alive counts, plus one all-zero plane
    std::vector<std::uint8_t> m_transitions;  // next state for every (state, alive neighbors) pair
    std::vector<CellInstance> m_instances;    // storage behind collectInstances()

    // Seeded once, so every reset produces a different pattern.
    std::mt19937 m_rng{std::random_device{}()};
    ThreadPool m_pool;
    CudaAutomaton m_cuda;
};
