// GPU back-end: one thread per cell. Every thread counts the alive neighbors
// of its cell and applies the rule shared with the CPU path (CARule.h), so
// both produce the same grid.

#include "CudaAutomaton.h"

#include <cuda_runtime.h>

#include <chrono>
#include <utility>

namespace {

// Alive cells among the 26 neighbors of a cell that does not touch a grid
// face: every neighbor exists, so the offsets are fixed.
__device__ int countAliveInterior(const std::uint8_t* cell, int n, int alive) {
    int count = 0;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy) {
            const std::uint8_t* row = cell + (dz * n + dy) * n;
            count += (row[-1] == alive) + (row[0] == alive) + (row[1] == alive);
        }
    return count - (cell[0] == alive);
}

// Coordinate next to a face cell: wrapped around, or -1 if it lies outside a
// grid with dead edges.
__device__ int neighborCoordinate(int i, int n, bool wrap) {
    if (i >= 0 && i < n) return i;
    return wrap ? (i + n) % n : -1;
}

// The same count for a cell on a grid face, where neighbors either wrap
// around to the opposite face or are missing.
__device__ int countAliveOnFace(const std::uint8_t* grid, int x, int y, int z, int n, int alive,
                                bool wrap) {
    int count = 0;
    for (int dz = -1; dz <= 1; ++dz) {
        const int nz = neighborCoordinate(z + dz, n, wrap);
        if (nz < 0) continue;
        for (int dy = -1; dy <= 1; ++dy) {
            const int ny = neighborCoordinate(y + dy, n, wrap);
            if (ny < 0) continue;
            const std::uint8_t* row = grid + (nz * n + ny) * n;
            for (int dx = -1; dx <= 1; ++dx) {
                const int nx = neighborCoordinate(x + dx, n, wrap);
                if (nx >= 0) count += (row[nx] == alive);
            }
        }
    }
    return count - (grid[(z * n + y) * n + x] == alive);
}

__global__ void stepKernel(std::uint8_t* next, const std::uint8_t* current, int n, CARule rule,
                           bool wrap) {
    const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    const int z = static_cast<int>(blockIdx.z * blockDim.z + threadIdx.z);
    if (x >= n || y >= n || z >= n) return;

    const int index = (z * n + y) * n + x;
    const int alive = rule.aliveState();
    const bool interior = x > 0 && x < n - 1 && y > 0 && y < n - 1 && z > 0 && z < n - 1;
    const int neighbors = interior ? countAliveInterior(current + index, n, alive)
                                   : countAliveOnFace(current, x, y, z, n, alive, wrap);
    next[index] = caNextState(current[index], neighbors, rule);
}

}  // namespace

CudaAutomaton::~CudaAutomaton() {
    release();
}

bool CudaAutomaton::available() {
    int deviceCount = 0;
    return cudaGetDeviceCount(&deviceCount) == cudaSuccess && deviceCount > 0;
}

void CudaAutomaton::release() {
    if (m_current) cudaFree(m_current);
    if (m_next) cudaFree(m_next);
    m_current = m_next = nullptr;
    m_capacity = 0;
}

const char* CudaAutomaton::reserve(std::size_t bytes) {
    if (bytes <= m_capacity) return nullptr;

    release();
    cudaError_t status = cudaMalloc(&m_current, bytes);
    if (status == cudaSuccess) status = cudaMalloc(&m_next, bytes);
    if (status != cudaSuccess) {
        release();
        return cudaGetErrorString(status);
    }
    m_capacity = bytes;
    return nullptr;
}

CudaStepResult CudaAutomaton::step(std::uint8_t* cells, int n, const CARule& rule, bool wrap,
                                   int steps) {
    const std::size_t bytes = static_cast<std::size_t>(n) * n * n;
    if (const char* error = reserve(bytes)) return {.error = error};

    const dim3 block(8, 8, 8);
    const unsigned blocksPerAxis = (static_cast<unsigned>(n) + block.x - 1) / block.x;
    const dim3 blocks(blocksPerAxis, blocksPerAxis, blocksPerAxis);

    const auto start = std::chrono::steady_clock::now();

    cudaError_t status = cudaMemcpy(m_current, cells, bytes, cudaMemcpyHostToDevice);
    for (int i = 0; i < steps && status == cudaSuccess; ++i) {
        stepKernel<<<blocks, block>>>(m_next, m_current, n, rule, wrap);
        status = cudaGetLastError();  // launch errors
        std::swap(m_current, m_next);
    }
    // The download waits for the kernels, so it also reports their errors.
    if (status == cudaSuccess) status = cudaMemcpy(cells, m_current, bytes, cudaMemcpyDeviceToHost);
    if (status != cudaSuccess) return {.error = cudaGetErrorString(status)};

    const std::chrono::duration<double, std::milli> elapsed =
        std::chrono::steady_clock::now() - start;
    return {.milliseconds = elapsed.count()};
}
