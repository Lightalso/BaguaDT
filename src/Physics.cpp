#include "Physics.h"
#include "ChronoHsc.h"
#include "Fem.h"
#include "HscNative.h"
#include <QQuaternion>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#ifdef BAGUA_HAS_CHRONO
#include "chrono/collision/ChCollisionShapeBox.h"
#include "chrono/collision/ChCollisionShapeConvexHull.h"
#include "chrono/collision/ChCollisionShapeCylinder.h"
#include "chrono/collision/ChCollisionShapeEllipsoid.h"
#include "chrono/collision/ChCollisionShapeSphere.h"
#include "chrono/collision/ChCollisionShapeTriangleMesh.h"
#include "chrono/functions/ChFunctionRamp.h"
#include "chrono/functions/ChFunctionConst.h"
#include "chrono/geometry/ChTriangleMeshConnected.h"
#include "chrono/physics/ChBody.h"
#include "chrono/physics/ChContactMaterialNSC.h"
#include "chrono/physics/ChContactMaterialSMC.h"
#include "chrono/physics/ChLoadContainer.h"
#include "chrono/physics/ChLinkLock.h"
#include "chrono/physics/ChLoadsNodeXYZ.h"
#include "chrono/physics/ChSystemNSC.h"
#include "chrono/physics/ChSystemSMC.h"
#include "chrono/solver/ChDirectSolverLS.h"
#include "chrono/solver/ChIterativeSolver.h"
#endif
namespace bagua {
struct Physics::Impl {
    std::unique_ptr<HscNative> native;
#ifdef BAGUA_HAS_CHRONO
    std::unique_ptr<chrono::ChSystem> system;
    std::vector<std::shared_ptr<chrono::ChBody>> bodies;
    std::vector<FemBinding> fem;
    std::shared_ptr<ChronoHsc> hsc;
#endif
    QString description;
};
Physics::Physics() = default;
Physics::~Physics() = default;
bool Physics::available() {
#ifdef BAGUA_HAS_CHRONO
    return true;
#else
    return false;
#endif
}
bool Physics::active() const { return bool(impl); }
void Physics::reset() {
    impl.reset();
    preview.objects.clear();
    preview.selected.clear();
}
double Physics::time() const {
    if (impl && impl->native) return impl->native->time();
#ifdef BAGUA_HAS_CHRONO
    return impl ? impl->system->GetChTime() : 0;
#else
    return 0;
#endif
}
int Physics::contacts() const {
    if (impl && impl->native) return impl->native->stats().contactCount;
#ifdef BAGUA_HAS_CHRONO
    return impl ? impl->system->GetNumContacts() : 0;
#else
    return 0;
#endif
}
bool Physics::start(const Scene &source, QString *error, HscOptions options) {
    if (options.enabled && options.nativePipeline) {
        auto next=std::make_unique<Impl>();next->native=std::make_unique<HscNative>();
        if (!next->native->start(source,preview,options,error))return false;
        next->description=QStringLiteral("HSC CUDA · ABD / IPC / CCD / Newton");impl=std::move(next);return true;
    }
#ifdef BAGUA_HAS_CHRONO
    try {
        auto next = std::make_unique<Impl>();
        const bool elastic =
            std::any_of(source.objects.cbegin(), source.objects.cend(), [](const Object &o) { return o.deformable; });
        const bool mixed = std::any_of(source.objects.cbegin(), source.objects.cend(), [](const Object &o) {
            return !o.deformable && !o.fixed && o.shape != Shape::Plane;
        });
        const bool freeElastic = std::any_of(source.objects.cbegin(), source.objects.cend(),
                                             [](const Object &o) { return o.deformable && !o.fixed; });
        std::shared_ptr<chrono::ChContactMaterial> material;
        if (elastic) {
            auto system = std::make_unique<chrono::ChSystemSMC>();
            system->UseMaterialProperties(false);
            system->SetContactForceModel(chrono::ChSystemSMC::Hooke);
            system->SetTangentialDisplacementModel(chrono::ChSystemSMC::None);
            system->SetContactStiff(true);
            auto smc = chrono_types::make_shared<chrono::ChContactMaterialSMC>();
            smc->SetKn(20000);
            smc->SetGn(80);
            smc->SetFriction(.5f);
            material = smc;
            if (options.enabled && mixed && freeElastic) {
                next->hsc = chrono_types::make_shared<ChronoHsc>(options);
                system->SetSolver(next->hsc);
            } else
                system->SetSolver(chrono_types::make_shared<chrono::ChSolverSparseLU>());
            next->description = next->hsc ? QStringLiteral("Chrono SMC/FEM · HSC CPU 实验适配")
                                          : QStringLiteral("Chrono SMC/FEM · SparseLU 基线（HSC 未启用）");
            next->system = std::move(system);
        } else {
            next->system = std::make_unique<chrono::ChSystemNSC>();
            next->system->SetSolverType(chrono::ChSolver::Type::PSOR);
            next->system->GetSolver()->AsIterative()->SetMaxIterations(100);
            auto nsc = chrono_types::make_shared<chrono::ChContactMaterialNSC>();
            nsc->SetFriction(.5f);
            nsc->SetRestitution(.1f);
            material = nsc;
            next->description = options.enabled ? QStringLiteral("Chrono NSC · PSOR（纯刚体，HSC 不适用）")
                                                : QStringLiteral("Chrono NSC · PSOR");
        }
        auto &sys = *next->system;
        sys.SetCollisionSystemType(chrono::ChCollisionSystem::Type::BULLET);
        sys.SetGravitationalAcceleration({0, -9.81, 0});
        auto runtime = source.objects;
        for (const auto &o : source.objects) {
            const auto d = o.dimensions * o.scale;
            // The editor allows very large scale ranges; reject unsupported physical sizes explicitly.
            for (int a = 0; a < 3; ++a)
                if (!std::isfinite(d[a]) || d[a] < .001f || d[a] > 10000.f || !std::isfinite(o.position[a]) ||
                    !std::isfinite(o.rotation[a]))
                    throw std::runtime_error("Physical dimensions must be between 0.001 and 10000 metres.");
            if (!std::isfinite(o.mass) || o.mass < .001 || o.mass > 1e9)
                throw std::runtime_error("Mass must be between 0.001 and 1e9 kg.");
            if (o.deformable) {
                auto binding = makeFem(o, int(next->bodies.size()), sys, material);
                runtime[binding.objectIndex] = binding.visual;
                next->fem.push_back(std::move(binding));
                next->bodies.push_back(nullptr);
                continue;
            }
            auto body = chrono_types::make_shared<chrono::ChBody>();
            body->SetName(o.id.toStdString());
            body->SetPos({o.position.x(), o.position.y(), o.position.z()});
            auto q = QQuaternion::fromEulerAngles(o.rotation);
            body->SetRot({q.scalar(), q.x(), q.y(), q.z()});
            body->SetPosDt({o.linearVelocity.x(),o.linearVelocity.y(),o.linearVelocity.z()});
            body->SetAngVelParent(chrono::ChVector3d(o.angularVelocity.x(),o.angularVelocity.y(),o.angularVelocity.z())*(chrono::CH_PI/180.));
            body->SetFixed(o.fixed || o.shape == Shape::Plane);
            body->SetMass(o.mass);
            double x = d.x(), y = o.shape == Shape::Plane ? .1 : d.y(), z = d.z();
            chrono::ChVector3d inertia{(y * y + z * z) / 12, (x * x + z * z) / 12, (x * x + y * y) / 12};
            if (o.shape == Shape::Sphere)
                inertia = {(y * y + z * z) / 20, (x * x + z * z) / 20, (x * x + y * y) / 20};
            if (o.shape == Shape::Cylinder)
                inertia = {y * y / 12 + z * z / 16, (x * x + z * z) / 16, y * y / 12 + x * x / 16};
            body->SetInertiaXX(inertia * o.mass);
            if (o.collisionEnabled) {
                if (o.shape == Shape::Mesh) {
                    if (!o.mesh)
                        throw std::runtime_error("Missing mesh asset");
                    auto point = [&](quint32 i) {
                        auto p = o.mesh->positions[i] * d;
                        return chrono::ChVector3d(p.x(), p.y(), p.z());
                    };
                    if (o.fixed) {
                        auto mesh = chrono_types::make_shared<chrono::ChTriangleMeshConnected>();
                        for (qsizetype i = 0; i < o.mesh->indices.size(); i += 3)
                            mesh->AddTriangle(point(o.mesh->indices[i]), point(o.mesh->indices[i + 1]),
                                              point(o.mesh->indices[i + 2]));
                        body->AddCollisionShape(chrono_types::make_shared<chrono::ChCollisionShapeTriangleMesh>(
                            material, mesh, true, false));
                    } else {
                        std::vector<chrono::ChVector3d> points;
                        for (quint32 i = 0; i < o.mesh->positions.size(); ++i)
                            points.push_back(point(i));
                        // A planar surface is valid as fixed terrain, but not a volumetric rigid hull.
                        if (points.size() < 4)
                            throw std::runtime_error(
                                "Dynamic mesh collision requires a volumetric mesh; keep surfaces fixed.");
                        auto origin = points.front();
                        chrono::ChVector3d edge(0), normal(0);
                        double span2 = 0, area2 = 0, volume = 0;
                        for (auto p : points)
                            if ((p - origin).Length2() > span2) {
                                edge = p - origin;
                                span2 = edge.Length2();
                            }
                        for (auto p : points) {
                            auto n = chrono::Vcross(edge, p - origin);
                            if (n.Length2() > area2) {
                                normal = n;
                                area2 = n.Length2();
                            }
                        }
                        for (auto p : points)
                            volume = std::max(volume, std::abs(chrono::Vdot(normal, p - origin)));
                        if (span2 <= 0 || area2 <= 0 || volume <= 1e-7 * std::sqrt(span2 * area2))
                            throw std::runtime_error(
                                "Dynamic mesh collision requires a volumetric mesh; keep surfaces fixed.");
                        body->AddCollisionShape(
                            chrono_types::make_shared<chrono::ChCollisionShapeConvexHull>(material, points));
                    }
                } else if (o.shape == Shape::Plane) {
                    // A finite slab below the displayed XZ surface, always fixed.
                    // Use an explicit vector: ChFrame<>({x,y,z}) can select the frame's
                    // position/angle/axis constructor instead of its translation constructor.
                    body->AddCollisionShape(chrono_types::make_shared<chrono::ChCollisionShapeBox>(material, x, y, z),
                                            chrono::ChFrame<>(chrono::ChVector3d(0, -y / 2, 0)));
                } else if (o.shape == Shape::Box) {
                    body->AddCollisionShape(chrono_types::make_shared<chrono::ChCollisionShapeBox>(material, x, y, z));
                } else if (o.shape == Shape::Sphere) {
                    if (std::abs(x - y) < 1e-7 && std::abs(x - z) < 1e-7)
                        body->AddCollisionShape(
                            chrono_types::make_shared<chrono::ChCollisionShapeSphere>(material, x / 2));
                    else
                        body->AddCollisionShape(
                            chrono_types::make_shared<chrono::ChCollisionShapeEllipsoid>(material, x, y, z));
                } else if (std::abs(x - z) < 1e-7) {
                    // Chrono cylinders are Z-aligned; editor cylinders and inertia are Y-aligned.
                    // Rotate only the collision shape, preserving the authored body's orientation.
                    body->AddCollisionShape(
                        chrono_types::make_shared<chrono::ChCollisionShapeCylinder>(material, x / 2, y),
                        chrono::ChFrame<>(chrono::VNULL, chrono::QuatFromAngleX(chrono::CH_PI_2)));
                } else {
                    // Match the viewport's 40-sided elliptical cylinder, including nonuniform scaling.
                    std::vector<chrono::ChVector3d> points;
                    for (int i = 0; i < 40; ++i) {
                        double a = i * 2 * 3.14159265358979323846 / 40;
                        for (double h : {-y / 2, y / 2})
                            points.emplace_back(x / 2 * std::cos(a), h, z / 2 * std::sin(a));
                    }
                    body->AddCollisionShape(
                        chrono_types::make_shared<chrono::ChCollisionShapeConvexHull>(material, points));
                }
                body->GetCollisionModel()->SetEnvelope(float(std::min({x, y, z}) * .001));
                body->GetCollisionModel()->SetSafeMargin(float(std::min({x, y, z}) * .0005));
                body->EnableCollision(true);
            }
            sys.AddBody(body);
            next->bodies.push_back(body);
        }
        // The scene's joint schema maps to exact Chrono links in the baseline mode.
        for (auto value:source.joints) {
            auto j=value.toObject();auto body=[&](QString id) {
                for(int i=0;i<source.objects.size();++i)if(source.objects[i].id==id && next->bodies[size_t(i)])return next->bodies[size_t(i)];
                throw std::runtime_error("Chrono joint references a missing rigid body");
            };
            auto a=body(j["bodyA"].toString()),b=body(j["bodyB"].toString());auto p=j["anchor"].toArray(),axis=j["axis"].toArray();
            QVector3D direction(0,0,1);if(axis.size()==3)direction={float(axis[0].toDouble()),float(axis[1].toDouble()),float(axis[2].toDouble())};
            auto q=QQuaternion::rotationTo(QVector3D(0,0,1),direction.normalized());
            chrono::ChFrame<> frame(chrono::ChVector3d(p[0].toDouble(),p[1].toDouble(),p[2].toDouble()),chrono::ChQuaternion<>(q.scalar(),q.x(),q.y(),q.z()));
            auto type=j["type"].toString();std::shared_ptr<chrono::ChLinkLock> link;
            if(type=="ball")link=chrono_types::make_shared<chrono::ChLinkLockSpherical>();
            else if(type=="hinge")link=chrono_types::make_shared<chrono::ChLinkLockRevolute>();
            else if(type=="prismatic")link=chrono_types::make_shared<chrono::ChLinkLockPrismatic>();
            else if(type=="rotation") {
                auto prescribed=chrono_types::make_shared<chrono::ChLinkLockLock>();
                prescribed->SetMotionAxis({0,0,1});
                prescribed->SetMotionAng1(chrono_types::make_shared<chrono::ChFunctionRamp>(j["angleDegrees"].toDouble()*chrono::CH_PI/180.,j["angularVelocityDegrees"].toDouble()*chrono::CH_PI/180.));
                link=prescribed;
            }
            else throw std::runtime_error("Unsupported Chrono joint type");
            if(type=="rotation")link->Initialize(b,a,frame);else link->Initialize(a,b,frame);sys.AddLink(link);
        }
        // Stiff public load contributes genuine off-diagonal FEM/rigid Jacobian blocks.
        auto loads = chrono_types::make_shared<chrono::ChLoadContainer>();
        int activeDofs = 0;
        for (const auto &body : next->bodies)
            if (body && !body->IsFixed())
                activeDofs += 6;
        for (const auto &binding : next->fem) {
            const auto &o = source.objects[binding.objectIndex];
            for (const auto &node : binding.nodes)
                if (!node->IsFixed())
                    activeDofs += 3;
            if (o.femAttachmentBody.isEmpty())
                continue;
            int target = -1;
            for (int i = 0; i < source.objects.size(); ++i)
                if (source.objects[i].id == o.femAttachmentBody)
                    target = i;
            if (target < 0 || !next->bodies[size_t(target)] || next->bodies[size_t(target)]->IsFixed())
                throw std::runtime_error("FEM attachment must refer to an existing dynamic rigid body ID.");
            if (!std::isfinite(o.attachmentStiffness) || o.attachmentStiffness < 1 || o.attachmentStiffness > 1e8)
                throw std::runtime_error("FEM attachment stiffness must be between 1 and 1e8 N/m.");
            QVector3D lo(1e30f,1e30f,1e30f),hi(-1e30f,-1e30f,-1e30f);
            for(auto p:binding.visual.mesh->positions)for(int k=0;k<3;++k){lo[k]=std::min(lo[k],p[k]);hi[k]=std::max(hi[k],p[k]);}
            QVector3D attachmentPoint((lo.x()+hi.x())*.5f,hi.y(),(lo.z()+hi.z())*.5f);int center=0;float nearest=std::numeric_limits<float>::infinity();
            for(int i=0;i<binding.visual.mesh->positions.size();++i){float distance=(binding.visual.mesh->positions[i]-attachmentPoint).lengthSquared();if(distance<nearest){nearest=distance;center=i;}}
            auto node = binding.nodes[size_t(center)];
            auto body = next->bodies[size_t(target)];
            auto load = chrono_types::make_shared<chrono::ChLoadNodeXYZBodyBushing>(node, body);
            load->SetApplicationFrameB(chrono::ChFrame<>(body->TransformPointParentToLocal(node->GetPos())));
            auto spring = chrono_types::make_shared<chrono::ChFunctionRamp>(0, -o.attachmentStiffness);
            load->SetFunctionForceX(spring);
            load->SetFunctionForceY(spring);
            load->SetFunctionForceZ(spring);
            load->SetDamping({10, 10, 10});
            load->SetStiff(true);
            loads->Add(load);
        }
        if (!next->fem.empty())
            sys.Add(loads);
        if (next->hsc && activeDofs > 1536)
            throw std::runtime_error(
                "HSC CPU reference supports at most 1536 active DOFs; lower FEM resolution or disable HSC.");
        preview.objects = std::move(runtime);
        preview.selected = source.selected;
        impl = std::move(next);
        return true;
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return false;
    }
#else
    Q_UNUSED(source);
    Q_UNUSED(options);
    if (error)
        *error = QStringLiteral("此构建未启用 Project Chrono");
    return false;
#endif
}
bool Physics::step(int count, QString *error) {
    if (!impl || count < 1 || count > 2400) {
        if (error)
            *error = QStringLiteral("仿真尚未启动或步数无效");
        return false;
    }
    if (impl->native) return impl->native->step(count,preview,error);
#ifdef BAGUA_HAS_CHRONO
    try {
        for (int i = 0; i < count; ++i)
            impl->system->DoStepDynamics(timeStep);
        auto updated = preview.objects;
        for (int i = 0; i < updated.size(); ++i) {
            auto &o = updated[i];
            const auto &body = impl->bodies[i];
            if (!body)
                continue;
            auto p = body->GetPos();
            auto q = body->GetRot();
            if (!std::isfinite(p.x()) || !std::isfinite(p.y()) || !std::isfinite(p.z()) || !std::isfinite(q.e0()) ||
                !std::isfinite(q.e1()) || !std::isfinite(q.e2()) || !std::isfinite(q.e3()))
                throw std::runtime_error("Non-finite simulation result; reset the simulation.");
            o.position = {float(p.x()), float(p.y()), float(p.z())};
            o.rotation = QQuaternion(float(q.e0()), float(q.e1()), float(q.e2()), float(q.e3())).toEulerAngles();
        }
        for (const auto &binding : impl->fem)
            updated[binding.objectIndex] = updateFem(binding, updated[binding.objectIndex]);
        preview.objects = std::move(updated);
        emit preview.changed();
        return true;
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return false;
    }
#else
    Q_UNUSED(error);
    return false;
#endif
}
QString Physics::solverDescription() const { return impl ? impl->description : QStringLiteral("模拟未开始"); }
bool Physics::hscActive() const {
    if (impl && impl->native) return true;
#ifdef BAGUA_HAS_CHRONO
    return impl && bool(impl->hsc);
#else
    return false;
#endif
}
HscStats Physics::hscStats() const {
    if (impl && impl->native) return impl->native->stats();
#ifdef BAGUA_HAS_CHRONO
    return impl && impl->hsc ? impl->hsc->stats : HscStats{};
#else
    return {};
#endif
}
} // namespace bagua
