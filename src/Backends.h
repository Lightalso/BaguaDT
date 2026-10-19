#pragma once
#include "Scene.h"
namespace bagua {
struct BackendReport {
    bool cudaCompiled = false, cudaAvailable = false, cudaPassed = false, chronoCompiled = false, chronoPassed = false;
    QString cuda, chrono;
    int bodies = 0;
};
BackendReport validateBackends(const Scene &scene);
} // namespace bagua
