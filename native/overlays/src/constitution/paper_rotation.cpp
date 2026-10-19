#include <uipc/constitution/paper_rotation.h>
#include <uipc/constitution/affine_body_spherical_joint.h>
#include <uipc/builtin/constitution_uid_auto_register.h>
#include <uipc/builtin/constitution_type.h>
#include <uipc/builtin/attribute_name.h>
#include <Eigen/Dense>
namespace uipc::constitution {
REGISTER_CONSTITUTION_UIDS() {
    list<builtin::UIDInfo> info;info.push_back(builtin::UIDInfo{.uid=10011,.name="PaperRotation",.type=string{builtin::InterAffineBody}});return info;
}
geometry::SimplicialComplex PaperRotation::create_geometry(S<geometry::SimplicialComplexSlot> a,S<geometry::SimplicialComplexSlot> b,
        const Vector3 &anchor,const Vector3 &worldAxis,Float angle,Float strength,Float angularVelocity) {
    vector<Vector3> anchors{anchor};vector<S<geometry::SimplicialComplexSlot>> as{a},bs{b};vector<IndexT> ids{0};vector<Float> strengths{strength};
    AffineBodySphericalJoint binding;auto sc=binding.create_geometry(anchors,as,ids,bs,ids,strengths);
    geometry::view(*sc.meta().find<U64>(builtin::constitution_uid))[0]=uid();
    Matrix3x3 fa=a->geometry().transforms().view()[0].block<3,3>(0,0),fb=b->geometry().transforms().view()[0].block<3,3>(0,0);
    sc.vertices().create<Vector3>("paper_axis_rest",fa.inverse()*worldAxis.normalized());
    sc.vertices().create<Matrix3x3>("paper_alignment",fa.inverse()*fb);
    sc.vertices().create<Float>("paper_angle",angle);
    sc.vertices().create<Float>("paper_angular_velocity",angularVelocity);
    return sc;
}
}
