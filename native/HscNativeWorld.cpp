#include "HscNativeApi.h"
#include "HscTypes.h"
#include <Eigen/Dense>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <uipc/common/uipc.h>
#include <uipc/constitution/affine_body_constitution.h>
#include <uipc/constitution/affine_body_fixed_joint.h>
#include <uipc/constitution/affine_body_prismatic_joint.h>
#include <uipc/constitution/affine_body_prismatic_joint_limit.h>
#include <uipc/constitution/affine_body_revolute_joint.h>
#include <uipc/constitution/affine_body_revolute_joint_limit.h>
#include <uipc/constitution/affine_body_spherical_joint.h>
#include <uipc/constitution/log_neo_hookean.h>
#include <uipc/constitution/paper_rotation.h>
#include <uipc/constitution/soft_vertex_stitch.h>
#include <uipc/uipc.h>
#include <unordered_map>
#include <utility>
#define NOMINMAX
#include <Windows.h>
namespace {
using namespace uipc;
using namespace uipc::core;
using namespace uipc::geometry;
using namespace uipc::constitution;
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
Vector3 vector3(const Json &j) {
    require(j.is_array() && j.size() == 3, "Expected XYZ vector");
    return {j[0].get<Float>(), j[1].get<Float>(), j[2].get<Float>()};
}
void errorText(char *out, int capacity, const char *message) {
    if (out && capacity > 0) {
        std::strncpy(out, message, size_t(capacity) - 1);
        out[capacity - 1] = 0;
    }
}
struct Binding {
    std::string id;
    S<SimplicialComplexSlot> slot;
    bool affine = false;
    std::vector<int> anchors;
};
// Runtime record of one authored link, kept for per-step diagnostics.
struct LinkRecord {
    std::string id, type;
    S<SimplicialComplexSlot> slot;
    double rest = 0;
    bool limit = false;
    double lower = 0, upper = 0;
    size_t a = 0, b = 0;
    std::vector<Vector2i> pairs;
    Vector3 anchorA = Vector3::Zero(), anchorB = Vector3::Zero(), axisA = Vector3::UnitZ();
};
struct NativeWorld {
    std::unique_ptr<Engine> engine;
    std::unique_ptr<Scene> scene;
    std::unique_ptr<World> world;
    std::vector<Binding> bindings;
    std::vector<LinkRecord> linkRecords;
    std::string state;
    double dt = 1. / 240.;
    std::map<std::string, size_t> byId;
    explicit NativeWorld(const Json &input, const std::string &workspace) {
        auto global = default_config();
        global["module_dir"] = input.at("moduleDir");
        uipc::init(global);
        std::filesystem::create_directories(workspace);
        engine = std::make_unique<Engine>("cuda", workspace);
        auto config = Scene::default_config();
        auto options = input.value("options", Json::object());
        dt = input.value("dt", dt);
        require(dt > 0 && dt <= .1, "Invalid HSC timestep");
        config["dt"] = dt;
        config["gravity"] = Vector3(0, -9.81, 0);
        config["contact"]["enable"] = true;
        config["contact"]["friction"]["enable"] = true;
        config["contact"]["d_hat"] = options.value("contactDistance", .001);
        config["linear_system"]["solver"] = "linear_hsc";
        config["linear_system"]["tol_rate"] = options.value("linearTolerance", 1e-4);
        config["linear_system"]["hsc"] = {{"max_rank", options.value("maxRank", 48)},
                                          {"neumann_order", options.value("neumannTerms", 5)},
                                          {"max_iter", options.value("maxIterations", 2000)},
                                          {"omega", options.value("omega", .7)},
                                          {"adaptive_abd_inverse", options.value("adaptiveInverse", false) ? 1 : 0}};
        config["line_search"]["max_iter"] = 64;
        config["newton"]["max_iter"] = 300;
        config["newton"]["semi_implicit"]["enable"] = 0;
        config["extras"]["strict_mode"]["enable"] = 1;
        scene = std::make_unique<Scene>(config);
        AffineBodyConstitution abd;
        LogNeoHookean fem;
        SoftVertexStitch stitch;
        AffineBodySphericalJoint spherical;
        AffineBodyRevoluteJoint revolute;
        AffineBodyPrismaticJoint prismatic;
        AffineBodyFixedJoint fixedJoint;
        PaperRotation rotation;
        AffineBodyPrismaticJointLimit prismaticLimit;
        AffineBodyRevoluteJointLimit revoluteLimit;
        scene->constitution_tabular().insert(abd);
        scene->constitution_tabular().insert(fem);
        scene->constitution_tabular().insert(stitch);
        scene->constitution_tabular().insert(spherical);
        scene->constitution_tabular().insert(revolute);
        scene->constitution_tabular().insert(prismatic);
        scene->constitution_tabular().insert(fixedJoint);
        scene->constitution_tabular().insert(rotation);
        auto &contacts = scene->contact_tabular();
        contacts.default_model(.5, options.value("contactStiffness", 1e8));
        // Anchors must never collide: they are coincident with the surface by design.
        auto virtualElement = contacts.create("virtual_anchor");
        std::vector<ContactElement> elements;
        std::vector<bool> collision;
        for (const auto &object : input.at("objects")) {
            std::string id = object.at("id");
            require(!byId.contains(id), "Duplicate HSC object ID");
            vector<Vector3> vertices;
            for (const auto &v : object.at("positions"))
                vertices.push_back(vector3(v));
            require(!vertices.empty(), "HSC geometry has no vertices");
            bool elastic = object.value("deformable", false);
            SimplicialComplex mesh = [&]() {
                if (elastic) {
                    vector<Vector4i> tets;
                    double volume = 0;
                    for (const auto &tet : object.at("tetrahedra")) {
                        Vector4i ids(tet[0].get<int>(), tet[1].get<int>(), tet[2].get<int>(), tet[3].get<int>());
                        for (int i = 0; i < 4; ++i)
                            require(ids[i] >= 0 && size_t(ids[i]) < vertices.size(),
                                    "Invalid tetrahedron vertex index");
                        double v = (vertices[size_t(ids[1])] - vertices[size_t(ids[0])])
                                       .dot((vertices[size_t(ids[2])] - vertices[size_t(ids[0])])
                                                .cross(vertices[size_t(ids[3])] - vertices[size_t(ids[0])])) /
                                   6;
                        require(std::abs(v) > 1e-15, "Degenerate HSC tetrahedron");
                        if (v < 0)
                            std::swap(ids[1], ids[2]);
                        volume += std::abs(v);
                        tets.push_back(ids);
                    }
                    require(volume > 0, "FEM needs tetrahedral volume geometry");
                    auto mesh = tetmesh(vertices, tets);
                    fem.apply_to(mesh,
                                 ElasticModuli::youngs_poisson(object.value("youngModulus", 5000.),
                                                               object.value("poissonRatio", .3)),
                                 object.value("mass", 1.) / volume);
                    auto pins = mesh.vertices().find<IndexT>(builtin::is_fixed);
                    require(bool(pins), "FEM fixed-node attribute missing");
                    for (const auto &index : object.value("pinned", Json::array())) {
                        int i = index.get<int>();
                        require(i >= 0 && size_t(i) < vertices.size(), "Invalid pinned node");
                        view(*pins)[i] = 1;
                    }
                    return mesh;
                } else {
                    vector<Vector3i> faces;
                    double volume = 0;
                    for (const auto &face : object.at("triangles")) {
                        Vector3i ids(face[0].get<int>(), face[1].get<int>(), face[2].get<int>());
                        for (int i = 0; i < 3; ++i)
                            require(ids[i] >= 0 && size_t(ids[i]) < vertices.size(), "Invalid triangle vertex index");
                        volume +=
                            vertices[size_t(ids[0])].dot(vertices[size_t(ids[1])].cross(vertices[size_t(ids[2])])) / 6;
                        faces.push_back(ids);
                    }
                    require(std::abs(volume) > 1e-15, "ABD requires a closed nonzero-volume surface");
                    if (volume < 0)
                        for (auto &face : faces)
                            std::swap(face[1], face[2]);
                    // IPC/ABD only defines a volume for a watertight surface; report it here instead of
                    // letting the backend's internal assertion surface as the error message.
                    std::unordered_map<uint64_t, int> edges;
                    edges.reserve(faces.size() * 3);
                    for (const auto &face : faces)
                        for (int k = 0; k < 3; ++k) {
                            uint32_t u = uint32_t(face[k]), v = uint32_t(face[(k + 1) % 3]);
                            if (u > v)
                                std::swap(u, v);
                            ++edges[(uint64_t(u) << 32) | v];
                        }
                    for (const auto &edge : edges)
                        require(edge.second == 2, "Dynamic bodies need a watertight surface: every triangle edge must "
                                                  "be shared by exactly two triangles");
                    auto mesh = trimesh(vertices, faces);
                    abd.apply_to(mesh, options.value("abdStiffness", 1e9), object.value("mass", 1.) / std::abs(volume));
                    auto transform = object.at("transform");
                    Matrix4x4 matrix;
                    for (int r = 0; r < 4; ++r)
                        for (int c = 0; c < 4; ++c)
                            matrix(r, c) = transform[r * 4 + c].get<Float>();
                    view(mesh.transforms())[0] = matrix;
                    view(*mesh.instances().find<IndexT>(builtin::is_fixed))[0] = object.value("fixed", false) ? 1 : 0;
                    return mesh;
                }
            }();
            Vector3 velocity = vector3(object.value("linearVelocity", Json::array({0, 0, 0}))),
                    omega =
                        vector3(object.value("angularVelocity", Json::array({0, 0, 0}))) * (3.141592653589793 / 180.);
            if (elastic) {
                auto velocities = mesh.vertices().find<Vector3>(builtin::velocity);
                Vector3 origin = vector3(object.value("origin", Json::array({0, 0, 0})));
                for (size_t i = 0; i < vertices.size(); ++i)
                    view(*velocities)[i] = velocity + omega.cross(vertices[i] - origin);
            } else {
                Matrix4x4 rate = Matrix4x4::Zero();
                Matrix3x3 skew;
                skew << 0, -omega[2], omega[1], omega[2], 0, -omega[0], -omega[1], omega[0], 0;
                rate.block<3, 3>(0, 0) = skew * mesh.transforms().view()[0].block<3, 3>(0, 0);
                rate.block<3, 1>(0, 3) = velocity;
                view(*mesh.instances().find<Matrix4x4>(builtin::velocity))[0] = rate;
            }
            label_surface(mesh);
            if (elastic)
                label_triangle_orient(mesh);
            auto element = contacts.create(id);
            element.apply_to(mesh);
            elements.push_back(element);
            collision.push_back(object.value("collision", true));
            std::vector<int> anchors;
            for (const auto &value : object.value("anchors", Json::array())) {
                int index = value.get<int>();
                require(index >= 0 && size_t(index) < vertices.size(), "Anchor index out of range");
                require(!elastic, "Anchors require a rigid (ABD) body");
                anchors.push_back(index);
            }
            if (!anchors.empty()) {
                // Anchors are excluded from the surface and from contact: they are coincident
                // with body features by design, and a zero-distance IPC pair would be singular.
                if (auto surface = mesh.vertices().find<IndexT>(builtin::is_surf))
                    for (int index : anchors)
                        view(*surface)[index] = 0;
                auto contactIds = mesh.vertices().find<IndexT>(builtin::contact_element_id);
                if (!contactIds)
                    contactIds = mesh.vertices().create<IndexT>(builtin::contact_element_id, element.id());
                for (int index : anchors)
                    view(*contactIds)[index] = virtualElement.id();
            }
            auto obj = scene->objects().create(id);
            auto slots = obj->geometries().create(mesh);
            byId[id] = bindings.size();
            bindings.push_back({id, slots.geometry, !elastic, std::move(anchors)});
        }
        // Anchor vertices are isolated from every other contact element.
        contacts.insert(virtualElement, contacts.default_element(), .5, 1e8, false);
        contacts.insert(virtualElement, virtualElement, .5, 1e8, false);
        for (const auto &element : elements)
            contacts.insert(virtualElement, element, .5, 1e8, false);
        std::set<std::pair<size_t, size_t>> disabledPairs;
        for (size_t i = 0; i < elements.size(); ++i)
            for (size_t j = i; j < elements.size(); ++j)
                if (!collision[i] || !collision[j]) {
                    contacts.insert(elements[i], elements[j], .5, 1e8, false);
                    disabledPairs.insert({i, j});
                }
        // Explicit part-to-part contact exclusions (overlapping proxies inside an assembly).
        for (const auto &exclusion : input.value("exclusions", Json::array())) {
            std::string a = exclusion.at("a"), b = exclusion.at("b");
            require(byId.contains(a) && byId.contains(b), "Contact exclusion references a missing body");
            require(a != b, "Contact exclusion must join two different bodies");
            size_t i = byId.at(a), j = byId.at(b);
            if (i > j)
                std::swap(i, j);
            if (!disabledPairs.insert({i, j}).second)
                continue;
            contacts.insert(elements[i], elements[j], .5, 1e8, false);
        }
        // A finite-length soft stitch preserves the authored separation at the attachment.
        for (const auto &object : input.at("objects")) {
            std::string target = object.value("attachmentBody", std::string());
            if (target.empty())
                continue;
            require(byId.contains(target), "FEM attachment target missing");
            auto &left = bindings[byId.at(object.at("id").get<std::string>())], &right = bindings[byId.at(target)];
            require(!left.affine && right.affine, "FEM attachment must connect FEM to ABD");
            int node = object.at("attachmentNode");
            auto p = left.slot->geometry().positions().view()[node];
            auto rest = right.slot->geometry().positions().view();
            auto matrix = right.slot->geometry().transforms().view()[0];
            int closest = 0;
            double distance = std::numeric_limits<double>::infinity();
            for (size_t i = 0; i < rest.size(); ++i) {
                Vector3 q = (matrix * Vector4(rest[i][0], rest[i][1], rest[i][2], 1)).head<3>();
                double d = (q - p).norm();
                if (d < distance) {
                    distance = d;
                    closest = int(i);
                }
            }
            vector<Vector2i> pairs{Vector2i(node, closest)};
            auto geometry = stitch.create_geometry({left.slot, right.slot}, pairs,
                                                   object.value("attachmentStiffness", 20000.), distance);
            scene->objects().create("attachment-" + left.id)->geometries().create(geometry);
        }
        // Authored links: kinematic joints (body-local anchors), limits, and soft vertex stitches.
        for (const auto &link : input.value("links", Json::array())) {
            std::string id = link.at("id"), type = link.at("type"), a = link.at("a"), b = link.at("b");
            require(byId.contains(a) && byId.contains(b), "Link references a missing body");
            auto &left = bindings[byId.at(a)], &right = bindings[byId.at(b)];
            if (type == "stitch" || type == "spring") {
                require(left.affine, "Stitch anchors must live on a rigid (ABD) body");
                bool anchorPairs = link.value("anchorPairs", false);
                if (anchorPairs)
                    require(right.affine, "Anchor-pair stitches need a rigid (ABD) body on both sides");
                std::vector<Vector2i> pairs;
                for (const auto &pair : link.at("pairs")) {
                    int ordinal = pair[0].get<int>(), vertex = pair[1].get<int>();
                    require(ordinal >= 0 && size_t(ordinal) < left.anchors.size(),
                            "Stitch anchor ordinal out of range");
                    require(vertex >= 0, "Stitch vertex index out of range");
                    if (anchorPairs) {
                        require(size_t(vertex) < right.anchors.size(),
                                "Stitch anchor ordinal on the second body is out of range");
                        vertex = right.anchors[size_t(vertex)];
                    }
                    require(size_t(vertex) < right.slot->geometry().positions().view().size(),
                            "Stitch vertex index out of range");
                    pairs.push_back(Vector2i(left.anchors[size_t(ordinal)], vertex));
                }
                require(!pairs.empty(), "Stitch needs at least one vertex pair");
                double rest = link.value("rest", 0.);
                require(rest >= 0, "Stitch rest length must be non-negative");
                auto geometry =
                    stitch.create_geometry({left.slot, right.slot}, pairs, link.at("stiffness").get<Float>(), rest);
                scene->objects().create(id)->geometries().create(geometry);
                linkRecords.push_back(
                    {id, "stitch", S<SimplicialComplexSlot>{}, rest, false, 0, 0, byId.at(a), byId.at(b), pairs});
                continue;
            }
            require(left.affine && right.affine, "Kinematic links require two rigid bodies");
            Vector3 anchorA = vector3(link.at("anchorA")), anchorB = vector3(link.at("anchorB"));
            Float strength = link.value("strength", 100.);
            require(strength > 0, "Link strength must be positive");
            std::vector<S<SimplicialComplexSlot>> ls{left.slot}, rs{right.slot};
            std::vector<IndexT> ids{0};
            std::vector<Float> strengths{strength};
            std::vector<Vector3> la{anchorA}, rb{anchorB};
            SimplicialComplex geometry = [&]() -> SimplicialComplex {
                if (type == "spherical")
                    return spherical.create_geometry(la, rb, ls, ids, rs, ids, strengths);
                if (type == "fixed")
                    return fixedJoint.create_geometry(la, rb, ls, ids, rs, ids, strengths);
                Vector3 axis = vector3(link.at("axis"));
                require(axis.norm() > 1e-10, "Link axis is zero");
                Matrix3x3 lr = left.slot->geometry().transforms().view()[0].block<3, 3>(0, 0);
                Matrix3x3 rr = right.slot->geometry().transforms().view()[0].block<3, 3>(0, 0);
                if (type == "rotation") {
                    // Legacy paper-rotation drive: a spherical anchor plus a prescribed spin.
                    Vector3 world =
                        (left.slot->geometry().transforms().view()[0] * Vector4(anchorA[0], anchorA[1], anchorA[2], 1))
                            .head<3>();
                    std::vector<Vector3> anchorPoint{world};
                    auto anchor = spherical.create_geometry(la, rb, ls, ids, rs, ids, strengths);
                    scene->objects().create(id + "-anchor")->geometries().create(anchor);
                    Float angle = link.value("angle", 0.), speed = link.value("angularVelocity", 0.);
                    return rotation.create_geometry(left.slot, right.slot, world, axis, angle, strength, speed);
                }
                std::vector<Vector3> l0{anchorA}, l1{anchorA + (lr.inverse() * axis.normalized()) * .2};
                std::vector<Vector3> r0{anchorB}, r1{anchorB + (rr.inverse() * axis.normalized()) * .2};
                if (type == "revolute")
                    return revolute.create_geometry(l0, l1, r0, r1, ls, ids, rs, ids, strengths);
                if (type == "prismatic")
                    return prismatic.create_geometry(l0, l1, r0, r1, ls, ids, rs, ids, strengths);
                throw std::runtime_error("Unsupported HSC link type: " + type);
            }();
            bool limited = link.contains("limit");
            double lower = 0, upper = 0;
            if (limited) {
                require(type == "revolute" || type == "prismatic",
                        "Limits are only defined for revolute and prismatic links");
                auto limit = link.at("limit");
                lower = limit.value("lower", 0.);
                upper = limit.value("upper", 0.);
                require(lower < upper, "Link limit must satisfy lower < upper");
                Float limitStrength = limit.value("strength", 100.);
                require(limitStrength > 0, "Link limit strength must be positive");
                if (type == "prismatic")
                    prismaticLimit.apply_to(geometry, Float(lower), Float(upper), limitStrength);
                else
                    revoluteLimit.apply_to(geometry, Float(lower), Float(upper), limitStrength);
            }
            auto slots = scene->objects().create(id)->geometries().create(geometry);
            linkRecords.push_back({id, type, slots.geometry, 0., limited, lower, upper, byId.at(a), byId.at(b), {}});
            auto &record = linkRecords.back();
            record.anchorA = anchorA;
            record.anchorB = anchorB;
            if (type == "prismatic")
                record.axisA = left.slot->geometry().transforms().view()[0].block<3, 3>(0, 0).inverse() *
                               vector3(link.at("axis")).normalized();
        }
        world = std::make_unique<World>(*engine);
        world->init(*scene);
        require(world->is_valid(), "HSC world initialization failed; inspect native log");
    }
    const char *snapshot() {
        world->retrieve();
        Json result = {{"time", world->frame() * dt},
                       {"frame", world->frame()},
                       {"objects", Json::array()},
                       {"engineStats", engine->frame_stats()}};
        for (const auto &binding : bindings) {
            const auto &mesh = binding.slot->geometry();
            auto positions = mesh.positions().view();
            Json values = Json::array();
            Matrix4x4 transform = Matrix4x4::Identity();
            if (binding.affine)
                transform = mesh.transforms().view()[0];
            for (const auto &p : positions) {
                Vector3 q = (transform * Vector4(p[0], p[1], p[2], 1)).head<3>();
                values.push_back({q[0], q[1], q[2]});
            }
            result["objects"].push_back({{"id", binding.id}, {"positions", values}});
        }
        Json links = Json::array();
        for (const auto &record : linkRecords) {
            Json entry = {{"id", record.id}, {"type", record.type}};
            if (record.type == "stitch") {
                auto &left = bindings[record.a], &right = bindings[record.b];
                auto &lgeo = left.slot->geometry(), &rgeo = right.slot->geometry();
                auto lpos = lgeo.positions().view(), rpos = rgeo.positions().view();
                Matrix4x4 lt = Matrix4x4::Identity(), rt = Matrix4x4::Identity();
                if (left.affine)
                    lt = lgeo.transforms().view()[0];
                if (right.affine)
                    rt = rgeo.transforms().view()[0];
                double worst = 0;
                for (const auto &pair : record.pairs) {
                    Vector3 a =
                        (lt * Vector4(lpos[size_t(pair[0])][0], lpos[size_t(pair[0])][1], lpos[size_t(pair[0])][2], 1))
                            .head<3>();
                    Vector3 b =
                        (rt * Vector4(rpos[size_t(pair[1])][0], rpos[size_t(pair[1])][1], rpos[size_t(pair[1])][2], 1))
                            .head<3>();
                    worst = std::max(worst, std::abs((a - b).norm() - record.rest));
                }
                entry["error"] = worst;
            } else {
                auto *geo = record.slot->geometry().as<SimplicialComplex>();
                require(geo != nullptr, "Joint geometry is not a simplicial complex");
                if (auto distance = geo->edges().find<Float>("distance"))
                    entry["distance"] = distance->view()[0];
                if (auto angle = geo->edges().find<Float>("angle"))
                    entry["angle"] = angle->view()[0];
                auto lt = bindings[record.a].slot->geometry().transforms().view()[0],
                     rt = bindings[record.b].slot->geometry().transforms().view()[0];
                Vector3 delta = (lt * Vector4(record.anchorA[0], record.anchorA[1], record.anchorA[2], 1)).head<3>() -
                                (rt * Vector4(record.anchorB[0], record.anchorB[1], record.anchorB[2], 1)).head<3>();
                if (record.type == "prismatic") {
                    Vector3 axis = (lt.block<3, 3>(0, 0) * record.axisA).normalized();
                    delta -= delta.dot(axis) * axis;
                }
                entry["error"] = delta.norm();
                if (record.limit) {
                    entry["limitLower"] = record.lower;
                    entry["limitUpper"] = record.upper;
                }
            }
            links.push_back(entry);
        }
        result["links"] = links;
        using GetStats = const bagua::HscStats *(*)();
        auto get =
            reinterpret_cast<GetStats>(GetProcAddress(GetModuleHandleW(L"uipc_backend_cuda.dll"), "bagua_hsc_stats"));
        if (get) {
            auto s = *get();
            result["hsc"] = {{"contacts", s.contactCount},
                             {"rank", s.rank},
                             {"calls", s.calls},
                             {"iterations", s.iterations},
                             {"relativeResidual", s.relativeResidual},
                             {"acaResidual", s.acaResidual},
                             {"milliseconds", s.milliseconds},
                             {"nonzeros", s.nonzeros},
                             {"factorNonzeros", s.factorNonzeros},
                             {"inverseFallbacks", s.inverseFallbacks}};
        }
        state = result.dump();
        return state.c_str();
    }
};
} // namespace
extern "C" {
unsigned bagua_hsc_native_abi() { return 1; }
void *bagua_hsc_native_create(const char *scene, const char *workspace, char *error, int errorSize) {
    try {
        return new NativeWorld(uipc::Json::parse(scene), workspace);
    } catch (const std::exception &e) {
        errorText(error, errorSize, e.what());
        return nullptr;
    }
}
bool bagua_hsc_native_step(void *handle, int count, char *error, int errorSize) {
    try {
        require(handle && count >= 1 && count <= 2400, "Invalid HSC step request");
        auto &w = *static_cast<NativeWorld *>(handle);
        for (int i = 0; i < count; ++i) {
            require(w.world->is_valid(), "HSC world is invalid; reset before continuing");
            w.world->advance();
            w.world->sync();
            if (!w.world->is_valid())
                throw std::runtime_error("HSC step failed: " + w.engine->status().to_json().dump());
        }
        return true;
    } catch (const std::exception &e) {
        errorText(error, errorSize, e.what());
        return false;
    }
}
const char *bagua_hsc_native_state(void *handle, char *error, int errorSize) {
    try {
        require(handle, "Null HSC world");
        return static_cast<NativeWorld *>(handle)->snapshot();
    } catch (const std::exception &e) {
        errorText(error, errorSize, e.what());
        return nullptr;
    }
}
void bagua_hsc_native_destroy(void *handle) { delete static_cast<NativeWorld *>(handle); }
}
