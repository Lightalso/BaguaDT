#include <affine_body/inter_affine_body_constitution.h>
#include <utils/matrix_assembler.h>
#include <sim_engine.h>
#include "PaperRotationMath.h"
namespace uipc::backend::cuda {
namespace {
using M24=Matrix<Float,24,24>;
using V24=Vector<Float,24>;
__global__ void rotationEnergy(cuda_tool::CBufferView<Vector2i> ids,cuda_tool::CBufferView<Matrix3x3> alignment,
    cuda_tool::CBufferView<Matrix3x3> frozen,cuda_tool::CBufferView<Float> strength,
    cuda_tool::CBufferView<ABDJacobiDyadicMass> masses,cuda_tool::CBufferView<Vector12> qs,
    cuda_tool::BufferView<Float> energies,int count) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=count)return;auto pair=ids(i);auto qa=qs(pair[0]),qb=qs(pair[1]);
    double r[9],m[9];for(int a=0;a<3;++a)for(int b=0;b<3;++b){r[a*3+b]=frozen(i)(a,b);m[a*3+b]=alignment(i)(a,b);}
    double k=strength(i)*(masses(pair[0]).mass()+masses(pair[1]).mass());
    energies(i)=bagua::paperQuadraticRotation(qa.data(),qb.data(),r,m,k);
}
__global__ void rotationDerivatives(cuda_tool::CBufferView<Vector2i> ids,cuda_tool::CBufferView<Vector3> axes,
    cuda_tool::CBufferView<Matrix3x3> alignment,cuda_tool::BufferView<Matrix3x3> frozen,
    cuda_tool::CBufferView<Float> angles,cuda_tool::CBufferView<Float> angularVelocity,Float time,cuda_tool::CBufferView<Float> strength,
    cuda_tool::CBufferView<ABDJacobiDyadicMass> masses,cuda_tool::CBufferView<Vector12> qs,
    cuda_tool::DoubletVectorView<Float,12> gradients,cuda_tool::TripletMatrixView<Float,12> hessians,bool gradientOnly,int count) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=count)return;auto pair=ids(i);auto qa=qs(pair[0]),qb=qs(pair[1]);
    double r[9],m[9],gradient[24],hessian[576];
    if(!gradientOnly) {
        auto axis=axes(i);bagua::paperRodrigues(qa.data()+3,axis.data(),angles(i)+angularVelocity(i)*time,r);
        for(int a=0;a<3;++a)for(int b=0;b<3;++b)frozen(i)(a,b)=r[a*3+b];
    }
    for(int a=0;a<3;++a)for(int b=0;b<3;++b){r[a*3+b]=frozen(i)(a,b);m[a*3+b]=alignment(i)(a,b);}
    double k=strength(i)*(masses(pair[0]).mass()+masses(pair[1]).mass());
    bagua::paperQuadraticRotation(qa.data(),qb.data(),r,m,k,gradient,gradientOnly?nullptr:hessian);
    V24 g;for(int a=0;a<24;++a)g[a]=gradient[a];DoubletVectorAssembler{gradients}.segment<2>(2*i).write(pair,g);
    if(!gradientOnly){M24 h;for(int a=0;a<24;++a)for(int b=0;b<24;++b)h(a,b)=hessian[a*24+b];TripletMatrixAssembler{hessians}.half_block<2>(3*i).write(pair,h);}
}
}
class PaperRotation final:public InterAffineBodyConstitution {
public:using InterAffineBodyConstitution::InterAffineBodyConstitution;
private:
    cuda_tool::DeviceBuffer<Vector2i> bodyIds;
    cuda_tool::DeviceBuffer<Vector3> axes;
    cuda_tool::DeviceBuffer<Matrix3x3> alignment,frozen;
    cuda_tool::DeviceBuffer<Float> angles,angularVelocity,strength;
    U64 get_uid()const noexcept override{return 10011;}
    void do_build(BuildInfo &)override{require<AffineBodyDynamics>();}
    void do_init(FilteredInfo &info)override {
        vector<Vector2i> ids;vector<Vector3> as;vector<Matrix3x3> ms,rs;vector<Float> theta,omega,k;
        info.for_each(world().scene().geometries(),[&](geometry::Geometry &geometry){
            auto sc=geometry.as<geometry::SimplicialComplex>();
            auto li=sc->vertices().find<IndexT>("l_geo_id")->view(),ri=sc->vertices().find<IndexT>("r_geo_id")->view();
            auto la=sc->vertices().find<IndexT>("l_inst_id")->view(),ra=sc->vertices().find<IndexT>("r_inst_id")->view();
            auto av=sc->vertices().find<Vector3>("paper_axis_rest")->view();auto mv=sc->vertices().find<Matrix3x3>("paper_alignment")->view();
            auto tv=sc->vertices().find<Float>("paper_angle")->view(),wv=sc->vertices().find<Float>("paper_angular_velocity")->view(),kv=sc->vertices().find<Float>("strength_ratio")->view();
            for(SizeT i=0;i<sc->vertices().size();++i){ids.push_back({info.body_id(li[i],la[i]),info.body_id(ri[i],ra[i])});as.push_back(av[i]);ms.push_back(mv[i]);rs.push_back(Matrix3x3::Identity());theta.push_back(tv[i]);omega.push_back(wv[i]);k.push_back(kv[i]);}
        });
        bodyIds.copy_from(ids);axes.copy_from(as);alignment.copy_from(ms);frozen.copy_from(rs);angles.copy_from(theta);angularVelocity.copy_from(omega);strength.copy_from(k);
    }
    void do_report_energy_extent(EnergyExtentInfo &info)override{info.energy_count(bodyIds.size());}
    void do_report_gradient_hessian_extent(GradientHessianExtentInfo &info)override{info.gradient_count(2*bodyIds.size());if(!info.gradient_only())info.hessian_count(3*bodyIds.size());}
    void do_compute_energy(ComputeEnergyInfo &info)override {
        int n=int(bodyIds.size());if(n)rotationEnergy<<<(n+63)/64,64>>>(bodyIds.cview(),alignment.cview(),frozen.cview(),strength.cview(),info.body_masses(),info.qs(),info.energies(),n);
    }
    void do_compute_gradient_hessian(ComputeGradientHessianInfo &info)override {
        int n=int(bodyIds.size());if(n)rotationDerivatives<<<(n+31)/32,32>>>(bodyIds.cview(),axes.cview(),alignment.cview(),frozen.view(),angles.cview(),angularVelocity.cview(),engine().frame()*info.dt(),strength.cview(),info.body_masses(),info.qs(),info.gradients(),info.hessians(),info.gradient_only(),n);
    }
};
REGISTER_SIM_SYSTEM(PaperRotation);
}
