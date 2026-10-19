#pragma once
#include "Scene.h"
namespace bagua {
struct Vertex {
    QVector3D position, normal;
};
QVector<Vertex> geometry(Shape shape);
QVector<Vertex> geometry(const Object &object);
std::pair<QVector3D, QVector3D> localBounds(const Object &object);
// Intersection uses actual triangles, not overlapping screen rectangles.
float intersect(const Object &object, QVector3D origin, QVector3D direction);
} // namespace bagua
