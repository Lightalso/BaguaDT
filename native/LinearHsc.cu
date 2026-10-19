// Application-owned HSC plugin for the pinned libuipc CUDA backend.
#include "HscCudaDevice.h"
#include <affine_body/abd_linear_subsystem.h>
#include <collision_detection/simplex_trajectory_filter.h>
#include <collision_detection/vertex_half_plane_trajectory_filter.h>
#include <cuda_runtime.h>
#include <finite_element/fem_linear_subsystem.h>
#include <linear_system/iterative_solver.h>
#include <map>
#include <sim_engine.h>
#include <stdexcept>
namespace {
bagua::HscStats latest;
}
extern "C" __declspec(dllexport) const bagua::HscStats *bagua_hsc_stats() { return &latest; }
namespace uipc::backend::cuda {
namespace {
using Packed = bagua::HscDeviceBlock;
using MatrixView = cuda_tool::CBCOOMatrixView<Float, 3>;
__global__ void packHessian(MatrixView matrix, Packed *out) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= matrix.triplet_count())
        return;
    auto entry = matrix(i);
    out[i].row = 3 * entry.row_index;
    out[i].col = 3 * entry.col_index;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            out[i].value[r * 3 + c] = entry.value(r, c);
}
void checkCuda(cudaError_t code) {
    if (code != cudaSuccess)
        throw SimEngineException(cudaGetErrorString(code));
}
template <class T> struct Buffer {
    T *data = nullptr;
    size_t capacity = 0;
    ~Buffer() {
        if (data)
            cudaFree(data);
    }
    void resize(size_t n) {
        if (n <= capacity)
            return;
        if (data)
            checkCuda(cudaFree(data));
        data = nullptr;
        capacity = 0;
        checkCuda(cudaMalloc(reinterpret_cast<void **>(&data), n * sizeof(T)));
        capacity = n;
    }
};
} // namespace
class LinearHSC final : public IterativeSolver {
public:
    using IterativeSolver::IterativeSolver;

private:
    ABDLinearSubsystem *abd = nullptr;
    FEMLinearSubsystem *fem = nullptr;
    GlobalTrajectoryFilter *contacts = nullptr;
    Buffer<Packed> packed;
    bagua::HscOptions options;
    int calls = 0;
    void do_build(BuildInfo &) override {
        auto &config = world().scene().config();
        auto solver = config.find<std::string>("linear_system/solver");
        if (!solver || solver->view()[0] != "linear_hsc")
            throw SimSystemException("LinearHSC not selected");
        abd = find<ABDLinearSubsystem>();
        fem = find<FEMLinearSubsystem>();
        contacts = find<GlobalTrajectoryFilter>();
        options.maxRank = config.find<IndexT>("linear_system/hsc/max_rank")->view()[0];
        options.neumannTerms = config.find<IndexT>("linear_system/hsc/neumann_order")->view()[0];
        options.maxIterations = config.find<IndexT>("linear_system/hsc/max_iter")->view()[0];
        options.omega = config.find<Float>("linear_system/hsc/omega")->view()[0];
        options.tolerance = config.find<Float>("linear_system/tol_rate")->view()[0];
        options.adaptiveInverse = config.find<IndexT>("linear_system/hsc/adaptive_abd_inverse")->view()[0] != 0;
        options.residualCorrection = false; // inexact direction is refined by IPC's nonlinear Newton loop
        logger::info("BaguaDT HSC CUDA: sparse full-pivot COO ACA / CSR factors / 3-12 DOF Neumann / Schur PCG");
    }
    void do_solve(GlobalLinearSystem::SolvingInfo &info) override {
        int n = int(info.x().size()), qo = abd ? abd->dof_offset() : 0, nq = abd ? abd->dof_count() : 0,
            fo = fem ? fem->dof_offset() : 0, nf = fem ? fem->dof_count() : 0;
        if (!(nq + nf == n && nq % 12 == 0 && nf % 3 == 0))
            throw SimEngineException("HSC requires ABD/FEM energy DOFs; unsupported subsystem");
        if (n == 0)
            return;
        auto matrix = info.A();
        int nnz = matrix.triplet_count();
        packed.resize(size_t(nnz));
        if (nnz)
            packHessian<<<(nnz + 255) / 256, 256>>>(matrix, packed.data);
        static_assert(std::is_same_v<Float, double>, "HSC resident solver requires double-precision native buffers");
        bagua::HscStats stats;
        std::string error;
        if (!bagua::solveHscDeviceBlocks(packed.data, nnz, info.b().buffer_view().data(), info.x().buffer_view().data(),
                                         n, fo, nf, qo, nq, options, stats, error))
            throw SimEngineException("HSC CUDA solve failed: " + error);
        info.iter_count(stats.iterations);
        if (contacts) {
            auto sf = contacts->find<SimplexTrajectoryFilter>();
            auto gf = contacts->find<VertexHalfPlaneTrajectoryFilter>();
            if (sf)
                stats.contactCount += int(sf->PTs().size() + sf->EEs().size() + sf->PEs().size() + sf->PPs().size());
            if (gf)
                stats.contactCount += int(gf->PHs().size());
        }
        stats.calls = ++calls;
        latest = stats;
    }
};
REGISTER_SIM_SYSTEM(LinearHSC);
} // namespace uipc::backend::cuda
