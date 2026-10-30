#include "MainWindow.h"
#include "VehicleAssemblyDialog.h"
#include <QHash>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace bagua {
namespace {
int objectIndex(const QVector<Object> &objects, const QString &id) {
    for (int i = 0; i < objects.size(); ++i)
        if (objects[i].id == id)
            return i;
    return -1;
}
QString uniqueLinkId(const QVector<Link> &links, const QString &base) {
    QString id = base;
    int suffix = 2;
    auto used = [&](const QString &candidate) {
        return std::any_of(links.cbegin(), links.cend(),
                           [&](const Link &link) { return link.id == candidate; });
    };
    while (used(id))
        id = base + QStringLiteral("_%1").arg(suffix++);
    return id;
}
int ensureAnchor(Object &object, const QVector3D &local) {
    for (int i = 0; i < object.anchors.size(); ++i)
        if ((object.anchors[i] - local).lengthSquared() < 1e-8f)
            return i;
    object.anchors.push_back(local);
    return int(object.anchors.size()) - 1;
}
bool hasExclusion(const QVector<QPair<QString, QString>> &exclusions, const QString &a, const QString &b) {
    return std::any_of(exclusions.cbegin(), exclusions.cend(), [&](const auto &pair) {
        return (pair.first == a && pair.second == b) || (pair.first == b && pair.second == a);
    });
}
void addExclusion(QVector<QPair<QString, QString>> &exclusions, const QString &a, const QString &b) {
    if (!hasExclusion(exclusions, a, b))
        exclusions.push_back({a, b});
}
// Rim components exported from the HSC vehicle already carry their 39 bead
// anchors. For an arbitrary imported rim, generate a ring in local XY around
// the mesh centre so the automatic bead pairing has a meaningful A side.
void ensureRimAnchors(Object &rim, int count, double radius) {
    if (!rim.anchors.isEmpty())
        return;
    QVector3D center{0, 0, 0};
    if (rim.mesh && !rim.mesh->positions.isEmpty()) {
        QVector3D lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
        for (const auto &p : rim.mesh->positions) {
            lo.setX(std::min(lo.x(), p.x()));
            lo.setY(std::min(lo.y(), p.y()));
            lo.setZ(std::min(lo.z(), p.z()));
            hi.setX(std::max(hi.x(), p.x()));
            hi.setY(std::max(hi.y(), p.y()));
            hi.setZ(std::max(hi.z(), p.z()));
        }
        center = (lo + hi) * .5f;
    }
    const int n = std::clamp(count, 1, 256);
    rim.anchors.reserve(n);
    for (int i = 0; i < n; ++i) {
        const double angle = 6.283185307179586 * double(i) / double(n);
        rim.anchors.push_back(center + QVector3D(float(std::cos(angle) * radius),
                                                float(std::sin(angle) * radius), 0));
    }
}
Link makeBead(const QVector<Link> &links, const Object &rim, const Object &tire, double stiffness,
              QString *error) {
    Link bead;
    bead.id = uniqueLinkId(links, rim.name + QStringLiteral("_bead"));
    bead.type = Link::Type::Stitch;
    bead.a = rim.id;
    bead.b = tire.id;
    bead.anchorPairs = false;
    bead.stiffness = stiffness;
    bead.rest = 0;
    if (!tire.mesh || tire.mesh->positions.isEmpty()) {
        if (error)
            *error = QStringLiteral("轮胎没有网格顶点，无法建立轮辋-轮胎缝合：") + tire.name;
        return {};
    }
    QVector<QVector3D> targets;
    targets.reserve(tire.mesh->positions.size());
    for (const auto &point : tire.mesh->positions)
        targets.push_back(tire.matrix().map(point));
    QSet<int> used;
    for (int i = 0; i < rim.anchors.size(); ++i) {
        const QVector3D world = rim.anchorMatrix().map(rim.anchors[i]);
        int best = -1;
        float bestDistance = std::numeric_limits<float>::max();
        for (int j = 0; j < targets.size(); ++j) {
            if (used.contains(j))
                continue;
            const float distance = (targets[j] - world).lengthSquared();
            if (distance < bestDistance) {
                bestDistance = distance;
                best = j;
            }
        }
        if (best >= 0) {
            used.insert(best);
            bead.pairs.push_back({i, best});
        }
    }
    if (bead.pairs.isEmpty() && error)
        *error = QStringLiteral("轮辋与轮胎之间没有可配对的顶点：") + rim.name;
    return bead;
}
} // namespace

