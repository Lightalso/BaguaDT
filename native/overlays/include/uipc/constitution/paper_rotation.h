#pragma once
#include <uipc/constitution/inter_affine_body_constitution.h>
#include <uipc/geometry/simplicial_complex_slot.h>
namespace uipc::constitution {
class UIPC_CONSTITUTION_API PaperRotation final:public InterAffineBodyConstitution {
public:
    geometry::SimplicialComplex create_geometry(S<geometry::SimplicialComplexSlot> a,S<geometry::SimplicialComplexSlot> b,
        const Vector3 &anchor,const Vector3 &worldAxis,Float angle,Float strength,Float angularVelocity=0);
private:
    U64 get_uid()const noexcept override{return 10011;}
};
}
