// Stands in for CudaAutomaton.cu in builds without CUDA (USE_CUDA=OFF):
// no device is ever available and stepping reports an error.

#include "CudaAutomaton.h"

CudaAutomaton::~CudaAutomaton() = default;

bool CudaAutomaton::available() {
    return false;
}

CudaStepResult CudaAutomaton::step(std::uint8_t*, int, const CARule&, bool, int) {
    return {.error = "this build has no CUDA support"};
}
