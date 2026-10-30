#include "ChronoHsc.h"
#ifdef BAGUA_HAS_CHRONO
#include <stdexcept>
namespace bagua {
double ChronoHsc::Solve(chrono::ChSystemDescriptor &descriptor) {
    if (descriptor.CountActiveConstraints())
        throw std::runtime_error("HSC supports unconstrained SPD SMC/FEM systems; exact bilateral/NSC constraints "
                                 "require a different solver");
    chrono::ChSparseMatrix matrix;
    chrono::ChVectorDynamic<> rhs;
    descriptor.BuildSystemMatrix(&matrix, &rhs, conditioning_factor);
    std::vector<int> permutation, rigidBlocks;
    for (auto *v : descriptor.GetVariables())
        if (v->IsActive() && v->GetDOF() == 3)
            for (unsigned k = 0; k < 3; ++k)
                permutation.push_back(int(v->GetOffset() + k));
    int nf = int(permutation.size());
    for (auto *v : descriptor.GetVariables())
        if (v->IsActive() && v->GetDOF() != 3) {
            if (v->GetDOF() != 6 && v->GetDOF() != 12)
                throw std::runtime_error("Unsupported HSC variable block");
            rigidBlocks.push_back(int(v->GetDOF()));
            for (unsigned k = 0; k < v->GetDOF(); ++k)
                permutation.push_back(int(v->GetOffset() + k));
        }
    int n = int(rhs.size());
    if (n != int(permutation.size()))
        throw std::runtime_error("HSC variable partition mismatch");
    std::vector<double> h(size_t(n) * n), b(size_t(n), 0.), x; // evaluated once per Chrono solve
    for (int i = 0; i < n; ++i) {
        b[size_t(i)] = rhs[permutation[size_t(i)]];
        for (int j = 0; j < n; ++j)
            h[size_t(i) * n + j] = matrix.coeff(permutation[size_t(i)], permutation[size_t(j)]);
    }
    HscStats result;
    QString error;
    if (!solveHsc(h, nf, rigidBlocks, b, options, x, result, &error))
        throw std::runtime_error(error.toStdString());
    result.calls = stats.calls + 1;
    stats = result;
    chrono::ChVectorDynamic<> solution(n);
    for (int i = 0; i < n; ++i)
        solution[permutation[size_t(i)]] = x[size_t(i)];
    descriptor.FromVectorToUnknowns(solution, conditioning_factor);
    return result.relativeResidual;
}
} // namespace bagua
#endif
