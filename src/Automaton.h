#pragma once
#include "Types.h"
#include "CARule.h"
#include <QString>
#include <vector>
#include <cstdint>

enum class Backend {
    CpuSingle,
    CpuMulti,
    Cuda
};

struct StepResult {
    double  milliseconds = 0.0;
    QString backendName;
};

struct RulePreset {
    const char* name;
    CARule      rule;
    float       seedDensity; // fraction of cells set alive at reset (0..1)
    bool        seedFull;    // true = seed the whole grid, false = central third
};

// Owns the 3D grid and advances it with the chosen back-end. State is
// authoritative on the host; the CUDA back-end uploads/downloads per step().
class Automaton {
public:
    void reset(const AutomatonParams& p);                 // reallocate + seed
    StepResult step(const AutomatonParams& p, Backend backend);

    // Fill `out` with 4 floats per living/decaying cell: x, y, z, stateNorm.
    void collectInstances(std::vector<float>& out, int& count) const;

    int  gridN() const { return m_N; }
    int  maxState() const { return m_rule.maxState; }

    void saveState();
    void restoreState();

    static int             presetCount();
    static const char*     presetName(int i);
    static bool            cudaSupported();
    static void            shutdown();    // releases CUDA buffers (no-op without CUDA)
    static QString         backendName(Backend b);

private:
    std::vector<uint8_t> m_cur;
    std::vector<uint8_t> m_next;
    std::vector<uint8_t> m_saved;
    int    m_N = 0;
    CARule m_rule { 0, 0, 2 };
    bool   m_wrap = false;

    void stepCpu(bool multiThreaded);
};
