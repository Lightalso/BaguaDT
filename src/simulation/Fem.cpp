#include "Fem.h"
#ifdef BAGUA_HAS_CHRONO
#include "chrono/fea/ChContactSurfaceNodeCloud.h"
#include "chrono/fea/ChContinuumMaterial.h"
#include "chrono/fea/ChElementTetraCorot_4.h"
#include "chrono/fea/ChMesh.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <limits>
#include <stdexcept>
namespace bagua {
namespace {
double volume(const FemBinding &b, const std::array<int, 4> &ids) {
    auto a = b.nodes[size_t(ids[0])]->GetPos(), p = b.nodes[size_t(ids[1])]->GetPos(),
         q = b.nodes[size_t(ids[2])]->GetPos(), r = b.nodes[size_t(ids[3])]->GetPos();
    return chrono::Vdot(p - a, chrono::Vcross(q - a, r - a)) / 6;
}
} // namespace
FemBinding makeFem(const Object &o,int index,chrono::ChSystem &system,std::shared_ptr<chrono::ChContactMaterial> material) {
    const bool imported=o.shape==Shape::Mesh && o.mesh && !o.mesh->tetrahedra.isEmpty();
    if((!imported && o.shape!=Shape::Box) || o.femResolution<1 || o.femResolution>6 || !std::isfinite(o.youngModulus) || o.youngModulus<10 || o.youngModulus>1e12 || !std::isfinite(o.poissonRatio) || o.poissonRatio<0 || o.poissonRatio>=.49)
        throw std::runtime_error("FEM requires a box or tetrahedral asset with valid elastic parameters.");
    FemBinding binding;binding.objectIndex=index;binding.visual=o;binding.restInverse=o.matrix().inverted();
    auto mesh=chrono_types::make_shared<chrono::fea::ChMesh>();auto geometry=std::make_shared<MeshData>();
    geometry->id="fem-"+o.id;geometry->name=o.name;geometry->source="Chrono corotational tetrahedra";
    int n=o.femResolution;auto idx=[&](int x,int y,int z){return (z*(n+1)+y)*(n+1)+x;};
    std::vector<std::array<int,4>> tets;
    if(imported) {
        geometry->positions=o.mesh->positions;
        for(qsizetype i=0;i<o.mesh->tetrahedra.size();i+=4)tets.push_back({int(o.mesh->tetrahedra[i]),int(o.mesh->tetrahedra[i+1]),int(o.mesh->tetrahedra[i+2]),int(o.mesh->tetrahedra[i+3])});
    } else {
        for(int z=0;z<=n;++z)for(int y=0;y<=n;++y)for(int x=0;x<=n;++x)geometry->positions.push_back({float(x)/n-.5f,float(y)/n-.5f,float(z)/n-.5f});
        const int pattern[6][4]={{0,1,3,7},{0,3,2,7},{0,2,6,7},{0,6,4,7},{0,4,5,7},{0,5,1,7}};
        for(int z=0;z<n;++z)for(int y=0;y<n;++y)for(int x=0;x<n;++x) {
            int c[8]={idx(x,y,z),idx(x+1,y,z),idx(x,y+1,z),idx(x+1,y+1,z),idx(x,y,z+1),idx(x+1,y,z+1),idx(x,y+1,z+1),idx(x+1,y+1,z+1)};
            for(const auto &p:pattern)tets.push_back({c[p[0]],c[p[1]],c[p[2]],c[p[3]]});
        }
    }
    float minY=std::numeric_limits<float>::infinity();for(auto p:geometry->positions)minY=std::min(minY,p.y());
    for(auto p:geometry->positions) {
        auto world=o.matrix().map(p);auto node=chrono_types::make_shared<chrono::fea::ChNodeFEAxyz>(chrono::ChVector3d(world.x(),world.y(),world.z()));
        auto v=o.linearVelocity+QVector3D::crossProduct(o.angularVelocity*float(3.141592653589793/180.),world-o.position);node->SetPosDt({v.x(),v.y(),v.z()});
        node->SetFixed(o.fixed || (o.pinBottom && std::abs(p.y()-minY)<1e-6f));mesh->AddNode(node);binding.nodes.push_back(node);
    }
    double totalVolume=0;
    for(auto ids:tets) {
        for(int i:ids)if(i<0 || size_t(i)>=binding.nodes.size())throw std::runtime_error("Invalid FEM tetrahedron index");
        if(volume(binding,ids)<0)std::swap(ids[1],ids[2]);double rest=volume(binding,ids);
        if(!(rest>0) || !std::isfinite(rest))throw std::runtime_error("Degenerate FEM rest tetrahedron");
        binding.tets.push_back(ids);binding.restVolumes.push_back(rest);totalVolume+=rest;for(int i:ids)geometry->tetrahedra.push_back(quint32(i));
    }
    auto elastic=chrono_types::make_shared<chrono::fea::ChContinuumElastic>();elastic->SetYoungModulus(o.youngModulus);elastic->SetPoissonRatio(o.poissonRatio);elastic->SetDensity(o.mass/totalVolume);elastic->SetRayleighDampingBeta(.005);
    std::map<std::array<int,3>,std::pair<int,std::array<int,3>>> faces;
    for(auto ids:binding.tets) {
        auto element=chrono_types::make_shared<chrono::fea::ChElementTetraCorot_4>();element->SetNodes(binding.nodes[size_t(ids[0])],binding.nodes[size_t(ids[1])],binding.nodes[size_t(ids[2])],binding.nodes[size_t(ids[3])]);element->SetMaterial(elastic);mesh->AddElement(element);
        std::array<std::array<int,3>,4> fs{{{ids[1],ids[2],ids[3]},{ids[0],ids[3],ids[2]},{ids[0],ids[1],ids[3]},{ids[0],ids[2],ids[1]}}};
        for(auto face:fs){auto key=face;std::sort(key.begin(),key.end());auto &entry=faces[key];++entry.first;entry.second=face;}
    }
    for(const auto &entry:faces)if(entry.second.first==1)for(int i:entry.second.second)geometry->indices.push_back(quint32(i));
    auto d=o.dimensions*o.scale;
    if(o.collisionEnabled){auto contact=chrono_types::make_shared<chrono::fea::ChContactSurfaceNodeCloud>(material);contact->AddAllNodes(*mesh,double(std::min({d.x(),d.y(),d.z()}))/60);mesh->AddContactSurface(contact);}
    system.Add(mesh);binding.visual.shape=Shape::Mesh;binding.visual.mesh=geometry;return binding;
}
Object updateFem(const FemBinding &b, const Object &previous) {
    Object result = previous;
    auto geometry = std::make_shared<MeshData>(*previous.mesh);
    for (size_t i = 0; i < b.nodes.size(); ++i) {
        auto p = b.nodes[i]->GetPos();
        if (!std::isfinite(p.x()) || !std::isfinite(p.y()) || !std::isfinite(p.z()))
            throw std::runtime_error("Non-finite FEM node state");
        geometry->positions[qsizetype(i)] = b.restInverse.map({float(p.x()), float(p.y()), float(p.z())});
    }
    for (size_t i = 0; i < b.tets.size(); ++i)
        if (volume(b, b.tets[i]) <= 1e-6 * b.restVolumes[i])
            throw std::runtime_error(
                "FEM tetrahedron inverted; reduce step/load or use a nonlinear inversion-safe backend");
    result.mesh = geometry;
    return result;
}
} // namespace bagua
#endif
