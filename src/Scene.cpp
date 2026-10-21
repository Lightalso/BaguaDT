#include "Scene.h"
#include <QFile>
#include <QHash>
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
QMatrix4x4 Object::anchorMatrix() const {
    if (mesh && mesh->frameVertices.size() == 4) {
        QMatrix4x4 basis;
        auto p = mesh->positions[mesh->frameVertices[0]];
        basis.setColumn(3, QVector4D(p, 1));
        for (int i = 0; i < 3; ++i)
            basis.setColumn(i, QVector4D(mesh->positions[mesh->frameVertices[i + 1]] - p, 0));
        QMatrix4x4 scaling;
        scaling.scale(scale);
        return matrix() * basis * mesh->frameRestInverse * scaling;
    }
    QMatrix4x4 m;
    m.translate(position);
    m.rotate(QQuaternion::fromEulerAngles(rotation));
    m.scale(scale);
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
                       {"linearVelocity", vec(linearVelocity)},
                       {"angularVelocity", vec(angularVelocity)},
                       {"color", color.name(QColor::HexRgb)},
                       {"visible", visible},
                       {"collisionEnabled", collisionEnabled},
                       {"fixed", fixed},
                       {"mass", mass}};
    if (mesh)
        result.insert("mesh", mesh->id);
    if (!anchors.isEmpty()) {
        QJsonArray array;
        for (const auto &anchor : anchors)
            array.append(vec(anchor));
        result.insert("anchors", array);
    }
    if (deformable)
        result.insert("fem", QJsonObject{{"youngModulus", youngModulus},
                                         {"poissonRatio", poissonRatio},
                                         {"resolution", femResolution},
                                         {"pinBottom", pinBottom},
                                         {"attachmentBody", femAttachmentBody},
                                         {"attachmentStiffness", attachmentStiffness}});
    return result;
}
bool Link::operator==(const Link &other) const {
    return id == other.id && type == other.type && a == other.a && b == other.b && anchorA == other.anchorA &&
           anchorB == other.anchorB && axis == other.axis && strength == other.strength && hasLimit == other.hasLimit &&
           lower == other.lower && upper == other.upper && limitStrength == other.limitStrength &&
           angle == other.angle && angularVelocity == other.angularVelocity && driven == other.driven &&
           damped == other.damped && driveSpeed == other.driveSpeed && driveRamp == other.driveRamp &&
           driveStrength == other.driveStrength && damping == other.damping && pairs == other.pairs &&
           anchorPairs == other.anchorPairs && stiffness == other.stiffness && rest == other.rest;
}
QString Link::typeKey() const {
    switch (type) {
    case Type::Spherical:
        return QStringLiteral("spherical");
    case Type::Revolute:
        return QStringLiteral("revolute");
    case Type::Prismatic:
        return QStringLiteral("prismatic");
    case Type::Fixed:
        return QStringLiteral("fixed");
    case Type::Rotation:
        return QStringLiteral("rotation");
    case Type::Stitch:
        return QStringLiteral("stitch");
    }
    return QStringLiteral("revolute");
}
bool Link::typeFromKey(const QString &key, Type &out) {
    static const QHash<QString, Type> types{{"spherical", Type::Spherical}, {"revolute", Type::Revolute},
                                            {"prismatic", Type::Prismatic}, {"fixed", Type::Fixed},
                                            {"rotation", Type::Rotation},   {"stitch", Type::Stitch},
                                            {"spring", Type::Stitch}};
    const auto it = types.constFind(key);
    if (it == types.cend())
        return false;
    out = it.value();
    return true;
}
QJsonObject linkJson(const Link &link) {
    QJsonObject result{{"id", link.id}, {"type", link.typeKey()}, {"a", link.a}, {"b", link.b}};
    if (link.type == Link::Type::Stitch) {
        QJsonArray pairs;
        for (const auto &pair : link.pairs)
            pairs.append(QJsonArray{pair.first, pair.second});
        result.insert("pairs", pairs);
        result.insert("stiffness", link.stiffness);
        result.insert("rest", link.rest);
        if (link.anchorPairs)
            result.insert("anchorPairs", true);
        return result;
    }
    result.insert("anchorA", vec(link.anchorA));
    result.insert("anchorB", vec(link.anchorB));
    result.insert("strength", link.strength);
    if (link.type == Link::Type::Revolute || link.type == Link::Type::Prismatic || link.type == Link::Type::Rotation)
        result.insert("axis", vec(link.axis));
    if (link.hasLimit)
        result.insert("limit",
                      QJsonObject{{"lower", link.lower}, {"upper", link.upper}, {"strength", link.limitStrength}});
    if (link.type == Link::Type::Rotation) {
        result.insert("angle", link.angle);
        result.insert("angularVelocity", link.angularVelocity);
    }
    if (link.driven)
        result["drive"] =
            QJsonObject{{"speed", link.driveSpeed}, {"ramp", link.driveRamp}, {"strength", link.driveStrength}};
    if (link.damped)
        result["damper"] = QJsonObject{{"coefficient", link.damping}};
    return result;
}
class Change final : public QUndoCommand {
public:
    Change(Scene *s, QVector<Object> before, QString selBefore, QVector<Object> after, QString selAfter, QString text)
        : s(s), before(std::move(before)), after(std::move(after)), selBefore(std::move(selBefore)),
          selAfter(std::move(selAfter)) {
        beforeLinks = s->links;
        afterLinks = beforeLinks;
        beforeExclusions = s->exclusions;
        afterExclusions = beforeExclusions;
        auto exists = [&](const QString &id) {
            return std::any_of(this->after.cbegin(), this->after.cend(), [&](const Object &o) { return o.id == id; });
        };
        afterLinks.erase(std::remove_if(afterLinks.begin(), afterLinks.end(),
                                        [&](const Link &l) { return !exists(l.a) || !exists(l.b); }),
                         afterLinks.end());
        afterExclusions.erase(
            std::remove_if(afterExclusions.begin(), afterExclusions.end(),
                           [&](const auto &pair) { return !exists(pair.first) || !exists(pair.second); }),
            afterExclusions.end());
        for (auto &o : this->after)
            if (!o.femAttachmentBody.isEmpty() && !exists(o.femAttachmentBody))
                o.femAttachmentBody.clear();
        setText(text);
    }
    void undo() override {
        s->links = beforeLinks;
        s->exclusions = beforeExclusions;
        apply(before, selBefore);
    }
    void redo() override {
        s->links = afterLinks;
        s->exclusions = afterExclusions;
        apply(after, selAfter);
    }

private:
    void apply(const QVector<Object> &items, const QString &id) {
        s->objects = items;
        s->selected = id;
        s->notify();
    }
    Scene *s;
    QVector<Object> before, after;
    QVector<Link> beforeLinks, afterLinks;
    QVector<QPair<QString, QString>> beforeExclusions, afterExclusions;
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
namespace {
// Links and contact exclusions are edited as a unit so undo/redo keeps them consistent.
class LinkChange final : public QUndoCommand {
public:
    LinkChange(Scene *s, QVector<Link> beforeLinks, QVector<QPair<QString, QString>> beforeExclusions,
               QVector<Link> afterLinks, QVector<QPair<QString, QString>> afterExclusions, const QString &text)
        : s(s), beforeLinks(std::move(beforeLinks)), beforeExclusions(std::move(beforeExclusions)),
          afterLinks(std::move(afterLinks)), afterExclusions(std::move(afterExclusions)) {
        setText(text);
    }
    void undo() override { apply(beforeLinks, beforeExclusions); }
    void redo() override { apply(afterLinks, afterExclusions); }

private:
    void apply(const QVector<Link> &links, const QVector<QPair<QString, QString>> &exclusions) {
        s->links = links;
        s->exclusions = exclusions;
        s->notify();
    }
    Scene *s;
    QVector<Link> beforeLinks, afterLinks;
    QVector<QPair<QString, QString>> beforeExclusions, afterExclusions;
};
} // namespace
void Scene::editLinks(const QVector<Link> &next, const QVector<QPair<QString, QString>> &nextExclusions,
                      const QString &label) {
    if (next == links && nextExclusions == exclusions)
        return;
    undo.push(new LinkChange(this, links, exclusions, next, nextExclusions, label));
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
void Scene::removeAnchor(const QString &objectId, int index) {
    const auto *o = find(objectId);
    if (!o || index < 0 || index >= o->anchors.size())
        return;
    Object next = *o;
    next.anchors.removeAt(index);
    auto nextLinks = links;
    for (auto &link : nextLinks) {
        if (link.type != Link::Type::Stitch)
            continue;
        for (auto it = link.pairs.begin(); it != link.pairs.end();) {
            bool onA = link.a == objectId, onB = link.b == objectId && link.anchorPairs;
            if ((onA && it->first == index) || (onB && it->second == index))
                it = link.pairs.erase(it);
            else {
                if (onA && it->first > index)
                    --it->first;
                if (onB && it->second > index)
                    --it->second;
                ++it;
            }
        }
    }
    nextLinks.erase(std::remove_if(nextLinks.begin(), nextLinks.end(),
                                   [](const Link &l) { return l.type == Link::Type::Stitch && l.pairs.isEmpty(); }),
                    nextLinks.end());
    undo.beginMacro(QStringLiteral("删除锚点及相关缝合对"));
    editLinks(nextLinks, exclusions, QStringLiteral("更新缝合引用"));
    edit(next, QStringLiteral("删除锚点"));
    undo.endMacro();
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
    links = {};
    exclusions = {};
    timeStep = 1.0 / 240.0;
    simulation = {};
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
    QJsonArray linkArray;
    for (const auto &link : links)
        linkArray.append(linkJson(link));
    QJsonArray exclusionArray;
    for (const auto &pair : exclusions)
        exclusionArray.append(QJsonObject{{"a", pair.first}, {"b", pair.second}});
    return {{"format", "baguadt.scene"},
            {"version", 1},
            {"units", "m"},
            {"upAxis", "Y"},
            {"dt", timeStep},
            {"simulation", simulation},
            {"selected", selected},
            {"view", view},
            {"objects", a},
            {"assets", assets},
            {"links", linkArray},
            {"exclusions", exclusionArray}};
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
        if ((j.contains("linearVelocity") && !readVec("linearVelocity", o.linearVelocity, false)) ||
            (j.contains("angularVelocity") && !readVec("angularVelocity", o.angularVelocity, false)))
            return fail(QStringLiteral("初始速度无效"));
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
        if (j.contains("anchors")) {
            const auto array = j.value("anchors").toArray();
            if (!j.value("anchors").isArray() || array.size() > 4096)
                return fail(QStringLiteral("锚点数量或格式无效"));
            for (const auto &value : array) {
                const auto point = value.toArray();
                if (point.size() != 3)
                    return fail(QStringLiteral("锚点坐标无效"));
                QVector3D anchor;
                for (int k = 0; k < 3; ++k) {
                    const double d = point[k].toDouble(std::numeric_limits<double>::quiet_NaN());
                    if (!std::isfinite(d) || std::abs(d) > 1e6)
                        return fail(QStringLiteral("锚点坐标无效"));
                    anchor[k] = float(d);
                }
                o.anchors.push_back(anchor);
            }
        }
        if (o.deformable && !o.anchors.isEmpty())
            return fail(QStringLiteral("虚拟锚点只能属于刚体；弹性体请引用网格顶点"));
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
    auto readPoint = [&](const QJsonValue &value, QVector3D &out) {
        const auto point = value.toArray();
        if (point.size() != 3)
            return false;
        for (int k = 0; k < 3; ++k) {
            const double d = point[k].toDouble(std::numeric_limits<double>::quiet_NaN());
            if (!std::isfinite(d) || std::abs(d) > 1e6)
                return false;
            out[k] = float(d);
        }
        return true;
    };
    auto findObject = [&](const QString &id) {
        return std::find_if(next.begin(), next.end(), [&](const Object &o) { return o.id == id; });
    };
    double nextTimeStep = 1.0 / 240.0;
    if (root.contains("dt")) {
        const double value = root.value("dt").toDouble(-1);
        if (root.value("dt").isDouble() && std::isfinite(value) && value >= 1.0 / 2000.0 && value <= 0.1)
            nextTimeStep = value;
        else
            return fail(QStringLiteral("时间步长无效（应在 1/2000 至 1/10 秒之间）"));
    }
    QVector<Link> nextLinks;
    if (root.contains("links")) {
        const auto array = root.value("links").toArray();
        if (!root.value("links").isArray() || array.size() > 2000)
            return fail(QStringLiteral("链接列表格式错误或超过上限"));
        QSet<QString> linkIds;
        for (const auto &value : array) {
            if (!value.isObject())
                return fail(QStringLiteral("链接必须是对象"));
            const auto j = value.toObject();
            Link link;
            link.id = j.value("id").toString();
            if (link.id.isEmpty() || linkIds.contains(link.id))
                return fail(QStringLiteral("链接 ID 为空或重复"));
            linkIds.insert(link.id);
            if (!Link::typeFromKey(j.value("type").toString(), link.type))
                return fail(QStringLiteral("不支持的链接类型：") + j.value("type").toString());
            link.a = j.value("a").toString();
            link.b = j.value("b").toString();
            const auto left = findObject(link.a), right = findObject(link.b);
            if (link.a.isEmpty() || link.b.isEmpty() || link.a == link.b || left == next.end() || right == next.end())
                return fail(QStringLiteral("链接引用的对象无效：") + link.id);
            if (link.kinematic() && (left->deformable || right->deformable))
                return fail(QStringLiteral("运动学关节只能连接两个刚体：") + link.id);
            if (link.type == Link::Type::Stitch) {
                if (j.contains("drive") || j.contains("damper"))
                    return fail(QStringLiteral("缝合不支持驱动或轴向阻尼"));
                if (!j.value("pairs").isArray() || j.value("pairs").toArray().isEmpty() ||
                    j.value("pairs").toArray().size() > 100000)
                    return fail(QStringLiteral("缝合链接需要顶点对：") + link.id);
                for (const auto &pairValue : j.value("pairs").toArray()) {
                    const auto pair = pairValue.toArray();
                    if (pair.size() != 2 || !pair[0].isDouble() || !pair[1].isDouble())
                        return fail(QStringLiteral("缝合顶点对无效：") + link.id);
                    const int ai = pair[0].toInt(-1), bi = pair[1].toInt(-1);
                    if (ai < 0 || ai >= left->anchors.size())
                        return fail(QStringLiteral("缝合链接的锚点序号越界：") + link.id);
                    if (bi < 0)
                        return fail(QStringLiteral("缝合链接的顶点序号无效：") + link.id);
                    link.pairs.push_back({ai, bi});
                }
                if (j.contains("anchorPairs") && !j.value("anchorPairs").isBool())
                    return fail(QStringLiteral("anchorPairs 必须为布尔值"));
                link.anchorPairs = j.value("anchorPairs").toBool(false);
                if (left->deformable || (link.anchorPairs && right->deformable))
                    return fail(QStringLiteral("缝合锚点必须属于刚体"));
                if (!link.anchorPairs) {
                    if (!right->deformable)
                        return fail(QStringLiteral("刚体之间的缝合请使用两侧锚点模式；网格顶点模式仅用于弹性体"));
                    int count = right->mesh ? int(right->mesh->positions.size())
                                            : (right->deformable && right->shape == Shape::Box
                                                   ? (right->femResolution + 1) * (right->femResolution + 1) *
                                                         (right->femResolution + 1)
                                                   : 0);
                    for (const auto &pair : link.pairs)
                        if (pair.second >= count)
                            return fail(QStringLiteral("缝合目标顶点越界或没有明确的顶点拓扑"));
                }
                if (link.anchorPairs)
                    for (const auto &pair : link.pairs)
                        if (pair.second >= right->anchors.size())
                            return fail(QStringLiteral("缝合链接的另一侧锚点序号越界：") + link.id);
                link.stiffness = j.value("stiffness").toDouble(0);
                link.rest = j.value("rest").toDouble(0);
                if (!j.value("stiffness").isDouble() || !std::isfinite(link.stiffness) || link.stiffness <= 0 ||
                    (j.contains("rest") && !j["rest"].isDouble()) || !std::isfinite(link.rest) || link.rest < 0)
                    return fail(QStringLiteral("缝合刚度或静止长度无效：") + link.id);
                nextLinks.push_back(link);
                continue;
            }
            if (!readPoint(j.value("anchorA"), link.anchorA) || !readPoint(j.value("anchorB"), link.anchorB))
                return fail(QStringLiteral("链接锚点无效：") + link.id);
            link.strength = j.value("strength").toDouble(100);
            if ((j.contains("strength") && !j["strength"].isDouble()) || !std::isfinite(link.strength) ||
                link.strength <= 0)
                return fail(QStringLiteral("链接强度无效：") + link.id);
            if (link.type == Link::Type::Revolute || link.type == Link::Type::Prismatic ||
                link.type == Link::Type::Rotation) {
                if (!readPoint(j.value("axis"), link.axis) || link.axis.lengthSquared() < 1e-12f)
                    return fail(QStringLiteral("链接轴无效：") + link.id);
            }
            if (j.contains("limit")) {
                if (link.type != Link::Type::Prismatic && link.type != Link::Type::Revolute)
                    return fail(QStringLiteral("该链接类型不支持限位"));
                const auto limit = j.value("limit").toObject();
                link.lower = limit.value("lower").toDouble(0);
                link.upper = limit.value("upper").toDouble(0);
                link.limitStrength = limit.value("strength").toDouble(100);
                if (!limit.value("lower").isDouble() || !limit.value("upper").isDouble() ||
                    !std::isfinite(link.lower) || !std::isfinite(link.upper) || link.lower >= link.upper ||
                    (limit.contains("strength") && !limit["strength"].isDouble()) ||
                    !std::isfinite(link.limitStrength) || link.limitStrength <= 0)
                    return fail(QStringLiteral("链接限位无效：") + link.id);
                link.hasLimit = true;
            }
            if (j.contains("drive")) {
                auto d = j["drive"].toObject();
                if (link.type != Link::Type::Revolute && link.type != Link::Type::Prismatic)
                    return fail(QStringLiteral("驱动仅支持转动副和移动副"));
                link.driven = true;
                link.driveSpeed = d["speed"].toDouble(std::numeric_limits<double>::quiet_NaN());
                link.driveRamp = d["ramp"].toDouble(.5);
                link.driveStrength = d["strength"].toDouble(1e4);
                if ((d.contains("ramp") && !d["ramp"].isDouble()) ||
                    (d.contains("strength") && !d["strength"].isDouble()) || !std::isfinite(link.driveSpeed) ||
                    std::abs(link.driveSpeed) > 1e4 || !std::isfinite(link.driveRamp) || link.driveRamp < 0 ||
                    !std::isfinite(link.driveStrength) || link.driveStrength <= 0)
                    return fail(QStringLiteral("驱动参数无效"));
            }
            if (j.contains("damper")) {
                auto d = j["damper"].toObject();
                if (link.type != Link::Type::Prismatic || !d["coefficient"].isDouble())
                    return fail(QStringLiteral("阻尼仅支持移动副"));
                link.damped = true;
                link.damping = d["coefficient"].toDouble(-1);
                if (!std::isfinite(link.damping) || link.damping < 0 || link.damping > 1e9)
                    return fail(QStringLiteral("阻尼系数无效"));
            }
            if (link.type == Link::Type::Rotation) {
                link.angle = j.value("angle").toDouble(0);
                link.angularVelocity = j.value("angularVelocity").toDouble(0);
                if ((j.contains("angle") && !j["angle"].isDouble()) ||
                    (j.contains("angularVelocity") && !j["angularVelocity"].isDouble()) || !std::isfinite(link.angle) ||
                    !std::isfinite(link.angularVelocity))
                    return fail(QStringLiteral("转动约束参数无效：") + link.id);
            }
            nextLinks.push_back(link);
        }
    } else if (root.contains("joints")) {
        // Version-1 scenes stored joints with world-space anchors and four legacy type names.
        if (!root.value("joints").isArray())
            return fail(QStringLiteral("关节列表格式错误"));
        const auto array = root.value("joints").toArray();
        if (array.size() > 2000)
            return fail(QStringLiteral("关节列表格式错误"));
        for (const auto &value : array) {
            if (!value.isObject())
                return fail(QStringLiteral("关节必须是对象"));
            const auto j = value.toObject();
            Link link;
            const QString type = j.value("type").toString();
            const QHash<QString, Link::Type> legacy{{"ball", Link::Type::Spherical},
                                                    {"hinge", Link::Type::Revolute},
                                                    {"prismatic", Link::Type::Prismatic},
                                                    {"rotation", Link::Type::Rotation}};
            if (!legacy.contains(type))
                return fail(QStringLiteral("不支持的关节类型"));
            link.type = legacy.value(type);
            link.id = j.value("id").toString();
            link.a = j.value("bodyA").toString();
            link.b = j.value("bodyB").toString();
            const auto left = findObject(link.a), right = findObject(link.b);
            if (link.a.isEmpty() || link.b.isEmpty() || link.a == link.b || left == next.end() || right == next.end() ||
                left->deformable || right->deformable)
                return fail(QStringLiteral("关节连接对象无效"));
            if (link.id.isEmpty())
                link.id = link.a + "-" + link.b;
            QVector3D worldAnchor;
            if (!readPoint(j.value("anchor"), worldAnchor))
                return fail(QStringLiteral("关节坐标无效"));
            // v1 anchors were world-space; store them body-local so the link survives moves.
            const auto inverse = left->anchorMatrix().inverted();
            link.anchorA = inverse.map(worldAnchor);
            link.anchorB = right->anchorMatrix().inverted().map(worldAnchor);
            if (!readPoint(j.value("axis"), link.axis) || link.axis.lengthSquared() < 1e-12f)
                return fail(QStringLiteral("关节轴无效"));
            link.strength = j.value("strength").toDouble(100);
            if ((j.contains("strength") && !j["strength"].isDouble()) || !std::isfinite(link.strength) ||
                link.strength <= 0)
                return fail(QStringLiteral("关节强度无效"));
            if (link.type == Link::Type::Rotation) {
                const double deg = j.value("angleDegrees").toDouble(0);
                const double speed = j.value("angularVelocityDegrees").toDouble(0);
                if (!std::isfinite(deg) || !std::isfinite(speed))
                    return fail(QStringLiteral("旋转约束参数无效"));
                link.angle = deg * 3.14159265358979323846 / 180.0;
                link.angularVelocity = speed * 3.14159265358979323846 / 180.0;
            }
            nextLinks.push_back(link);
        }
    }
    QVector<QPair<QString, QString>> nextExclusions;
    if (root.contains("exclusions")) {
        const auto array = root.value("exclusions").toArray();
        if (!root.value("exclusions").isArray() || array.size() > 10000)
            return fail(QStringLiteral("接触排除列表格式错误"));
        for (const auto &value : array) {
            const auto j = value.toObject();
            const QString a = j.value("a").toString(), b = j.value("b").toString();
            if (a.isEmpty() || b.isEmpty() || a == b || findObject(a) == next.end() || findObject(b) == next.end())
                return fail(QStringLiteral("接触排除引用的对象无效"));
            nextExclusions.push_back({a, b});
        }
    }
    QJsonObject nextSimulation;
    if (root.contains("simulation")) {
        if (!root["simulation"].isObject())
            return fail(QStringLiteral("模拟配置必须为对象"));
        nextSimulation = root["simulation"].toObject();
        const QHash<QString, QPair<double, double>> ranges{{"gravity", {0, 100}},
                                                           {"contactDistance", {1e-6, .1}},
                                                           {"contactFriction", {0, 2}},
                                                           {"contactStiffness", {1, 1e14}},
                                                           {"abdStiffness", {1, 1e14}},
                                                           {"newtonIterations", {1, 2000}},
                                                           {"lineSearchIterations", {1, 200}},
                                                           {"velocityTolerance", {1e-8, 1}},
                                                           {"maxRank", {1, 4096}},
                                                           {"neumannTerms", {1, 32}},
                                                           {"maxIterations", {1, 100000}},
                                                           {"omega", {.001, 1}},
                                                           {"linearTolerance", {1e-12, .1}}};
        for (auto it = nextSimulation.begin(); it != nextSimulation.end(); ++it) {
            if (it.key() == "adaptiveInverse" || it.key() == "infiniteGround") {
                if (!it.value().isBool())
                    return fail(QStringLiteral("模拟配置开关无效"));
                continue;
            }
            if (!ranges.contains(it.key()) || !it.value().isDouble())
                return fail(QStringLiteral("未知模拟配置：") + it.key());
            auto r = ranges[it.key()];
            double v = it.value().toDouble();
            if (!std::isfinite(v) || v < r.first || v > r.second)
                return fail(QStringLiteral("模拟配置超出范围：") + it.key());
            if ((it.key()=="newtonIterations" || it.key()=="lineSearchIterations" || it.key()=="maxRank" || it.key()=="neumannTerms" || it.key()=="maxIterations") && std::floor(v)!=v)
                return fail(QStringLiteral("模拟迭代次数和秩必须为整数：")+it.key());
        }
    }
    simulation = nextSimulation;
    objects = std::move(next);
    links = nextLinks;
    exclusions = nextExclusions;
    timeStep = nextTimeStep;
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