bool MainWindow::applyVehicleAssembly(const VehicleAssemblyRequest &request) {
    if (!scene || editingLocked()) {
        log(QStringLiteral("请先退出仿真或回放再生成装配"));
        return false;
    }
    finishInspectorEdit();
    viewport->cancelDrag();
    QVector<Object> objects = scene->objects;
    QVector<Link> links = scene->links;
    QVector<QPair<QString, QString>> exclusions = scene->exclusions;
    const int chassisIndex = objectIndex(objects, request.chassis);
    if (chassisIndex < 0) {
        log(QStringLiteral("装配失败：未找到车架"));
        return false;
    }
    QSet<int> usedChassisAnchors;
    int wheels = 0, beads = 0;
    for (int i = 0; i < 4; ++i) {
        const auto &wheel = request.wheels[size_t(i)];
        if (!wheel.enabled)
            continue;
        const int spindleIndex = objectIndex(objects, wheel.spindle);
        const int rimIndex = objectIndex(objects, wheel.rim);
        const int tireIndex = objectIndex(objects, wheel.tire);
        if (spindleIndex < 0 || rimIndex < 0 || tireIndex < 0) {
            log(QStringLiteral("装配失败：第 %1 行车轮对象不存在").arg(i + 1));
            return false;
        }
        auto &chassis = objects[chassisIndex];
        auto &spindle = objects[spindleIndex];
        auto &rim = objects[rimIndex];
        auto &tire = objects[tireIndex];
        if (spindle.id == rim.id || spindle.id == tire.id || rim.id == tire.id) {
            log(QStringLiteral("装配失败：第 %1 行选择了重复对象").arg(i + 1));
            return false;
        }
        for (auto *object : {&chassis, &spindle, &rim, &tire})
            object->fixed = false;
        if (tire.mesh && !tire.mesh->tetrahedra.isEmpty()) {
            tire.deformable = true;
            tire.pinBottom = false;
        }
        // Chassis anchors are the four suspension mounts. Reuse an unused
        // anchor near the wheel, otherwise create one at the spindle centre.
        const QVector3D spindleWorld = spindle.anchorMatrix().map({0, 0, 0});
        const QVector3D chassisLocal = chassis.anchorMatrix().inverted().map(spindleWorld);
        int chassisAnchor = -1;
        float nearest = std::numeric_limits<float>::max();
        for (int a = 0; a < chassis.anchors.size(); ++a) {
            if (usedChassisAnchors.contains(a))
                continue;
            const float distance = (chassis.anchors[a] - chassisLocal).lengthSquared();
            if (distance < nearest) {
                nearest = distance;
                chassisAnchor = a;
            }
        }
        if (chassisAnchor < 0 || nearest > 0.36f) {
            chassisAnchor = ensureAnchor(chassis, chassisLocal);
        }
        usedChassisAnchors.insert(chassisAnchor);
        const int spindleAnchor = ensureAnchor(spindle, {0, 0, 0});

        Link guide;
        guide.id = uniqueLinkId(links, QStringLiteral("suspension_%1_guide").arg(i));
        guide.type = Link::Type::Prismatic;
        guide.a = chassis.id;
        guide.b = spindle.id;
        guide.anchorA = chassis.anchors[chassisAnchor];
        guide.anchorB = spindle.anchors[spindleAnchor];
        guide.axis = {0, 1, 0};
        guide.strength = request.suspensionStrength;
        guide.hasLimit = true;
        guide.lower = request.suspensionLower;
        guide.upper = request.suspensionUpper;
        guide.limitStrength = 100;
        guide.damped = request.suspensionDamping > 0;
        guide.damping = request.suspensionDamping;
        links.push_back(guide);

        Link spring;
        spring.id = uniqueLinkId(links, QStringLiteral("suspension_%1_spring").arg(i));
        spring.type = Link::Type::Stitch;
        spring.a = chassis.id;
        spring.b = spindle.id;
        spring.anchorPairs = true;
        spring.pairs = {{chassisAnchor, spindleAnchor}};
        spring.stiffness = request.springStiffness;
        spring.rest = request.springRest;
        links.push_back(spring);

        Link motor;
        motor.id = uniqueLinkId(links, QStringLiteral("suspension_%1_motor").arg(i));
        motor.type = Link::Type::Revolute;
        motor.a = spindle.id;
        motor.b = rim.id;
        motor.anchorA = {0, 0, .1f};
        motor.anchorB = {0, 0, .1f};
        motor.axis = {0, 0, -1};
        motor.strength = 1e5;
        motor.driven = true;
        motor.driveSpeed = request.driveSpeed;
        motor.driveRamp = .5;
        motor.driveStrength = 1e4;
        links.push_back(motor);

        ensureRimAnchors(rim, request.beadAnchors, request.beadRadius);
        QString beadError;
        Link bead = makeBead(links, rim, tire, request.beadStiffness, &beadError);
        if (bead.pairs.isEmpty()) {
            log(QStringLiteral("装配失败：") + beadError);
            return false;
        }
        links.push_back(bead);
        ++beads;
        addExclusion(exclusions, chassis.id, spindle.id);
        addExclusion(exclusions, spindle.id, rim.id);
        ++wheels;
    }
    for (const auto &pair : request.loosePairs) {
        const int rimIndex = objectIndex(objects, pair.first);
        const int tireIndex = objectIndex(objects, pair.second);
        if (rimIndex < 0 || tireIndex < 0) {
            log(QStringLiteral("松散轮胎装配失败：对象不存在"));
            return false;
        }
        auto &rim = objects[rimIndex];
        auto &tire = objects[tireIndex];
        if (rim.id == tire.id)
            continue;
        rim.fixed = false;
        tire.fixed = false;
        if (tire.mesh && !tire.mesh->tetrahedra.isEmpty()) {
            tire.deformable = true;
            tire.pinBottom = false;
        }
        ensureRimAnchors(rim, request.beadAnchors, request.beadRadius);
        QString beadError;
        Link bead = makeBead(links, rim, tire, request.beadStiffness, &beadError);
        if (bead.pairs.isEmpty()) {
            log(QStringLiteral("松散轮胎装配失败：") + beadError);
            return false;
        }
        links.push_back(bead);
        ++beads;
    }
    if (wheels == 0 && beads == 0) {
        log(QStringLiteral("没有生成任何车轮或松散轮胎装配"));
        return false;
    }
    scene->editAssembly(objects, links, exclusions, objects[chassisIndex].id,
                        QStringLiteral("车辆四轮装配"));
    viewport->frameSelected();
    if (request.enableHsc) {
        auto options = physicsOptions();
        options.enabled = true;
        setPhysicsOptions(options);
        if (request.applyHscPreset) {
            scene->timeStep = 1.0 / 30.0;
            scene->simulation = QJsonObject{{"gravity", 9.8},
                                            {"contactFriction", .6},
                                            {"contactStiffness", 1e10},
                                            {"contactDistance", .001},
                                            {"abdStiffness", 1e9},
                                            {"infiniteGround", true},
                                            {"newtonIterations", 400},
                                            {"lineSearchIterations", 32},
                                            {"velocityTolerance", .003},
                                            {"maxRank", 256},
                                            {"maxIterations", 12000},
                                            {"neumannTerms", 5},
                                            {"omega", .7},
                                            {"linearTolerance", 1e-4},
                                            {"adaptiveInverse", true}};
            scene->notifyChanged();
            log(QStringLiteral("已应用 HSC 车辆参数预设；可在“工具 → 物理模拟设置”继续调整。"));
        }
    }
    log(QStringLiteral("车辆装配完成：%1 个悬挂车轮、%2 条轮辋-轮胎缝合；移动副/弹簧/转动副/接触排除已建立。")
            .arg(wheels)
            .arg(beads));
    return true;
}
} // namespace bagua
