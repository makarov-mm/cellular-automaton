// CudaAutomaton.cu
// GPU back-end: one thread per cell advances the 3D cellular automaton, using
// the same rule logic as the CPU path (CARule.h) so results match exactly.

#include "CudaAutomaton.h"
#include <cuda_runtime.h>
#include <cstdio>
#include <algorithm>

// Report (but do not abort on) CUDA API errors: for a visual demo a printed
// diagnostic beats a silent hang or garbage state.
#define CA_CUDA_CHECK(call)                                                    \
    do {                                                                       \
        cudaError_t e_ = (call);                                               \
        if (e_ != cudaSuccess)                                                 \
            std::fprintf(stderr, "CUDA error: %s at %s:%d\n",                 \
                         cudaGetErrorString(e_), __FILE__, __LINE__);          \
    } while (0)

static uint8_t* g_a = nullptr;   // ping
static uint8_t* g_b = nullptr;   // pong
static int      g_cap = 0;

__global__ void caKernel(uint8_t* out, const uint8_t* in,
                         int N, CARule rule, bool wrap) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    int z = blockIdx.z * blockDim.z + threadIdx.z;
    if (x >= N || y >= N || z >= N) return;

    int idx = (z * N + y) * N + x;
    int c   = caCountAlive(in, x, y, z, N, rule.maxState, wrap);
    out[idx] = caNextState(in[idx], c, rule);
}

static void ensureCapacity(int count) {
    if (count <= g_cap) return;
    if (g_a) cudaFree(g_a);
    if (g_b) cudaFree(g_b);
    CA_CUDA_CHECK(cudaMalloc(&g_a, (size_t)count));
    CA_CUDA_CHECK(cudaMalloc(&g_b, (size_t)count));
    g_cap = count;
}

bool cudaAvailable() {
    int n = 0;
    if (cudaGetDeviceCount(&n) != cudaSuccess) return false;
    return n > 0;
}

void cudaStepAutomaton(int N, const CARule& rule, bool wrap,
                       uint8_t* grid, int substeps, double* elapsedMs) {
    int    count = N * N * N;
    size_t bytes = (size_t)count;
    ensureCapacity(count);

    dim3 block(8, 8, 8);
    dim3 gridDim((N + block.x - 1) / block.x,
                 (N + block.y - 1) / block.y,
                 (N + block.z - 1) / block.z);

    cudaEvent_t start, stop;
    cudaEventCreate(&start);
    cudaEventCreate(&stop);
    cudaEventRecord(start);

    uint8_t* d_in  = g_a;
    uint8_t* d_out = g_b;
    CA_CUDA_CHECK(cudaMemcpy(d_in, grid, bytes, cudaMemcpyHostToDevice));

    for (int s = 0; s < substeps; ++s) {
        caKernel<<<gridDim, block>>>(d_out, d_in, N, rule, wrap);
        std::swap(d_in, d_out);   // d_in now holds the newest state
    }
    CA_CUDA_CHECK(cudaGetLastError());   // catch launch/config errors

    CA_CUDA_CHECK(cudaMemcpy(grid, d_in, bytes, cudaMemcpyDeviceToHost));

    cudaEventRecord(stop);
    cudaEventSynchronize(stop);
    float ms = 0.0f;
    cudaEventElapsedTime(&ms, start, stop);
    *elapsedMs = (double)ms;

    cudaEventDestroy(start);
    cudaEventDestroy(stop);
}

void cudaShutdown() {
    if (g_a) cudaFree(g_a);
    if (g_b) cudaFree(g_b);
    g_a = g_b = nullptr;
    g_cap = 0;
}
