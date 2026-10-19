#include "Scene.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuaternion>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace bagua {
QString shapeKey(Shape s) { return QStringList{"box", "sphere", "cylinder", "plane", "mesh"}.at(int(s)); }
QString shapeName(Shape s) {
    return QStringList{QStringLiteral("立方体"), QStringLiteral("球体"), QStringLiteral("圆柱"), QStringLiteral("平面"),
                       QStringLiteral("网格")}
        .at(int(s));
}
QMatrix4x4 Object::matrix() const {
    QMatrix4x4 m;
    m.translate(position);
    m.rotate(QQuaternion::fromEulerAngles(rotation));
    m.scale(scale * dimensions);
    return m;
}
static QJsonArray vec(QVector3D v) { return {v.x(), v.y(), v.z()}; }
QJsonObject Object::json() const {
    QJsonObject result{{"id", id},
                       {"name", name},
                       {"shape", shapeKey(shape)},
                       {"position", vec(position)},
                       {"rotation", vec(rotation)},
                       {"scale", vec(scale)},
                       {"dimensions", vec(dimensions)},
                       {"linearVelocity",vec(linearVelocity)},
                       {"angularVelocity",vec(angularVelocity)},
                       {"color", color.name(QColor::HexRgb)},
                       {"visible", visible},
                       {"collisionEnabled", collisionEnabled},
                       {"fixed", fixed},
                       {"mass", mass}};
    if (mesh)
        result.insert("mesh", mesh->id);
    if (deformable)
        result.insert("fem", QJsonObject{{"youngModulus", youngModulus},
                                         {"poissonRatio", poissonRatio},
                                         {"resolution", femResolution},
                                         {"pinBottom", pinBottom},
                                         {"attachmentBody", femAttachmentBody},
                                         {"attachmentStiffness", attachmentStiffness}});
    return result;
}
class Change final : public QUndoCommand {
public:
    Change(Scene *s, QVector<Object> before, QString selBefore, QVector<Object> after, QString selAfter, QString text)
        : s(s), before(std::move(before)), after(std::move(after)), selBefore(std::move(selBefore)),
          selAfter(std::move(selAfter)) {
        setText(text);
    }
    void undo() override { apply(before, selBefore); }
    void redo() override { apply(after, selAfter); }

private:
    void apply(const QVector<Object> &items, const QString &id) {
        s->objects = items;
        s->selected = id;
        s->notify();
    }
    Scene *s;
    QVector<Object> before, after;
    QString selBefore, selAfter;
};
Scene::Scene(QObject *p) : QObject(p), undo(this) {}
Object *Scene::find(const QString &id) {
    for (auto &o : objects)
        if (o.id == id)
            return &o;
    return nullptr;
}
const Object *Scene::find(const QString &id) const {
    for (const auto &o : objects)
        if (o.id == id)
            return &o;
    return nullptr;
}
Object *Scene::current() { return find(selected); }
const Object *Scene::current() const { return find(selected); }
void Scene::notify() {
    emit changed();
    emit selectionChanged();
}
void Scene::select(const QString &id) {
    const QString valid = find(id) ? id : QString{};
    if (selected == valid)
        return;
    selected = valid;
    emit selectionChanged();
}
void Scene::replace(QVector<Object> items, const QString &sel, const QString &label) {
    undo.push(new Change(this, objects, selected, std::move(items), sel, label));
}
QString Scene::create(Shape shape) {
    Object o;
    o.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    o.shape = shape;
    int n = 1;
    do {
        o.name = shapeName(shape) + QString(" %1").arg(n++);
    } while (std::any_of(objects.cbegin(), objects.cend(), [&](const Object &x) { return x.name == o.name; }));
    if (shape == Shape::Plane) {
        o.fixed = true;
        o.position = {0, 0, 0};
        o.dimensions = {6, 1, 6};
        o.color = QColor("#d1d6dd");
    }
    auto next = objects;
    next.push_back(o);
    replace(next, o.id, QStringLiteral("创建") + shapeName(shape));
    return o.id;
}
void Scene::removeSelected() {
    if (!current())
        return;
    auto next = objects;
    for (int i = 0; i < next.size(); ++i)
        if (next[i].id == selected) {
            next.removeAt(i);
            break;
        }
    replace(next, {}, QStringLiteral("删除对象"));
}
void Scene::duplicateSelected() {
    if (!current())
        return;
    Object o = *current();
    o.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    o.name += QStringLiteral(" 副本");
    o.position += QVector3D(.6f, 0, .6f);
    auto next = objects;
    next.push_back(o);
    replace(next, o.id, QStringLiteral("复制对象"));
}
void Scene::edit(const Object &o, const QString &label) {
    auto next = objects;
    for (auto &x : next)
        if (x.id == o.id) {
            if (x.json() == o.json())
                return;
            x = o;
            replace(next, selected, label);
            return;
        }
}
void Scene::preview(const Object &o) {
    if (auto *p = find(o.id)) {
        *p = o;
        emit changed();
    }
}
void Scene::finishPreview(const Object &before, const QString &label) {
    auto *p = find(before.id);
    if (!p || p->json() == before.json())
        return;
    // Copy the object before restoring the preview. Holding a pointer into a
    // shared QVector while copying that vector would mutate both snapshots.
    const Object after = *p;
    *p = before;
    edit(after, label);
}
void Scene::reset(bool example) {
    joints = {};
    objects.clear();
    selected.clear();
    undo.clear();
    view = {};
    if (example) {
        Object floor;
        floor.id = "ground";
        floor.name = QStringLiteral("地面");
        floor.shape = Shape::Plane;
        floor.fixed = true;
        floor.position = {0, -.02f, 0};
        floor.dimensions = {12, 1, 12};
        floor.color = QColor("#e3e7ec");
        objects.push_back(floor);
        Object box;
        box.id = "box-01";
        box.name = QStringLiteral("立方体 1");
        box.position = {0, .75f, 0};
        box.dimensions = {1.5f, 1.5f, 1.5f};
        box.rotation = {0, 22, 0};
        objects.push_back(box);
        Object sphere;
        sphere.id = "sphere-01";
        sphere.name = QStringLiteral("球体 1");
        sphere.shape = Shape::Sphere;
        sphere.position = {-2, .65f, .2f};
        sphere.dimensions = {1.3f, 1.3f, 1.3f};
        sphere.color = QColor("#e5ad63");
        objects.push_back(sphere);
        Object cylinder;
        cylinder.id = "cylinder-01";
        cylinder.name = QStringLiteral("圆柱 1");
        cylinder.shape = Shape::Cylinder;
        cylinder.position = {2, .9f, -.5f};
        cylinder.dimensions = {1.1f, 1.8f, 1.1f};
        cylinder.color = QColor("#809fe8");
        objects.push_back(cylinder);
        selected = box.id;
    }
    undo.setClean();
    notify();
}
QJsonObject Scene::json() const {
    QJsonArray a;
    QJsonObject assets;
    for (const auto &o : objects) {
        a.append(o.json());
        if (o.mesh && !assets.contains(o.mesh->id))
            assets.insert(o.mesh->id, meshJson(*o.mesh));
    }
    return {{"format", "baguadt.scene"}, {"version", 1}, {"units", "m"}, {"upAxis", "Y"},
            {"selected", selected},      {"view", view}, {"objects", a}, {"assets", assets}, {"joints", joints}};
}
bool Scene::save(const QString &path, QString *error) {
    QSaveFile file(path);
    const auto bytes = QJsonDocument(json()).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    undo.setClean();
    return true;
}
bool Scene::load(const QString &path, QString *error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    if (f.size() > 256 * 1024 * 1024) {
        if (error)
            *error = QStringLiteral("场景文件超过 256 MB 限制");
        return false;
    }
    QJsonParseError pe;
    const auto d = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !d.isObject()) {
        if (error)
            *error = QStringLiteral("无效 JSON：") + pe.errorString();
        return false;
    }
    return loadJson(d.object(), error);
}
bool Scene::loadJson(const QJsonObject &root, QString *error) {
    auto fail = [&](QString message) {
        if (error)
            *error = message;
        return false;
    };
    if (root["format"] != "baguadt.scene" || root["version"].toInt(-1) != 1 || root["units"] != "m" ||
        root["upAxis"] != "Y" || !root["objects"].isArray())
        return fail(QStringLiteral("不支持的场景格式、版本或单位"));
    const auto a = root["objects"].toArray();
    if (a.size() > 10000)
        return fail(QStringLiteral("对象数量超过限制"));
    QVector<Object> next;
    QHash<QString, std::shared_ptr<const MeshData>> assets;
    qsizetype totalVertices = 0, totalIndices = 0;
    const auto assetJson = root.value("assets").toObject();
    for (auto it = assetJson.begin(); it != assetJson.end(); ++it) {
        auto mesh = readMesh(it.value().toObject(), error);
        if (!mesh || mesh->id != it.key())
            return fail(QStringLiteral("网格资产无效：") + it.key());
        totalVertices += mesh->positions.size();
        totalIndices += mesh->indices.size();
        if (totalVertices > 2000000 || totalIndices > 12000000)
            return fail(QStringLiteral("场景网格数量超出限制"));
        assets.insert(it.key(), mesh);
    }
    QSet<QString> ids;
    for (const auto &v : a) {
        if (!v.isObject())
            return fail(QStringLiteral("对象格式错误"));
        auto j = v.toObject();
        Object o;
        o.id = j["id"].toString();
        o.name = j["name"].toString();
        if (o.id.isEmpty() || ids.contains(o.id) || o.name.trimmed().isEmpty())
            return fail(QStringLiteral("对象 ID 重复、为空或名称为空"));
        ids.insert(o.id);
        const int type = QStringList{"box", "sphere", "cylinder", "plane", "mesh"}.indexOf(j["shape"].toString());
        if (type < 0)
            return fail(QStringLiteral("未知几何类型"));
        o.shape = Shape(type);
        if (o.shape == Shape::Mesh) {
            o.mesh = assets.value(j.value("mesh").toString());
            if (!o.mesh)
                return fail(QStringLiteral("缺少网格资产"));
        }
        auto readVec = [&](const char *key, QVector3D &result, bool positive) {
            auto ar = j[key].toArray();
            if (ar.size() != 3)
                return false;
            float values[3];
            for (int k = 0; k < 3; ++k) {
                double d = ar[k].toDouble(std::numeric_limits<double>::quiet_NaN());
                if (!std::isfinite(d) || std::abs(d) > 1e6 || (positive && d < .001))
                    return false;
                values[k] = float(d);
            }
            result = {values[0], values[1], values[2]};
            return true;
        };
        if (!readVec("position", o.position, false) || !readVec("rotation", o.rotation, false) ||
            !readVec("scale", o.scale, true) || !readVec("dimensions", o.dimensions, true))
            return fail(QStringLiteral("变换或尺寸不合法"));
        o.color = QColor(j["color"].toString());
        if (!o.color.isValid() || !j["visible"].isBool())
            return fail(QStringLiteral("颜色或可见性不合法"));
        o.visible = j["visible"].toBool();
        if ((j.contains("collisionEnabled") && !j["collisionEnabled"].isBool()) ||
            (j.contains("fixed") && !j["fixed"].isBool()) ||
            (j.contains("mass") && (!j["mass"].isDouble() || !std::isfinite(j["mass"].toDouble()) ||
                                    j["mass"].toDouble() < .001 || j["mass"].toDouble() > 1e9)))
            return fail(QStringLiteral("刚体参数不合法"));
        if ((j.contains("linearVelocity") && !readVec("linearVelocity",o.linearVelocity,false)) ||
            (j.contains("angularVelocity") && !readVec("angularVelocity",o.angularVelocity,false)))return fail(QStringLiteral("初始速度无效"));
        o.collisionEnabled = j["collisionEnabled"].toBool(true);
        o.fixed = o.shape == Shape::Plane || j["fixed"].toBool(false);
        o.mass = j["mass"].toDouble(1.0);
        if (j.contains("fem")) {
            if (!j.value("fem").isObject())
                return fail(QStringLiteral("FEM 参数无效"));
            auto fem = j.value("fem").toObject();
            o.deformable = true;
            o.youngModulus = fem.value("youngModulus").toDouble(-1);
            o.poissonRatio = fem.value("poissonRatio").toDouble(-1);
            o.femAttachmentBody = fem.value("attachmentBody").toString();
            o.attachmentStiffness = fem.value("attachmentStiffness").toDouble(20000);
            if ((fem.contains("attachmentBody") && !fem.value("attachmentBody").isString()) ||
                !std::isfinite(o.attachmentStiffness) || o.attachmentStiffness < 1 || o.attachmentStiffness > 1e8)
                return fail(QStringLiteral("FEM 连接参数无效"));
            o.femResolution = fem.value("resolution").toInt(-1);
            o.pinBottom = fem.value("pinBottom").toBool(true);
            if (!std::isfinite(o.youngModulus) || o.youngModulus < 10 || o.youngModulus > 1e12 ||
                !std::isfinite(o.poissonRatio) || o.poissonRatio < 0 || o.poissonRatio >= .49 || o.femResolution < 1 ||
                o.femResolution > 6 || (fem.contains("pinBottom") && !fem.value("pinBottom").isBool()))
                return fail(QStringLiteral("FEM 材料或网格划分参数无效"));
        }
        next.push_back(o);
    }
    const auto camera = root["view"].toObject();
    if (!camera.isEmpty()) {
        const auto t = camera["target"].toArray();
        if (t.size() != 3)
            return fail(QStringLiteral("相机目标无效"));
        for (const auto &x : t)
            if (!x.isDouble() || !std::isfinite(x.toDouble()) || std::abs(x.toDouble()) > 1e6)
                return fail(QStringLiteral("相机目标无效"));
        for (const char *k : {"yaw", "pitch", "distance"})
            if (!camera[k].isDouble() || !std::isfinite(camera[k].toDouble()) || std::abs(camera[k].toDouble()) > 1e6)
                return fail(QStringLiteral("相机参数无效"));
    }
    QJsonArray nextJoints;
    if (root.contains("joints")) {
        if (!root["joints"].isArray()) return fail(QStringLiteral("关节列表格式错误"));
        nextJoints=root["joints"].toArray();
        if (nextJoints.size()>10000) return fail(QStringLiteral("关节数量超限"));
        for (auto value:nextJoints) {
            if (!value.isObject()) return fail(QStringLiteral("关节必须是对象"));
            auto j=value.toObject();auto type=j["type"].toString();
            if (type!="ball" && type!="hinge" && type!="prismatic" && type!="rotation") return fail(QStringLiteral("不支持的关节类型"));
            QString a=j["bodyA"].toString(),b=j["bodyB"].toString();
            if (a.isEmpty() || b.isEmpty() || a==b) return fail(QStringLiteral("关节连接对象无效"));
            for (auto id:{a,b}) if (std::none_of(next.cbegin(),next.cend(),[&](const Object &o){return o.id==id && !o.deformable;})) return fail(QStringLiteral("关节必须引用两个已有刚体"));
            for (auto field:QStringList{"anchor",type=="ball"?"anchor":"axis"}) {
                auto vector=j[field].toArray();if (vector.size()!=3) return fail(QStringLiteral("关节坐标或轴无效"));
                double length=0;for(auto v:vector){if(!v.isDouble() || !std::isfinite(v.toDouble()))return fail(QStringLiteral("关节数值无效"));length+=v.toDouble()*v.toDouble();}
                if(field=="axis" && length<1e-20)return fail(QStringLiteral("关节轴不能为零"));
            }
            if (j.contains("strength") && (!j["strength"].isDouble() || !std::isfinite(j["strength"].toDouble()) || j["strength"].toDouble()<=0)) return fail(QStringLiteral("关节强度无效"));
            if (j.contains("angleDegrees") && (!j["angleDegrees"].isDouble() || !std::isfinite(j["angleDegrees"].toDouble()))) return fail(QStringLiteral("旋转约束角度无效"));
            if (j.contains("angularVelocityDegrees") && (!j["angularVelocityDegrees"].isDouble() || !std::isfinite(j["angularVelocityDegrees"].toDouble()))) return fail(QStringLiteral("旋转约束角速度无效"));
        }
    }
    objects = std::move(next);
    joints = nextJoints;
    view = camera;
    selected = root["selected"].toString();
    if (!find(selected))
        selected.clear();
    undo.clear();
    undo.setClean();
    notify();
    return true;
}
} // namespace bagua
