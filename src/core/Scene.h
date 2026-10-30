#pragma once
#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QObject>
#include <QPair>
#include <QUndoStack>
#include <QVector3D>
#include <QVector>
#include <memory>

namespace bagua {
enum class Shape { Box, Sphere, Cylinder, Plane, Mesh };
struct MeshData {
    QString id, name, source, sha256;
    QVector<QVector3D> positions;
    QVector<quint32> indices;
    QVector<quint32> tetrahedra;
    QVector<quint32> frameVertices;
    QMatrix4x4 frameRestInverse;
};
QJsonObject meshJson(const MeshData &mesh);
std::shared_ptr<const MeshData> readMesh(const QJsonObject &json, QString *error = nullptr);
QString shapeKey(Shape shape);
QString shapeName(Shape shape);
struct Object {
    QString id, name;
    Shape shape = Shape::Box;
    QVector3D position{0, .5f, 0}, rotation{}, scale{1, 1, 1}, dimensions{1, 1, 1};
    QVector3D linearVelocity{}, angularVelocity{};
    QColor color{"#53b8b0"};
    bool visible = true;
    bool collisionEnabled = true, fixed = false;
    double mass = 1.0;
    bool deformable = false, pinBottom = true;
    double youngModulus = 5000, poissonRatio = .3;
    int femResolution = 2;
    QString femAttachmentBody;
    double attachmentStiffness = 20000;
    // Body-local virtual anchor vertices. They carry no surface, volume or contact; they exist
    // so vertex-level links (springs, bead stitches) have something to attach to.
    QVector<QVector3D> anchors;
    std::shared_ptr<const MeshData> mesh;
    QMatrix4x4 matrix() const;
    // Anchors use body-local metres. Dimensions size geometry; scale also scales anchors.
    QMatrix4x4 anchorMatrix() const;
    QJsonObject json() const;
};

// A link joins two objects. Joints are kinematic (libuipc affine-body constitutions), stitches
// are soft vertex springs (FEM/ABD vertices). Anchors are body-local; `axis` is a world
// direction taken at rest.
struct Link {
    enum class Type { Spherical, Revolute, Prismatic, Fixed, Rotation, Stitch };
    QString id;
    Type type = Type::Revolute;
    QString a, b;
    QVector3D anchorA{}, anchorB{};
    QVector3D axis{0, 0, 1};
    double strength = 100; // libuipc strength ratio: K = ratio * (mass_a + mass_b)
    bool hasLimit = false;
    double lower = 0, upper = 0, limitStrength = 100;
    double angle = 0, angularVelocity = 0; // legacy "rotation" drive, radians
    QVector<QPair<int, int>> pairs;        // stitch: (anchor ordinal on a, anchor/vertex on b)
    bool anchorPairs = false;              // stitch: pair[1] is an anchor ordinal on b, else a vertex
    bool driven = false, damped = false;
    double driveSpeed = 0, driveRamp = .5, driveStrength = 1e4, damping = 0;
    double stiffness = 20000, rest = 0; // stitch
    bool kinematic() const { return type != Type::Stitch; }
    bool operator==(const Link &other) const;
    bool operator!=(const Link &other) const { return !(*this == other); }
    QString typeKey() const;
    static bool typeFromKey(const QString &key, Type &out);
};
QJsonObject linkJson(const Link &link);

// Per-link runtime diagnostics reported by the backends each step.
struct LinkStat {
    QString id, type;
    double distance = 0, angle = 0, error = 0, aim = 0, velocity = 0, force = 0;
    bool hasDistance = false, hasAngle = false;
    double limitLower = 0, limitUpper = 0;
    bool hasLimit = false;
};

class Scene : public QObject {
    Q_OBJECT
public:
    explicit Scene(QObject *parent = nullptr);
    QVector<Object> objects;
    QVector<Link> links;
    QVector<QPair<QString, QString>> exclusions;
    double timeStep = 1.0 / 240.0;
    QString selected;
    QUndoStack undo;
    QJsonObject view, simulation;
    Object *current();
    const Object *current() const;
    Object *find(const QString &id);
    const Object *find(const QString &id) const;
    void select(const QString &id);
    QString create(Shape shape);
    void removeSelected();
    void removeAnchor(const QString &objectId, int index);
    void duplicateSelected();
    void edit(const Object &object, const QString &label);
    void preview(const Object &object);
    void finishPreview(const Object &before, const QString &label);
    void replace(QVector<Object> items, const QString &selection, const QString &label);
    void editLinks(const QVector<Link> &links, const QVector<QPair<QString, QString>> &exclusions,
                   const QString &label);
    // Replace objects, links and contact exclusions as one undoable operation.
    // Used by component import and the vehicle assembly wizard so a multi-link
    // assembly cannot be left half-applied by undo.
    void editAssembly(QVector<Object> items, const QVector<Link> &links,
                      const QVector<QPair<QString, QString>> &exclusions, const QString &selection,
                      const QString &label);
    void reset(bool example = true);
    QJsonObject json() const;
    bool save(const QString &path, QString *error = nullptr);
    bool load(const QString &path, QString *error = nullptr);
    bool loadJson(const QJsonObject &root, QString *error = nullptr);
    void notify();
    // Used by the simulation display snapshot: repaint without pretending the
    // selection changed, because the worker updates this scene many times a second.
    void notifyChanged();
signals:
    void changed();
    void selectionChanged();
};
} // namespace bagua
