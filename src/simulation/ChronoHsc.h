#pragma once
#include "Hsc.h"
#ifdef BAGUA_HAS_CHRONO
#include "chrono/solver/ChSolver.h"
namespace bagua {
class ChronoHsc final : public chrono::ChSolver {
public:
    explicit ChronoHsc(HscOptions options) : options(options) {}
    bool IsIterative() const override { return true; }
    bool IsDirect() const override { return false; }
    bool SolveRequiresMatrix() const override { return true; }
    double Solve(chrono::ChSystemDescriptor &descriptor) override;
    HscStats stats;

private:
    HscOptions options;
};
} // namespace bagua
#endif
