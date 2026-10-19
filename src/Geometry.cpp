#include "Geometry.h"
#include <array>
#include <cmath>
#include <limits>
namespace bagua {
static constexpr float pi = 3.14159265358979323846f;
QVector<Vertex> geometry(Shape s) {
    QVector<Vertex> out;
    auto tri = [&](QVector3D a, QVector3D b, QVector3D c) {
        auto n = QVector3D::crossProduct(b - a, c - a).normalized();
        out << Vertex{a, n} << Vertex{b, n} << Vertex{c, n};
    };
    auto quad = [&](QVector3D a, QVector3D b, QVector3D c, QVector3D d) {
        tri(a, b, c);
        tri(a, c, d);
    };
    if (s == Shape::Plane) {
        quad({-.5f, 0, -.5f}, {-.5f, 0, .5f}, {.5f, 0, .5f}, {.5f, 0, -.5f});
        return out;
    }
    if (s == Shape::Box) {
        quad({-.5f, -.5f, .5f}, {.5f, -.5f, .5f}, {.5f, .5f, .5f}, {-.5f, .5f, .5f});
        quad({.5f, -.5f, -.5f}, {-.5f, -.5f, -.5f}, {-.5f, .5f, -.5f}, {.5f, .5f, -.5f});
        quad({.5f, -.5f, .5f}, {.5f, -.5f, -.5f}, {.5f, .5f, -.5f}, {.5f, .5f, .5f});
        quad({-.5f, -.5f, -.5f}, {-.5f, -.5f, .5f}, {-.5f, .5f, .5f}, {-.5f, .5f, -.5f});
        quad({-.5f, .5f, .5f}, {.5f, .5f, .5f}, {.5f, .5f, -.5f}, {-.5f, .5f, -.5f});
        quad({-.5f, -.5f, -.5f}, {.5f, -.5f, -.5f}, {.5f, -.5f, .5f}, {-.5f, -.5f, .5f});
        return out;
    }
    const int segments = 40, rings = 24;
    if (s == Shape::Cylinder) {
        for (int i = 0; i < segments; ++i) {
            float a = 2 * pi * i / segments, b = 2 * pi * ((i + 1) % segments) / segments;
            QVector3D p{.5f * cosf(a), -.5f, .5f * sinf(a)}, q{.5f * cosf(b), -.5f, .5f * sinf(b)},
                r = q + QVector3D(0, 1, 0), t = p + QVector3D(0, 1, 0);
            tri({0, -.5f, 0}, p, q);
            tri({0, .5f, 0}, r, t);
            auto n1 = QVector3D(p.x(), 0, p.z()).normalized(), n2 = QVector3D(q.x(), 0, q.z()).normalized();
            out << Vertex{p, n1} << Vertex{t, n1} << Vertex{r, n2} << Vertex{p, n1} << Vertex{r, n2} << Vertex{q, n2};
        }
        return out;
    }
    // Snap the poles: sinf(pi) is about 1e-8 instead of 0, so the south cap would not weld and
    // the surface would be open. Dynamic bodies (ABD/FEM) require a watertight triangle mesh.
    auto point = [](float a, float b) {
        if (std::abs(sinf(a)) < 1e-6f)
            return QVector3D(0, cosf(a) >= 0 ? .5f : -.5f, 0);
        return QVector3D(.5f * sinf(a) * cosf(b), .5f * cosf(a), .5f * sinf(a) * sinf(b));
    };
    auto smooth = [&](QVector3D a, QVector3D b, QVector3D c) {
        out << Vertex{a, a.normalized()} << Vertex{b, b.normalized()} << Vertex{c, c.normalized()};
    };
    for (int j = 0; j < rings; ++j)
        for (int i = 0; i < segments; ++i) {
            // The last column reuses the first meridian exactly so the longitude seam welds too.
            auto a = point(pi * j / rings, 2 * pi * i / segments),
                 b = point(pi * (j + 1) / rings, 2 * pi * i / segments),
                 c = point(pi * (j + 1) / rings, 2 * pi * ((i + 1) % segments) / segments),
                 d = point(pi * j / rings, 2 * pi * ((i + 1) % segments) / segments);
            // At the poles a == d and b == c, so that half of the quad is a zero-area triangle.
            // Skipping it keeps the cap watertight without degenerate faces.
            if (j != 0)
                smooth(a, d, c);
            if (j != rings - 1)
                smooth(a, c, b);
        }
    return out;
}
QVector<Vertex> geometry(const Object &o) {
    if (o.shape != Shape::Mesh || !o.mesh)
        return geometry(o.shape);
    QVector<Vertex> result;
    result.reserve(o.mesh->indices.size());
    for (qsizetype i = 0; i < o.mesh->indices.size(); i += 3) {
        auto a = o.mesh->positions[o.mesh->indices[i]], b = o.mesh->positions[o.mesh->indices[i + 1]],
             c = o.mesh->positions[o.mesh->indices[i + 2]];
        auto n = QVector3D::crossProduct(b - a, c - a).normalized();
        result << Vertex{a, n} << Vertex{b, n} << Vertex{c, n};
    }
    return result;
}
std::pair<QVector3D, QVector3D> localBounds(const Object &o) {
    if (o.shape != Shape::Mesh || !o.mesh)
        return {{-.5f, o.shape == Shape::Plane ? 0.f : -.5f, -.5f}, {.5f, o.shape == Shape::Plane ? 0.f : .5f, .5f}};
    QVector3D lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
    for (auto p : o.mesh->positions)
        for (int a = 0; a < 3; ++a) {
            lo[a] = std::min(lo[a], p[a]);
            hi[a] = std::max(hi[a], p[a]);
        }
    return {lo, hi};
}
float intersect(const Object &o, QVector3D origin, QVector3D dir) {
    static const std::array<QVector<Vertex>, 4> meshes = {geometry(Shape::Box), geometry(Shape::Sphere),
                                                          geometry(Shape::Cylinder), geometry(Shape::Plane)};
    bool ok;
    auto inverse = o.matrix().inverted(&ok);
    if (!ok)
        return std::numeric_limits<float>::infinity();
    const auto ro = inverse.map(origin), rd = inverse.mapVector(dir);
    float nearest = std::numeric_limits<float>::infinity();
    const auto imported = o.shape == Shape::Mesh ? geometry(o) : QVector<Vertex>{};
    const auto &mesh = o.shape == Shape::Mesh ? imported : meshes[int(o.shape)];
    for (int i = 0; i < mesh.size(); i += 3) {
        auto a = mesh[i].position, b = mesh[i + 1].position, c = mesh[i + 2].position;
        auto e1 = b - a, e2 = c - a, p = QVector3D::crossProduct(rd, e2);
        float det = QVector3D::dotProduct(e1, p);
        if (std::abs(det) < 1e-9f)
            continue;
        float inv = 1 / det;
        auto t = ro - a;
        float u = QVector3D::dotProduct(t, p) * inv;
        if (u < 0 || u > 1)
            continue;
        auto q = QVector3D::crossProduct(t, e1);
        float v = QVector3D::dotProduct(rd, q) * inv;
        if (v < 0 || u + v > 1)
            continue;
        float distance = QVector3D::dotProduct(e2, q) * inv;
        if (distance > 0 && distance < nearest)
            nearest = distance;
    }
    return nearest;
}
} // namespace bagua
