#include "Automaton.h"

#include <chrono>
#include <thread>
#include <algorithm>
#include <functional>
#include <random>

#ifdef HAVE_CUDA
#include "CudaAutomaton.h"
#endif

namespace {

// Build a bitmask over neighbor counts from an inclusive range [lo, hi].
constexpr uint32_t maskRange(int lo, int hi) {
    uint32_t m = 0;
    for (int i = lo; i <= hi; ++i) m |= (1u << i);
    return m;
}

// A few well-known aesthetically pleasing 3D rules (Moore-26 neighborhood).
const RulePreset kPresets[] = {
    // name            survive              birth                            states  density  full-grid
    { "445",          { maskRange(4,4),    maskRange(4,4),                   5  },   0.28f,   false },
    { "Pyroclastic",  { maskRange(4,7),    maskRange(6,8),                   10 },   0.35f,   false },
    { "Amoeba",       { maskRange(9,26),   maskRange(5,7) | (1u<<12) | (1u<<13) | (1u<<15), 5 }, 0.30f, false },
    // Clouds needs a dense, whole-grid seed: it only survives/is born with a
    // high live-neighbor count (13+), so a sparse central seed dies instantly.
    { "Clouds",       { maskRange(13,26),  maskRange(13,14) | maskRange(17,19), 2 }, 0.50f,   true  },
    { "Crystal",      { maskRange(5,8),    (1u<<1) | (1u<<3),                5  },   0.12f,   false },
};
const int kPresetCount = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

// Advance z-slabs [zStart, zEnd) by one step.
void stepSlab(int N, const CARule& rule, bool wrap,
              uint8_t* out, const uint8_t* in, int zStart, int zEnd) {
    for (int z = zStart; z < zEnd; ++z)
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                int idx = (z * N + y) * N + x;
                int c   = caCountAlive(in, x, y, z, N, rule.maxState, wrap);
                out[idx] = caNextState(in[idx], c, rule);
            }
}

} // namespace

int         Automaton::presetCount()        { return kPresetCount; }
const char* Automaton::presetName(int i)    { return kPresets[(i >= 0 && i < kPresetCount) ? i : 0].name; }

void Automaton::reset(const AutomatonParams& p) {
    int idx = (p.rulePreset >= 0 && p.rulePreset < kPresetCount) ? p.rulePreset : 0;
    const RulePreset& ps = kPresets[idx];

    m_N    = p.gridN;
    m_wrap = p.wrap;
    m_rule = ps.rule;

    size_t n = (size_t)m_N * m_N * m_N;
    m_cur.assign(n, 0);
    m_next.assign(n, 0);

    // Seed the region and density recommended for this rule. The RNG is
    // persistent across resets so "Reset / reseed" really produces a new
    // pattern each time (a fixed srand here would repeat the same grid).
    static std::mt19937 rng{ std::random_device{}() };
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    int lo = ps.seedFull ? 0 : m_N / 3;
    int hi = ps.seedFull ? m_N : 2 * m_N / 3;
    for (int z = lo; z < hi; ++z)
        for (int y = lo; y < hi; ++y)
            for (int x = lo; x < hi; ++x)
                if (dist(rng) < ps.seedDensity)
                    m_cur[(z * m_N + y) * m_N + x] = (uint8_t)m_rule.maxState;
}

void Automaton::stepCpu(bool multiThreaded) {
    if (!multiThreaded) {
        stepSlab(m_N, m_rule, m_wrap, m_next.data(), m_cur.data(), 0, m_N);
    } else {
        unsigned hw = std::thread::hardware_concurrency();
        if (hw == 0) hw = 4;
        int nThreads = (int)hw;
        std::vector<std::thread> pool;
        int slabsPer = (m_N + nThreads - 1) / nThreads;
        for (int t = 0; t < nThreads; ++t) {
            int zs = t * slabsPer;
            int ze = std::min(m_N, zs + slabsPer);
            if (zs >= ze) break;
            pool.emplace_back(stepSlab, m_N, std::cref(m_rule), m_wrap,
                              m_next.data(), m_cur.data(), zs, ze);
        }
        for (auto& th : pool) th.join();
    }
    m_cur.swap(m_next);
}

StepResult Automaton::step(const AutomatonParams& p, Backend backend) {
    StepResult res;
    m_wrap = p.wrap;   // allow toggling wrap without a reseed

    if (backend == Backend::Cuda) {
#ifdef HAVE_CUDA
        double ms = 0.0;
        cudaStepAutomaton(m_N, m_rule, m_wrap, m_cur.data(), p.substeps, &ms);
        res.backendName  = "CUDA (GPU)";
        res.milliseconds = ms;   // CUDA-event time: upload + kernels + download
        return res;
#else
        backend = Backend::CpuMulti;   // graceful fallback
#endif
    }

    auto t0 = std::chrono::high_resolution_clock::now();
    bool multi = (backend == Backend::CpuMulti);
    for (int s = 0; s < p.substeps; ++s) stepCpu(multi);
    auto t1 = std::chrono::high_resolution_clock::now();

    res.backendName  = multi ? "CPU (multi-threaded)" : "CPU (single-threaded)";
    res.milliseconds = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return res;
}

void Automaton::collectInstances(std::vector<float>& out, int& count) const {
    out.clear();
    count = 0;
    float inv = (m_rule.maxState > 0) ? 1.0f / (float)m_rule.maxState : 1.0f;
    for (int z = 0; z < m_N; ++z)
        for (int y = 0; y < m_N; ++y)
            for (int x = 0; x < m_N; ++x) {
                uint8_t s = m_cur[(z * m_N + y) * m_N + x];
                if (s == 0) continue;
                out.push_back((float)x);
                out.push_back((float)y);
                out.push_back((float)z);
                out.push_back((float)s * inv);
                ++count;
            }
}

void Automaton::saveState()    { m_saved = m_cur; }
void Automaton::restoreState() { if (!m_saved.empty()) m_cur = m_saved; }

bool Automaton::cudaSupported() {
#ifdef HAVE_CUDA
    return cudaAvailable();
#else
    return false;
#endif
}

void Automaton::shutdown() {
#ifdef HAVE_CUDA
    cudaShutdown();
#endif
}

QString Automaton::backendName(Backend b) {
    switch (b) {
        case Backend::CpuSingle: return "CPU (single-threaded)";
        case Backend::CpuMulti:  return "CPU (multi-threaded)";
        case Backend::Cuda:      return "CUDA (GPU)";
    }
    return "unknown";
}
