#pragma once
#include "Types.h"
#include "CARule.h"
#include <cstdint>

// Host-side interface to the CUDA back-end (implemented in CudaAutomaton.cu).
// Qt-free so it can be included from both host and device translation units.

bool cudaAvailable();

// Advances the grid by `substeps` steps on the GPU. `grid` is a host buffer of
// N*N*N uint8_t states: it is uploaded, iterated on the device, and written back.
// The elapsed GPU wall time is written to *elapsedMs.
void cudaStepAutomaton(int N, const CARule& rule, bool wrap,
                       uint8_t* grid, int substeps, double* elapsedMs);

void cudaShutdown();
