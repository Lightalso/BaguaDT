#pragma once
#include "Scene.h"
#ifdef BAGUA_HAS_CHRONO
#include "chrono/fea/ChNodeFEAxyz.h"
#include "chrono/physics/ChSystem.h"
#include <array>
namespace bagua {
struct FemBinding {
    int objectIndex;
    Object visual;
    QMatrix4x4 restInverse;
    std::vector<std::shared_ptr<chrono::fea::ChNodeFEAxyz>> nodes;
    std::vector<std::array<int, 4>> tets;
    std::vector<double> restVolumes;
};
FemBinding makeFem(const Object &object, int index, chrono::ChSystem &system,
                   std::shared_ptr<chrono::ChContactMaterial> material);
Object updateFem(const FemBinding &binding, const Object &previous);
} // namespace bagua
#endif
