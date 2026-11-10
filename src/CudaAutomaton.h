#pragma once

#include "CARule.h"

#include <cstddef>
#include <cstdint>

// Outcome of a batch of steps on the GPU.
struct CudaStepResult {
    double milliseconds = 0.0;    // upload + kernels + download
    const char* error = nullptr;  // null on success, otherwise what went wrong
};

// The GPU back-end. It owns the two device buffers the grid ping-pongs
// between and frees them when destroyed.
//
// Implemented in CudaAutomaton.cu, or by CudaAutomatonStub.cpp in builds
// without CUDA. This header is compiled by both the host compiler and nvcc,
// so it stays free of Qt and of CUDA types.
class CudaAutomaton {
public:
    CudaAutomaton() = default;
    ~CudaAutomaton();

    CudaAutomaton(const CudaAutomaton&) = delete;
    CudaAutomaton& operator=(const CudaAutomaton&) = delete;

    // True when the program was built with CUDA and a device is present.
    static bool available();

    // Advances the n*n*n grid in `cells` by `steps` steps: the host buffer is
    // uploaded, iterated on the device and written back. If an error is
    // reported the contents of `cells` have not been advanced.
    [[nodiscard]] CudaStepResult step(std::uint8_t* cells, int n, const CARule& rule, bool wrap,
                                      int steps);

private:
    [[nodiscard]] const char* reserve(std::size_t bytes);  // null on success
    void release();

    std::uint8_t* m_current = nullptr;  // device buffer holding the newest state
    std::uint8_t* m_next = nullptr;     // device buffer the next step is written to
    std::size_t m_capacity = 0;         // bytes allocated for each buffer
};
