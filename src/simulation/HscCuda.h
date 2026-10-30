#pragma once
#include "HscTypes.h"
#include <string>
#include <vector>
namespace bagua {
struct HscCsr {
    int femDofs = 0;
    std::vector<int> rigidBlocks;
    std::vector<int> rows, columns;
    std::vector<double> values, rhs;
};
// Full CSR, including both symmetric triangles. No Qt/Eigen dependencies in CUDA translation units.
bool solveHscCuda(const HscCsr &system, const HscOptions &options, std::vector<double> &solution,
                  HscStats &stats, std::string &error);
}
