#pragma once
#include "HscTypes.h"
#include <QString>
#include <vector>
namespace bagua {
// Row-major SPD block system [D E^T; E A], deformable DOFs first.
// CPU reference implementation; supports 3-DOF FEM and arbitrary rigid/affine block sizes.
bool solveHsc(const std::vector<double> &matrix, int deformableDofs, const std::vector<int> &rigidBlocks,
              const std::vector<double> &rhs, const HscOptions &options, std::vector<double> &solution, HscStats &stats,
              QString *error = nullptr);
} // namespace bagua
