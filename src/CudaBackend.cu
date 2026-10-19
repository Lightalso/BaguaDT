#include <cfloat>
#include <cstdio>
#include <cuda_runtime.h>
// A concrete CUDA path for scene validation; dynamics remain a future module.
__global__ void worldBounds(const float *matrices, int count, float *bounds) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= count)
        return;
    const float *m = matrices + 16 * i;
    float lo[3] = {FLT_MAX, FLT_MAX, FLT_MAX}, hi[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (int c = 0; c < 8; ++c) {
        float x = (c & 1) ? .5f : -.5f, y = (c & 2) ? .5f : -.5f, z = (c & 4) ? .5f : -.5f;
        for (int a = 0; a < 3; ++a) {
            float v = m[a] * x + m[4 + a] * y + m[8 + a] * z + m[12 + a];
            lo[a] = fminf(lo[a], v);
            hi[a] = fmaxf(hi[a], v);
        }
    }
    for (int a = 0; a < 3; ++a) {
        bounds[i * 6 + a] = lo[a];
        bounds[i * 6 + a + 3] = hi[a];
    }
}
extern "C" int baguaCudaBounds(const float *matrices, int count, float *bounds, char *info, int infoSize) {
    int devices = 0;
    cudaError_t error = cudaGetDeviceCount(&devices);
    if (error != cudaSuccess || devices == 0) {
        std::snprintf(info, infoSize, "CUDA unavailable: %s", cudaGetErrorString(error));
        return -1;
    }
    cudaDeviceProp prop{};
    error = cudaGetDeviceProperties(&prop, 0);
    if (error != cudaSuccess) {
        std::snprintf(info, infoSize, "CUDA: %s", cudaGetErrorString(error));
        return -1;
    }
    if (count == 0) {
        std::snprintf(info, infoSize, "CUDA: %s (empty scene)", prop.name);
        return 0;
    }
    float *dm = nullptr, *db = nullptr;
    error = cudaMalloc(&dm, size_t(count) * 16 * sizeof(float));
    if (error == cudaSuccess)
        error = cudaMalloc(&db, size_t(count) * 6 * sizeof(float));
    if (error == cudaSuccess)
        error = cudaMemcpy(dm, matrices, size_t(count) * 16 * sizeof(float), cudaMemcpyHostToDevice);
    if (error == cudaSuccess) {
        worldBounds<<<(count + 127) / 128, 128>>>(dm, count, db);
        error = cudaGetLastError();
    }
    if (error == cudaSuccess)
        error = cudaMemcpy(bounds, db, size_t(count) * 6 * sizeof(float), cudaMemcpyDeviceToHost);
    cudaFree(dm);
    cudaFree(db);
    if (error != cudaSuccess) {
        std::snprintf(info, infoSize, "CUDA kernel failed: %s", cudaGetErrorString(error));
        return 1;
    }
    std::snprintf(info, infoSize, "CUDA: %s / %d objects", prop.name, count);
    return 0;
}
