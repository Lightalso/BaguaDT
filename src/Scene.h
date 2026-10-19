#pragma once
#include <QColor>
#include <QJsonObject>
#include <QJsonArray>
#include <QMatrix4x4>
#include <QObject>
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
    std::shared_ptr<const MeshData> mesh;
    QMatrix4x4 matrix() const;
    QJsonObject json() const;
};

// Editor data is independent of renderer objects and physics-engine objects.
class Scene : public QObject {
    Q_OBJECT
public:
    explicit Scene(QObject *parent = nullptr);
    QVector<Object> objects;
    QString selected;
    QUndoStack undo;
    QJsonObject view;
    QJsonArray joints;
    Object *current();
    const Object *current() const;
    Object *find(const QString &id);
    const Object *find(const QString &id) const;
    void select(const QString &id);
    QString create(Shape shape);
    void removeSelected();
    void duplicateSelected();
    void edit(const Object &object, const QString &label);
    void preview(const Object &object);
    void finishPreview(const Object &before, const QString &label);
    void replace(QVector<Object> items, const QString &selection, const QString &label);
    void reset(bool example = true);
    QJsonObject json() const;
    bool save(const QString &path, QString *error = nullptr);
    bool load(const QString &path, QString *error = nullptr);
    bool loadJson(const QJsonObject &root, QString *error = nullptr);
    void notify();
signals:
    void changed();
    void selectionChanged();
};
} // namespace bagua
