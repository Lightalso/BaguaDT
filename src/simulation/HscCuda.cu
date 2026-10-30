// Keep CCCL template kernels private to this solver; the SDK embeds its own copy.
#define THRUST_CUB_WRAPPED_NAMESPACE bagua_hsc_cccl
#include "HscCuda.h"
#include "HscCudaDevice.h"
#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cuda_runtime.h>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>
#include <thrust/binary_search.h>
#include <thrust/copy.h>
#include <thrust/device_vector.h>
#include <thrust/fill.h>
#include <thrust/inner_product.h>
#include <thrust/iterator/counting_iterator.h>
#include <thrust/reduce.h>
#include <thrust/sort.h>
#include <thrust/transform.h>
#include <thrust/transform_reduce.h>
namespace bagua {
namespace thrust = ::bagua_hsc_cccl::thrust;
namespace {
using DV = thrust::device_vector<double>;
using DI = thrust::device_vector<int>;
using Key = unsigned long long;
using DK = thrust::device_vector<Key>;
template <class T> T *ptr(thrust::device_vector<T> &v) { return thrust::raw_pointer_cast(v.data()); }
template <class T> const T *ptr(const thrust::device_vector<T> &v) { return thrust::raw_pointer_cast(v.data()); }
void check(cudaError_t result) {
    if (result != cudaSuccess)
        throw std::runtime_error(cudaGetErrorString(result));
}
void require(bool valid, const char *message) {
    if (!valid)
        throw std::runtime_error(message);
}
int grid(size_t n) { return int((n + 255) / 256); }
struct HostCsr {
    int n = 0, m = 0;
    std::vector<int> rows, columns;
    std::vector<double> values;
    HostCsr(int nr, int nc) : n(nr), m(nc), rows(size_t(nr) + 1, 0) {}
};
struct Csr {
    int n, m;
    DI rows, columns;
    DV values;
    explicit Csr(const HostCsr &h) : n(h.n), m(h.m), rows(h.rows), columns(h.columns), values(h.values) {}
    Csr(int nr, int nc, DI r, DI c, DV v)
        : n(nr), m(nc), rows(std::move(r)), columns(std::move(c)), values(std::move(v)) {}
};
__global__ void multiply(int n, const int *rows, const int *columns, const double *values, const double *x, double *y) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) {
        double sum = 0;
        for (int j = rows[i]; j < rows[i + 1]; ++j)
            sum += values[j] * x[columns[j]];
        y[i] = sum;
    }
}
__global__ void multiplyWarp(int n, const int *rows, const int *columns, const double *values, const double *x,
                             double *y) {
    int lane = threadIdx.x & 31, row = (blockIdx.x * blockDim.x + threadIdx.x) / 32;
    if (row >= n)
        return;
    double sum = 0;
    for (int j = rows[row] + lane; j < rows[row + 1]; j += 32)
        sum += values[j] * x[columns[j]];
    for (int offset = 16; offset; offset /= 2)
        sum += __shfl_down_sync(0xffffffff, sum, offset);
    if (lane == 0)
        y[row] = sum;
}
void product(const Csr &a, const DV &x, DV &y) {
    y.resize(size_t(a.n));
    if (a.n >= 1024)
        multiplyWarp<<<grid(size_t(a.n) * 32), 256>>>(a.n, ptr(a.rows), ptr(a.columns), ptr(a.values), ptr(x), ptr(y));
    else if (a.n)
        multiply<<<grid(a.n), 256>>>(a.n, ptr(a.rows), ptr(a.columns), ptr(a.values), ptr(x), ptr(y));
}
__global__ void combine(int n, double *y, const double *x, double a, double b) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n)
        y[i] = a * x[i] + b * y[i];
}
void axpby(DV &y, const DV &x, double a, double b) {
    if (!y.empty())
        combine<<<grid(y.size()), 256>>>(int(y.size()), ptr(y), ptr(x), a, b);
}
double dot(const DV &a, const DV &b) { return thrust::inner_product(a.begin(), a.end(), b.begin(), 0.); }
double norm(const DV &a) { return std::sqrt(dot(a, a)); }
__global__ void invertBlocks(int nb, const int *offsets, const int *sizes, const int *outOffsets, const int *rows,
                             const int *columns, const double *values, double *inverse, int *invalid) {
    int block = blockIdx.x;
    if (threadIdx.x || block >= nb)
        return;
    int start = offsets[block], n = sizes[block], out = outOffsets[block];
    double l[144] = {0}, b[144] = {0};
    for (int i = 0; i < n; ++i)
        for (int q = rows[start + i]; q < rows[start + i + 1]; ++q)
            if (columns[q] >= start && columns[q] < start + n)
                b[i * 12 + columns[q] - start] = values[q];
    for (int i = 0; i < n; ++i)
        for (int j = 0; j <= i; ++j) {
            double v = b[i * 12 + j];
            for (int k = 0; k < j; ++k)
                v -= l[i * 12 + k] * l[j * 12 + k];
            if (i == j) {
                if (!(v > 0) || !isfinite(v)) {
                    atomicAdd(invalid, 1);
                    return;
                }
                l[i * 12 + j] = sqrt(v);
            } else
                l[i * 12 + j] = v / l[j * 12 + j];
        }
    for (int col = 0; col < n; ++col) {
        double x[12] = {0};
        for (int i = 0; i < n; ++i) {
            double v = i == col ? 1 : 0;
            for (int j = 0; j < i; ++j)
                v -= l[i * 12 + j] * x[j];
            x[i] = v / l[i * 12 + i];
        }
        for (int i = n - 1; i >= 0; --i) {
            double v = x[i];
            for (int j = i + 1; j < n; ++j)
                v -= l[j * 12 + i] * x[j];
            x[i] = v / l[i * 12 + i];
        }
        for (int i = 0; i < n; ++i)
            inverse[out + i * n + col] = x[i];
    }
}
__global__ void applyBlocks(int nb, const int *offsets, const int *sizes, const int *invOffsets, const double *inverse,
                            const double *x, double *y, double omega) {
    int block = blockIdx.x, row = threadIdx.x;
    if (block < nb && row < sizes[block]) {
        int n = sizes[block], start = offsets[block], off = invOffsets[block];
        double sum = 0;
        for (int k = 0; k < n; ++k)
            sum += inverse[off + row * n + k] * x[start + k];
        y[start + row] = omega * sum;
    }
}
// FEM blocks are uniformly 3x3. Pack adjacent vertices into a launch instead of
// assigning a mostly idle warp to each vertex.
__global__ void applyTriples(int n, const double *inverse, const double *x, double *y, double omega) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n)
        return;
    int base = i / 3 * 3, off = i / 3 * 9 + (i % 3) * 3;
    y[i] = omega * (inverse[off] * x[base] + inverse[off + 1] * x[base + 1] + inverse[off + 2] * x[base + 2]);
}
__global__ void accumulateTriples(int n, const double *inverse, const double *work, double *term, double *y,
                                  double omega) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n)
        return;
    int base = i / 3 * 3, off = i / 3 * 9 + (i % 3) * 3;
    double next = term[i] - omega * (inverse[off] * work[base] + inverse[off + 1] * work[base + 1] +
                                     inverse[off + 2] * work[base + 2]);
    term[i] = next;
    y[i] += next;
}
struct BlockInverse {
    int count;
    bool triples;
    DI offsets, sizes, invOffsets;
    DV inverse;
    BlockInverse(const Csr &a, const std::vector<int> &blocks)
        : count(int(blocks.size())), triples(std::all_of(blocks.begin(), blocks.end(), [](int n) { return n == 3; })),
          sizes(blocks) {
        std::vector<int> off, ioff;
        int start = 0, packed = 0;
        for (int n : blocks) {
            require(n > 0 && n <= 12, "GPU HSC blocks must contain 1-12 DOFs");
            off.push_back(start);
            ioff.push_back(packed);
            start += n;
            packed += n * n;
        }
        require(start == a.n, "GPU HSC block partition does not cover its subspace");
        offsets = off;
        invOffsets = ioff;
        inverse.resize(size_t(packed));
        DI invalid(1, 0);
        if (count)
            invertBlocks<<<count, 1>>>(count, ptr(offsets), ptr(sizes), ptr(invOffsets), ptr(a.rows), ptr(a.columns),
                                       ptr(a.values), ptr(inverse), ptr(invalid));
        require(int(invalid[0]) == 0, "GPU HSC diagonal block is not positive definite");
    }
    void apply(const DV &x, DV &y, double omega) const {
        y.resize(x.size());
        if (count && triples)
            applyTriples<<<grid(x.size()), 256>>>(int(x.size()), ptr(inverse), ptr(x), ptr(y), omega);
        else if (count)
            applyBlocks<<<count, 32>>>(count, ptr(offsets), ptr(sizes), ptr(invOffsets), ptr(inverse), ptr(x), ptr(y),
                                       omega);
    }
};
struct PolynomialWorkspace {
    DV term, work, scaled;
};
void polynomial(const Csr &a, const BlockInverse &b, const HscOptions &o, const DV &x, DV &y,
                PolynomialWorkspace &workspace) {
    auto &term = workspace.term, &work = workspace.work, &scaled = workspace.scaled;
    b.apply(x, term, o.omega);
    y = term;
    for (int k = 1; k < o.neumannTerms; ++k) {
        product(a, term, work);
        if (b.count && b.triples)
            accumulateTriples<<<grid(term.size()), 256>>>(int(term.size()), ptr(b.inverse), ptr(work), ptr(term),
                                                          ptr(y), o.omega);
        else {
            b.apply(work, scaled, o.omega);
            axpby(term, scaled, -1, 1);
            axpby(y, term, 1, 1);
        }
    }
}
void polynomial(const Csr &a, const BlockInverse &b, const HscOptions &o, const DV &x, DV &y) {
    PolynomialWorkspace workspace;
    polynomial(a, b, o, x, y, workspace);
}
struct Pivot {
    double value;
    Key key;
};
struct PivotAt {
    const Key *keys;
    const double *values;
    __host__ __device__ Pivot operator()(int i) const { return {values[i], keys[i]}; }
};
struct Maximum {
    __host__ __device__ Pivot operator()(Pivot a, Pivot b) const {
        double aa = fabs(a.value), bb = fabs(b.value);
        return bb > aa || (bb == aa && b.key < a.key) ? b : a;
    }
};
__global__ void cross(int count, int nf, const Key *keys, const double *values, Key pivot, double inverse, double *u,
                      double *v) {
    int q = blockIdx.x * blockDim.x + threadIdx.x;
    if (q >= count)
        return;
    int i = int(keys[q] / nf), j = int(keys[q] % nf), pi = int(pivot / nf), pj = int(pivot % nf);
    if (j == pj)
        u[i] = values[q];
    if (i == pi)
        v[j] = values[q] * inverse;
}
struct Nonzero {
    const double *values;
    __host__ __device__ bool operator()(int i) const { return values[i] != 0; }
};
struct Gather {
    const double *values;
    __host__ __device__ double operator()(int i) const { return values[i]; }
};
__global__ void outer(int count, int nv, int nf, const int *ui, const double *u, const int *vi, const double *v,
                      Key *keys, double *values) {
    int q = blockIdx.x * blockDim.x + threadIdx.x;
    if (q >= count)
        return;
    int i = q / nv, j = q % nv;
    keys[q] = Key(ui[i]) * nf + vi[j];
    values[q] = -u[i] * v[j];
}
// Factors are stored transposed in CSR: one sparse row per ACA-generated column.
struct Factors {
    DI offsets, indices;
    DV values;
    std::vector<int> hostOffsets{0};
    void append(const DV &column) {
        DI indicesNew(column.size());
        auto end =
            thrust::copy_if(thrust::counting_iterator<int>(0), thrust::counting_iterator<int>(int(column.size())),
                            indicesNew.begin(), Nonzero{ptr(column)});
        int count = int(end - indicesNew.begin()), old = int(indices.size());
        indices.resize(size_t(old) + count);
        values.resize(size_t(old) + count);
        thrust::copy(indicesNew.begin(), indicesNew.begin() + count, indices.begin() + old);
        thrust::transform(indicesNew.begin(), indicesNew.begin() + count, values.begin() + old, Gather{ptr(column)});
        hostOffsets.push_back(old + count);
    }
    void finish() { offsets = hostOffsets; }
    int rank() const { return int(hostOffsets.size()) - 1; }
};
__global__ void factorDot(int rank, const int *offsets, const int *ids, const double *values, const double *x,
                          double *coeff) {
    int k = blockIdx.x;
    __shared__ double sum[256];
    double value = 0;
    if (k < rank)
        for (int i = offsets[k] + threadIdx.x; i < offsets[k + 1]; i += blockDim.x)
            value += values[i] * x[ids[i]];
    sum[threadIdx.x] = value;
    __syncthreads();
    for (int step = 128; step; step /= 2) {
        if (threadIdx.x < step)
            sum[threadIdx.x] += sum[threadIdx.x + step];
        __syncthreads();
    }
    if (!threadIdx.x && k < rank)
        coeff[k] = sum[0];
}
__global__ void factorAdd(int rank, const int *offsets, const int *ids, const double *values, const double *coeff,
                          double scale, double *x) {
    int k = blockIdx.x;
    if (k >= rank)
        return;
    for (int i = offsets[k] + threadIdx.x; i < offsets[k + 1]; i += blockDim.x)
        atomicAdd(x + ids[i], scale * values[i] * coeff[k]);
}
__global__ void factorColumn(int k, const int *offsets, const int *ids, const double *values, double *x) {
    int q = offsets[k] + blockIdx.x * blockDim.x + threadIdx.x;
    if (q < offsets[k + 1])
        x[ids[q]] = values[q];
}
void project(const Factors &f, const DV &x, DV &coeff) {
    coeff.resize(size_t(f.rank()));
    if (f.rank())
        factorDot<<<f.rank(), 256>>>(f.rank(), ptr(f.offsets), ptr(f.indices), ptr(f.values), ptr(x), ptr(coeff));
}
void addFactor(const Factors &f, const DV &coeff, DV &x, double scale) {
    if (f.rank())
        factorAdd<<<f.rank(), 256>>>(f.rank(), ptr(f.offsets), ptr(f.indices), ptr(f.values), ptr(coeff), scale,
                                     ptr(x));
}
void column(const Factors &f, int k, int n, DV &x) {
    x.assign(size_t(n), 0);
    int count = f.hostOffsets[size_t(k) + 1] - f.hostOffsets[size_t(k)];
    if (count)
        factorColumn<<<grid(count), 256>>>(k, ptr(f.offsets), ptr(f.indices), ptr(f.values), ptr(x));
}
void aca(int en, int em, DK keys, DV values, const HscOptions &o, Factors &u, Factors &v, HscStats &stats) {
    DV uc(size_t(en), 0.), vc(size_t(em), 0.);
    double scale = norm(values);
    for (int k = 0; k < std::min({o.maxRank, en, em}) && !values.empty() && scale > 0; ++k) {
        require(values.size() <= INT_MAX, "GPU ACA COO index count exceeds int32 capacity");
        Pivot p = thrust::transform_reduce(thrust::counting_iterator<int>(0),
                                           thrust::counting_iterator<int>(int(values.size())),
                                           PivotAt{ptr(keys), ptr(values)}, Pivot{0, ~Key(0)}, Maximum{});
        if (std::abs(p.value) <= std::numeric_limits<double>::epsilon() * scale)
            break;
        thrust::fill(uc.begin(), uc.end(), 0.);
        thrust::fill(vc.begin(), vc.end(), 0.);
        cross<<<grid(values.size()), 256>>>(int(values.size()), em, ptr(keys), ptr(values), p.key, 1 / p.value, ptr(uc),
                                            ptr(vc));
        u.append(uc);
        v.append(vc);
        int nu = u.hostOffsets.back() - u.hostOffsets[u.hostOffsets.size() - 2],
            nv = v.hostOffsets.back() - v.hostOffsets[v.hostOffsets.size() - 2];
        size_t count = size_t(nu) * nv, total = values.size() + count;
        require(total <= INT_MAX, "GPU ACA fill-in exceeds int32 COO capacity");
        size_t freeBytes = 0, totalBytes = 0;
        check(cudaMemGetInfo(&freeBytes, &totalBytes));
        require(total * (sizeof(Key) + sizeof(double)) * 3 < freeBytes,
                "GPU ACA residual fill-in exceeds available device memory");
        size_t old = values.size();
        keys.resize(total);
        values.resize(total);
        if (count)
            outer<<<grid(count), 256>>>(int(count), nv, em, ptr(u.indices) + u.hostOffsets[u.hostOffsets.size() - 2],
                                        ptr(u.values) + u.hostOffsets[u.hostOffsets.size() - 2],
                                        ptr(v.indices) + v.hostOffsets[v.hostOffsets.size() - 2],
                                        ptr(v.values) + v.hostOffsets[v.hostOffsets.size() - 2], ptr(keys) + old,
                                        ptr(values) + old);
        // Supplemental Algorithm 1 lines 9-11: concatenate COO, sort, reduce duplicates.
        thrust::sort_by_key(keys.begin(), keys.end(), values.begin());
        DK reducedKeys(total);
        DV reducedValues(total);
        auto ends =
            thrust::reduce_by_key(keys.begin(), keys.end(), values.begin(), reducedKeys.begin(), reducedValues.begin());
        size_t reduced = size_t(ends.first - reducedKeys.begin());
        reducedKeys.resize(reduced);
        reducedValues.resize(reduced);
        keys.swap(reducedKeys);
        values.swap(reducedValues);
    }
    u.finish();
    v.finish();
    stats.rank = u.rank();
    stats.factorNonzeros = int(u.values.size() + v.values.size());
    stats.acaResidual = scale ? norm(values) / scale : 0;
}
__global__ void denseProduct(int n, int m, const double *a, const double *x, double *y) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) {
        double sum = 0;
        for (int j = 0; j < m; ++j)
            sum += a[size_t(i) * m + j] * x[j];
        y[i] = sum;
    }
}
__global__ void putColumn(int n, int col, const double *x, double *a) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n)
        a[size_t(i) * n + col] = x[i];
}
__global__ void csrDense(int n, const int *rows, const int *cols, const double *values, const double *x, double *y) {
    int i = blockIdx.y * 16 + threadIdx.y, j = blockIdx.x * 16 + threadIdx.x;
    if (i >= n || j >= n)
        return;
    double sum = 0;
    for (int q = rows[i]; q < rows[i + 1]; ++q)
        sum += values[q] * x[size_t(cols[q]) * n + j];
    y[size_t(i) * n + j] = sum;
}
__global__ void identityMinus(int n, double *x) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n * n)
        x[i] = (i / n == i % n ? 1 : 0) - x[i];
}
__global__ void gemm(int n, const double *a, const double *b, double *c) {
    __shared__ double as[16][16], bs[16][16];
    int i = blockIdx.y * 16 + threadIdx.y, j = blockIdx.x * 16 + threadIdx.x;
    double sum = 0;
    for (int off = 0; off < n; off += 16) {
        as[threadIdx.y][threadIdx.x] = (i < n && off + threadIdx.x < n) ? a[size_t(i) * n + off + threadIdx.x] : 0;
        bs[threadIdx.y][threadIdx.x] = (j < n && off + threadIdx.y < n) ? b[size_t(off + threadIdx.y) * n + j] : 0;
        __syncthreads();
        for (int k = 0; k < 16; ++k)
            sum += as[threadIdx.y][k] * bs[k][threadIdx.x];
        __syncthreads();
    }
    if (i < n && j < n)
        c[size_t(i) * n + j] = sum;
}
__global__ void symmetric(int n, const double *a, double *b) {
    int q = blockIdx.x * blockDim.x + threadIdx.x;
    if (q < n * n)
        b[q] = (a[q] + a[size_t(q % n) * n + q / n]) * .5;
}
__global__ void seedDenseInverse(int n, const int *offsets, const int *sizes, const int *invOffsets,
                                 const double *inverse, double *out, double omega) {
    int block = blockIdx.x, q = threadIdx.x, s = sizes[block];
    if (q < s * s)
        out[size_t(offsets[block] + q / s) * n + offsets[block] + q % s] = omega * inverse[invOffsets[block] + q];
}
__global__ void accumulateDenseInverse(int n, const int *offsets, const int *sizes, const int *invOffsets,
                                       const double *inverse, const double *work, double *term, double *sum,
                                       double omega) {
    int block = blockIdx.y, row = threadIdx.y, col = blockIdx.x * 16 + threadIdx.x;
    if (row >= sizes[block] || col >= n)
        return;
    int start = offsets[block], s = sizes[block], off = invOffsets[block] + row * s;
    double value = 0;
    for (int k = 0; k < s; ++k)
        value += inverse[off + k] * work[size_t(start + k) * n + col];
    size_t index = size_t(start + row) * n + col;
    double next = term[index] - omega * value;
    term[index] = next;
    sum[index] += next;
}
struct AffineInverse {
    const Csr &a;
    const BlockInverse &blocks;
    const HscOptions &options;
    DV dense;
    AffineInverse(const Csr &matrix, const BlockInverse &b, const HscOptions &o, HscStats &stats)
        : a(matrix), blocks(b), options(o) {
        if (!o.adaptiveInverse || a.n == 0)
            return;
        size_t count = size_t(a.n) * a.n, freeBytes = 0, totalBytes = 0;
        check(cudaMemGetInfo(&freeBytes, &totalBytes));
        if (count > INT_MAX || count * 8 * 4 >= freeBytes) {
            stats.inverseFallbacks = 1;
            return;
        }
        dense.assign(count, 0.);
        DV ap(count), update(count), sym(count);
        seedDenseInverse<<<blocks.count, 256>>>(a.n, ptr(blocks.offsets), ptr(blocks.sizes), ptr(blocks.invOffsets),
                                                ptr(blocks.inverse), ptr(dense), o.omega);
        dim3 dim((a.n + 15) / 16, (a.n + 15) / 16);
        // The same block-Neumann polynomial applied to every basis column in a
        // batch; avoids thousands of tiny launches for the small ABD subspace.
        DV term = dense;
        for (int k = 1; k < o.neumannTerms; ++k) {
            csrDense<<<dim, dim3(16, 16)>>>(a.n, ptr(a.rows), ptr(a.columns), ptr(a.values), ptr(term), ptr(ap));
            accumulateDenseInverse<<<dim3((a.n + 15) / 16, blocks.count), dim3(16, 12)>>>(
                a.n, ptr(blocks.offsets), ptr(blocks.sizes), ptr(blocks.invOffsets), ptr(blocks.inverse), ptr(ap),
                ptr(term), ptr(dense), o.omega);
        }
        for (;;) {
            csrDense<<<dim, dim3(16, 16)>>>(a.n, ptr(a.rows), ptr(a.columns), ptr(a.values), ptr(dense), ptr(ap));
            identityMinus<<<grid(count), 256>>>(a.n, ptr(ap));
            double residual = norm(ap) / std::sqrt(double(a.n));
            if (std::isfinite(residual) && residual <= 1e-5)
                break;
            if (!std::isfinite(residual) || residual > 1e100) {
                dense.clear();
                stats.inverseFallbacks = 1;
                break;
            }
            // The adaptive extension is an optional enhancement over the paper's fixed-order
            // Neumann polynomial. Free bodies held only by soft links can make the ABD block too
            // ill-conditioned for it, so fall back to the fixed polynomial instead of failing.
            if (stats.inverseDoublings >= 48) {
                dense.clear();
                stats.inverseFallbacks = 1;
                break;
            }
            gemm<<<dim, dim3(16, 16)>>>(a.n, ptr(dense), ptr(ap), ptr(update));
            axpby(dense, update, 1, 1);
            symmetric<<<grid(count), 256>>>(a.n, ptr(dense), ptr(sym));
            dense.swap(sym);
            ++stats.inverseDoublings;
        }
    }
    void apply(const DV &x, DV &y) const {
        if (dense.empty())
            polynomial(a, blocks, options, x, y);
        else {
            y.resize(size_t(a.n));
            denseProduct<<<grid(a.n), 256>>>(a.n, a.n, ptr(dense), ptr(x), ptr(y));
        }
    }
};
void append(HostCsr &a, int row, int col, double value) {
    if (value) {
        ++a.rows[size_t(row) + 1];
        a.columns.push_back(col);
        a.values.push_back(value);
    }
}
void finish(HostCsr &a) { std::partial_sum(a.rows.begin(), a.rows.end(), a.rows.begin()); }
DV solveCore(const Csr &originalGpu, const Csr &dg, const Csr &ag, DK eKeys, DV eValues, int nf, int nq,
             const std::vector<int> &rigidBlocks, const DV &rightHandSide, const HscOptions &o, HscStats &next) {
    auto phaseStart = std::chrono::steady_clock::now();
    const bool timings = std::getenv("BAGUA_HSC_TIMINGS") != nullptr;
    auto mark = [&](const char *phase) {
        if (timings) {
            check(cudaDeviceSynchronize());
            auto now = std::chrono::steady_clock::now();
            std::fprintf(stderr, "HSC %s %.3f ms\n", phase,
                         std::chrono::duration<double, std::milli>(now - phaseStart).count());
            phaseStart = now;
        }
    };
    int n = nf + nq;
    BlockInverse di(dg, std::vector<int>(size_t(nf / 3), 3)), ai(ag, rigidBlocks);
    AffineInverse pa(ag, ai, o, next);
    mark("inverse");
    Factors u, v;
    aca(nq, nf, std::move(eKeys), std::move(eValues), o, u, v, next);
    mark("ACA");
    int rank = next.rank;
    DV c(size_t(rank) * rank), tempq, coeff, tempcoeff, tempf, uc;
    for (int j = 0; j < rank; ++j) {
        column(u, j, nq, uc);
        pa.apply(uc, tempq);
        project(u, tempq, coeff);
        putColumn<<<grid(rank), 256>>>(rank, j, ptr(coeff), ptr(c));
    }
    if (rank) {
        DV symmetricC(c.size());
        symmetric<<<grid(c.size()), 256>>>(rank, ptr(c), ptr(symmetricC));
        c.swap(symmetricC);
    }
    auto schur = [&](const DV &x, DV &y) {
        product(dg, x, y);
        if (!rank)
            return;
        project(v, x, coeff);
        tempcoeff.resize(size_t(rank));
        denseProduct<<<grid(rank), 256>>>(rank, rank, ptr(c), ptr(coeff), ptr(tempcoeff));
        addFactor(v, tempcoeff, y, -1);
    };
    mark("Schur coefficients");
    DV b = rightHandSide;
    DV x(size_t(n), 0.), r = b, applied, df, dq, rf(size_t(nf), 0.), rq(size_t(nq), 0.), direction(size_t(n), 0.);
    double normb = norm(b);
    next.relativeResidual = normb ? 1 : 0;
    for (int correction = 0; normb && correction < (o.residualCorrection ? o.maxCorrections : 1); ++correction) {
        product(originalGpu, x, applied);
        r = b;
        axpby(r, applied, -1, 1);
        thrust::copy(r.begin(), r.begin() + nf, rf.begin());
        thrust::copy(r.begin() + nf, r.end(), rq.begin());
        pa.apply(rq, tempq);
        project(u, tempq, coeff);
        DV right = rf;
        addFactor(v, coeff, right, -1);
        df.assign(size_t(nf), 0.);
        DV cr = right, z, p, ap;
        double rightNorm = norm(right);
        if (rightNorm) {
            PolynomialWorkspace femWorkspace;
            polynomial(dg, di, o, cr, z, femWorkspace);
            p = z;
            double rz = dot(cr, z);
            bool converged = false;
            for (int iter = 0; iter < o.maxIterations; ++iter) {
                schur(p, ap);
                double denominator = dot(p, ap);
                require(denominator > 0 && rz > 0 && std::isfinite(denominator) && std::isfinite(rz),
                        "GPU Schur PCG encountered a non-SPD operator");
                axpby(df, p, rz / denominator, 1);
                axpby(cr, ap, -rz / denominator, 1);
                ++next.iterations;
                const double target = o.residualCorrection ? std::min(1e-5, o.tolerance * .1) : o.tolerance;
                bool candidate = norm(cr) / rightNorm <= target, restart = false;
                // Use PCG's residual recurrence between reliable updates. Always
                // confirm convergence against the actual Schur operator.
                if (candidate || (iter + 1) % 256 == 0) {
                    schur(df, ap);
                    cr = right;
                    axpby(cr, ap, -1, 1);
                    if (norm(cr) / rightNorm <= target) {
                        converged = true;
                        break;
                    }
                    restart = candidate;
                }
                polynomial(dg, di, o, cr, z, femWorkspace);
                double rzNew = dot(cr, z);
                axpby(p, z, 1, restart ? 0 : rzNew / rz);
                rz = rzNew;
            }
            require(converged, "GPU Schur PCG iteration limit reached");
        }
        project(v, df, coeff);
        tempq = rq;
        addFactor(u, coeff, tempq, -1);
        pa.apply(tempq, dq);
        thrust::copy(df.begin(), df.end(), direction.begin());
        thrust::copy(dq.begin(), dq.end(), direction.begin() + nf);
        double alpha = 1;
        if (o.residualCorrection) {
            product(originalGpu, direction, applied);
            double numerator = dot(r, direction), denominator = dot(direction, applied);
            require(numerator > 0 && denominator > 0, "GPU HSC correction is not a descent direction");
            alpha = std::min(1., numerator / denominator);
        }
        axpby(x, direction, alpha, 1);
        product(originalGpu, x, applied);
        r = b;
        axpby(r, applied, -1, 1);
        next.relativeResidual = norm(r) / normb;
        ++next.corrections;
        require(std::isfinite(next.relativeResidual), "GPU HSC produced a non-finite residual");
        if (next.relativeResidual <= o.tolerance)
            break;
    }
    require(!o.residualCorrection || next.relativeResidual <= o.tolerance,
            "GPU HSC full-system residual correction limit reached; no fallback used");
    require(normb == 0 || dot(b, x) > 0, "GPU HSC Newton direction is not descent");
    check(cudaGetLastError());
    check(cudaDeviceSynchronize());
    mark("PCG and recovery");
    return x;
}
} // namespace
bool solveHscCuda(const HscCsr &input, const HscOptions &o, std::vector<double> &solution, HscStats &stats,
                  std::string &error) {
    auto started = std::chrono::steady_clock::now();
    HscStats next;
    next.cuda = true;
    try {
        int devices = 0;
        check(cudaGetDeviceCount(&devices));
        require(devices > 0, "CUDA HSC requires an NVIDIA GPU");
        int n = int(input.rhs.size()), nf = input.femDofs, nq = n - nf;
        require(nf >= 0 && nf % 3 == 0 && nq >= 0 && n > 0 && input.rows.size() == size_t(n) + 1 &&
                    input.rows.front() == 0 && input.rows.back() == int(input.values.size()) &&
                    input.values.size() == input.columns.size(),
                "Invalid GPU HSC CSR dimensions");
        require(o.maxRank > 0 && o.maxRank <= 4096 && o.neumannTerms > 0 && o.neumannTerms <= 32 &&
                    o.maxIterations > 0 && o.maxCorrections > 0 && std::isfinite(o.omega) && o.omega > 0 &&
                    o.omega <= 1 && std::isfinite(o.tolerance) && o.tolerance > 0 && o.tolerance < 1,
                "Invalid GPU HSC options");
        std::map<Key, double> entries;
        for (int i = 0; i < n; ++i) {
            require(input.rows[size_t(i)] >= 0 && input.rows[size_t(i)] <= input.rows[size_t(i) + 1],
                    "GPU HSC CSR row offsets must be monotone");
            require(std::isfinite(input.rhs[size_t(i)]), "Non-finite HSC RHS");
            for (int q = input.rows[size_t(i)]; q < input.rows[size_t(i) + 1]; ++q) {
                require(input.columns[size_t(q)] >= 0 && input.columns[size_t(q)] < n &&
                            std::isfinite(input.values[size_t(q)]),
                        "Invalid GPU HSC column/value");
                entries[Key(i) * n + input.columns[size_t(q)]] += input.values[size_t(q)];
            }
        }
        HostCsr original(n, n), d(nf, nf), a(nq, nq), e(nq, nf);
        double skew2 = 0, h2 = 0;
        for (const auto &entry : entries) {
            int i = int(entry.first / n), j = int(entry.first % n);
            append(original, i, j, entry.second);
            h2 += entry.second * entry.second;
            auto other = entries.find(Key(j) * n + i);
            double transpose = other == entries.end() ? 0 : other->second;
            skew2 += (entry.second - transpose) * (entry.second - transpose);
            if (other == entries.end())
                skew2 += entry.second * entry.second;
        }
        next.asymmetry = std::sqrt(skew2) / std::max(1., std::sqrt(h2));
        require(next.asymmetry <= 1e-4, "HSC requires a symmetric positive-definite system, not an NSC/KKT matrix");
        // Materialize the sparse symmetric union; never form an n*n matrix.
        std::map<Key, double> symmetricEntries;
        for (const auto &entry : entries) {
            int i = int(entry.first / n), j = int(entry.first % n);
            symmetricEntries[entry.first] += .5 * entry.second;
            symmetricEntries[Key(j) * n + i] += .5 * entry.second;
        }
        for (const auto &entry : symmetricEntries) {
            int i = int(entry.first / n), j = int(entry.first % n);
            if (i < nf && j < nf)
                append(d, i, j, entry.second);
            if (i >= nf && j >= nf)
                append(a, i - nf, j - nf, entry.second);
            if (i >= nf && j < nf)
                append(e, i - nf, j, entry.second);
        }
        finish(original);
        finish(d);
        finish(a);
        finish(e);
        next.nonzeros = int(original.values.size());
        std::vector<Key> ek;
        ek.reserve(e.values.size());
        for (int i = 0; i < e.n; ++i)
            for (int q = e.rows[size_t(i)]; q < e.rows[size_t(i) + 1]; ++q)
                ek.push_back(Key(i) * e.m + e.columns[size_t(q)]);
        auto x = solveCore(Csr(original), Csr(d), Csr(a), DK(ek), DV(e.values), nf, nq, input.rigidBlocks,
                           DV(input.rhs), o, next);
        check(cudaGetLastError());
        check(cudaDeviceSynchronize());
        std::vector<double> result(size_t(n), 0.);
        thrust::copy(x.begin(), x.end(), result.begin());
        solution = std::move(result);
        next.calls = 1;
        next.milliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        stats = next;
        return true;
    } catch (const std::exception &exception) {
        error = exception.what();
        return false;
    }
}
namespace {
__device__ int reordered(int i, int fo, int nf, int qo, int nq) {
    if (i >= fo && i < fo + nf)
        return i - fo;
    if (i >= qo && i < qo + nq)
        return nf + i - qo;
    return -1;
}
__global__ void expandBlocks(const HscDeviceBlock *blocks, int count, int n, int fo, int nf, int qo, int nq, Key *keys,
                             double *values, int *invalid) {
    int q = blockIdx.x * blockDim.x + threadIdx.x;
    if (q >= count * 18)
        return;
    int b = q / 18, k = q % 18, r = (k % 9) / 3, c = k % 3;
    auto block = blocks[b];
    bool transpose = k >= 9, diagonal = block.row == block.col;
    if (transpose && diagonal) {
        keys[q] = 0;
        values[q] = 0;
        return;
    }
    int i = reordered(block.row + r, fo, nf, qo, nq), j = reordered(block.col + c, fo, nf, qo, nq);
    double value = diagonal ? (block.value[r * 3 + c] + block.value[c * 3 + r]) * .5 : block.value[r * 3 + c];
    if (i < 0 || j < 0 || !isfinite(value)) {
        atomicAdd(invalid, 1);
        keys[q] = 0;
        values[q] = 0;
        return;
    }
    keys[q] = transpose ? Key(j) * n + i : Key(i) * n + j;
    values[q] = value;
}
struct RowOf {
    int m;
    __host__ __device__ int operator()(Key k) const { return int(k / m); }
};
struct ColOf {
    int m;
    __host__ __device__ int operator()(Key k) const { return int(k % m); }
};
Csr deviceCsr(int n, int m, const DK &keys, DV values) {
    DI rows(size_t(n) + 1, 0), columns(keys.size()), keyRows(keys.size());
    if (!keys.empty()) {
        thrust::transform(keys.begin(), keys.end(), keyRows.begin(), RowOf{m});
        thrust::transform(keys.begin(), keys.end(), columns.begin(), ColOf{m});
        thrust::lower_bound(keyRows.begin(), keyRows.end(), thrust::counting_iterator<int>(0),
                            thrust::counting_iterator<int>(n + 1), rows.begin());
    }
    return Csr(n, m, std::move(rows), std::move(columns), std::move(values));
}
struct InPartition {
    const Key *keys;
    int n, nf, type;
    __host__ __device__ bool operator()(int q) const {
        int i = int(keys[q] / n), j = int(keys[q] % n);
        return type == 0 ? (i < nf && j < nf) : type == 1 ? (i >= nf && j >= nf) : (i >= nf && j < nf);
    }
};
struct PartitionKey {
    const Key *keys;
    int n, nf, type;
    __host__ __device__ Key operator()(int q) const {
        int i = int(keys[q] / n), j = int(keys[q] % n);
        if (type == 0)
            return Key(i) * nf + j;
        if (type == 1)
            return Key(i - nf) * (n - nf) + j - nf;
        return Key(i - nf) * nf + j;
    }
};
void partition(const DK &keys, const DV &values, int n, int nf, int type, DK &partKeys, DV &partValues) {
    DI ids(keys.size());
    auto end = thrust::copy_if(thrust::counting_iterator<int>(0), thrust::counting_iterator<int>(int(keys.size())),
                               ids.begin(), InPartition{ptr(keys), n, nf, type});
    int count = int(end - ids.begin());
    partKeys.resize(size_t(count));
    partValues.resize(size_t(count));
    thrust::transform(ids.begin(), ids.begin() + count, partKeys.begin(), PartitionKey{ptr(keys), n, nf, type});
    thrust::transform(ids.begin(), ids.begin() + count, partValues.begin(), Gather{ptr(values)});
}
__global__ void permuteVector(int n, int fo, int nf, int qo, int nq, const double *input, double *out, bool inverse) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n)
        return;
    int j = reordered(i, fo, nf, qo, nq);
    if (j < 0)
        return;
    if (inverse)
        out[i] = input[j];
    else
        out[j] = input[i];
}
} // namespace
bool solveHscDeviceBlocks(const HscDeviceBlock *blocks, int blockCount, const double *rhs, double *solution, int n,
                          int fo, int nf, int qo, int nq, const HscOptions &o, HscStats &stats, std::string &error) {
    auto started = std::chrono::steady_clock::now();
    HscStats next;
    next.cuda = true;
    try {
        require(n > 0 && nf >= 0 && nf % 3 == 0 && nq >= 0 && nq % 12 == 0 && nf + nq == n && fo >= 0 && qo >= 0 &&
                    fo + nf <= n && qo + nq <= n && (!nf || !nq || fo + nf <= qo || qo + nq <= fo),
                "Invalid resident HSC ABD/FEM partition");
        require(blockCount > 0 && blockCount <= INT_MAX / 18 && blocks && rhs && solution,
                "Invalid resident HSC buffers");
        require(o.maxRank > 0 && o.neumannTerms > 0 && o.neumannTerms <= 32 && o.maxIterations > 0 &&
                    o.maxCorrections > 0 && std::isfinite(o.omega) && o.omega > 0 && o.omega <= 1 &&
                    std::isfinite(o.tolerance) && o.tolerance > 0 && o.tolerance < 1,
                "Invalid resident HSC options");
        size_t count = size_t(blockCount) * 18, freeBytes = 0, totalBytes = 0;
        check(cudaMemGetInfo(&freeBytes, &totalBytes));
        require(count * (sizeof(Key) + sizeof(double)) * 3 < freeBytes,
                "HSC sparse expansion exceeds available GPU memory");
        DK keys(count);
        DV values(count);
        DI invalid(1, 0);
        expandBlocks<<<grid(count), 256>>>(blocks, blockCount, n, fo, nf, qo, nq, ptr(keys), ptr(values), ptr(invalid));
        require(int(invalid[0]) == 0, "Native contact Hessian contains invalid DOFs or non-finite entries");
        thrust::sort_by_key(keys.begin(), keys.end(), values.begin());
        DK reducedKeys(count);
        DV reducedValues(count);
        auto ends =
            thrust::reduce_by_key(keys.begin(), keys.end(), values.begin(), reducedKeys.begin(), reducedValues.begin());
        size_t nnz = size_t(ends.first - reducedKeys.begin());
        reducedKeys.resize(nnz);
        reducedValues.resize(nnz);
        keys.swap(reducedKeys);
        values.swap(reducedValues);
        next.nonzeros = int(nnz);
        DK dk, ak, ek;
        DV dv, av, ev;
        partition(keys, values, n, nf, 0, dk, dv);
        partition(keys, values, n, nf, 1, ak, av);
        partition(keys, values, n, nf, 2, ek, ev);
        Csr original = deviceCsr(n, n, keys, values), d = deviceCsr(nf, nf, dk, std::move(dv)),
            a = deviceCsr(nq, nq, ak, std::move(av));
        DV b(size_t(n), 0.);
        permuteVector<<<grid(n), 256>>>(n, fo, nf, qo, nq, rhs, ptr(b), false);
        auto x = solveCore(original, d, a, std::move(ek), std::move(ev), nf, nq, std::vector<int>(size_t(nq / 12), 12),
                           b, o, next);
        permuteVector<<<grid(n), 256>>>(n, fo, nf, qo, nq, ptr(x), solution, true);
        check(cudaGetLastError());
        check(cudaDeviceSynchronize());
        next.calls = 1;
        next.milliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        stats = next;
        return true;
    } catch (const std::exception &exception) {
        error = exception.what();
        return false;
    }
}
} // namespace bagua
