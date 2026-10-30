#include "MainWindow.h"
#include "VehicleAssemblyDialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHash>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <algorithm>

namespace bagua {
void MainWindow::importMeshDialog() {
    if (editingLocked())
        return;
    const auto paths =
        QFileDialog::getOpenFileNames(this, QStringLiteral("导入网格"), {}, QStringLiteral("Meshes (*.obj *.stl *.ply *.msh)"));
    if (paths.isEmpty())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("导入设置"));
    QFormLayout form(&dialog);
    QComboBox units, axis;
    units.addItems({QStringLiteral("米"), QStringLiteral("毫米"), QStringLiteral("厘米")});
    axis.addItems({QStringLiteral("Y 轴向上"), QStringLiteral("Z 轴向上")});
    QCheckBox fixed(QStringLiteral("导入后固定（静态碰撞体）"));
    fixed.setChecked(true);
    form.addRow(QStringLiteral("源文件单位"), &units);
    form.addRow(QStringLiteral("源文件坐标系"), &axis);
    form.addRow(QString(), &fixed);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    for (const auto &path : paths)
        importAsset(path, {units.currentIndex() == 1 ? .001 : units.currentIndex() == 2 ? .01 : 1.,
                           axis.currentIndex() == 1, fixed.isChecked()});
}
bool MainWindow::importComponent(const QString &path) {
    if (editingLocked()) {
        log(QStringLiteral("请先退出仿真或回放再导入组件"));
        return false;
    }
    finishInspectorEdit();
    viewport->cancelDrag();
    Scene component;
    QString error;
    if (!component.load(path, &error)) {
        log(QStringLiteral("组件导入失败：") + error);
        return false;
    }
    if (component.objects.isEmpty()) {
        log(QStringLiteral("组件文件没有对象"));
        return false;
    }
    if (scene->objects.size() + component.objects.size() > 10000) {
        log(QStringLiteral("导入后对象数量超出限制"));
        return false;
    }
    QHash<QString, QString> idMap;
    auto exists = [&](const QString &id) {
        return std::any_of(scene->objects.cbegin(), scene->objects.cend(),
                           [&](const Object &o) { return o.id == id; });
    };
    QVector<Object> imported;
    for (auto object : component.objects) {
        const QString original = object.id;
        if (object.id.isEmpty() || exists(object.id) || idMap.values().contains(object.id))
            object.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        idMap.insert(original, object.id);
        imported.push_back(std::move(object));
    }
    QVector<Link> links = scene->links;
    QSet<QString> linkIds;
    for (const auto &link : links)
        linkIds.insert(link.id);
    for (auto link : component.links) {
        link.a = idMap.value(link.a, link.a);
        link.b = idMap.value(link.b, link.b);
        if (link.a.isEmpty() || link.b.isEmpty())
            continue;
        if (linkIds.contains(link.id))
            link.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        linkIds.insert(link.id);
        links.push_back(link);
    }
    QVector<QPair<QString, QString>> exclusions = scene->exclusions;
    for (auto pair : component.exclusions) {
        pair.first = idMap.value(pair.first, pair.first);
        pair.second = idMap.value(pair.second, pair.second);
        if (!pair.first.isEmpty() && !pair.second.isEmpty())
            exclusions.push_back(pair);
    }
    QVector<Object> objects = scene->objects;
    objects += imported;
    scene->editAssembly(objects, links, exclusions, imported.first().id,
                        QStringLiteral("导入组件：%1").arg(QFileInfo(path).completeBaseName()));
    viewport->frameSelected();
    log(QStringLiteral("已导入组件 %1（%2 个对象、%3 条约束）；保留锚点、质量与物理属性。")
            .arg(QFileInfo(path).fileName())
            .arg(imported.size())
            .arg(component.links.size()));
    return true;
}
void MainWindow::exportComponents(bool onlySelected) {
    const QString directory = QFileDialog::getExistingDirectory(
        this, onlySelected ? QStringLiteral("导出当前对象为组件") : QStringLiteral("拆分场景为组件"),
        QDir::current().filePath(QStringLiteral("components")));
    if (!directory.isEmpty())
        exportComponentsTo(directory, onlySelected);
}
bool MainWindow::exportComponentsTo(const QString &directory, bool onlySelected) {
    if (!scene || editingLocked())
        return false;
    const Object *chosen = scene->current();
    if (onlySelected && !chosen) {
        log(QStringLiteral("请先选择一个对象"));
        return false;
    }
    QDir output(directory);
    if (!output.exists() && !QDir().mkpath(directory)) {
        log(QStringLiteral("无法创建组件目录：") + directory);
        return false;
    }
    QSet<QString> usedNames;
    int count = 0;
    for (const auto &object : scene->objects) {
        if (onlySelected && object.id != chosen->id)
            continue;
        QString base = object.name.trimmed();
        base.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\s]+")), QStringLiteral("_"));
        if (base.isEmpty())
            base = object.id.left(8);
        while (usedNames.contains(base.toLower()))
            base += QStringLiteral("_");
        usedNames.insert(base.toLower());
        Scene component;
        component.objects = {object};
        component.selected = object.id;
        component.simulation = scene->simulation;
        component.timeStep = scene->timeStep;
        component.view = scene->view;
        const QString target = output.filePath(base + QStringLiteral(".bagua.json"));
        QSaveFile file(target);
        const auto bytes = QJsonDocument(component.json()).toJson(QJsonDocument::Indented);
        if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit()) {
            ++count;
        } else {
            log(QStringLiteral("写入组件失败：") + target);
        }
    }
    log(QStringLiteral("已导出 %1 个组件到 %2；可在新场景中逐个“导入组件”，再用车辆装配向导建立悬挂与轮辋-轮胎缝合。")
            .arg(count)
            .arg(QDir::toNativeSeparators(directory)));
    return count > 0;
}
void MainWindow::showVehicleAssemblyWizard() {
    if (editingLocked()) {
        log(QStringLiteral("请先退出仿真或回放再生成车辆装配"));
        return;
    }
    VehicleAssemblyDialog dialog(*scene, this);
    if (dialog.exec() == QDialog::Accepted)
        applyVehicleAssembly(dialog.request());
}
} // namespace bagua
