#pragma once
#include "HscTypes.h"
#include <string>
namespace bagua {
// Device memory only. Source is ONE triangle of full 3x3 blocks, matching libuipc's ge2sym convention.
struct HscDeviceBlock {int row,col;double value[9];};
bool solveHscDeviceBlocks(const HscDeviceBlock *blocks,int blockCount,const double *rhs,double *solution,
    int totalDofs,int femOffset,int femDofs,int affineOffset,int affineDofs,
    const HscOptions &options,HscStats &stats,std::string &error);
}
