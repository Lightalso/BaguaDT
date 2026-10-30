#include "MainWindow.h"
#include "MainWindowAssemblyShared.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QQuaternion>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace bagua {
namespace {
QString anchorLabel(const Object &object, int index) {
    if (index < 0 || index >= object.anchors.size())
        return QStringLiteral("未选择");
    const auto p = object.anchorMatrix().map(object.anchors[index]);
    return QStringLiteral("#%1 (%2, %3, %4)")
        .arg(index)
        .arg(p.x(), 0, 'f', 2)
        .arg(p.y(), 0, 'f', 2)
        .arg(p.z(), 0, 'f', 2);
}
QString vertexLabel(const Object &object, int index) {
    if (!object.mesh || index < 0 || index >= object.mesh->positions.size())
        return QStringLiteral("未选择");
    const auto p = object.matrix().map(object.mesh->positions[index]);
    return QStringLiteral("顶点 #%1 (%2, %3, %4)")
        .arg(index)
        .arg(p.x(), 0, 'f', 2)
        .arg(p.y(), 0, 'f', 2)
        .arg(p.z(), 0, 'f', 2);
}
int nearestAnchor(const Object &object, const QVector3D &world) {
    int best = -1;
    float distance = 1e30f;
    for (int i = 0; i < object.anchors.size(); ++i) {
        const float d = (object.anchorMatrix().map(object.anchors[i]) - world).lengthSquared();
        if (d < distance) {
            distance = d;
            best = i;
        }
    }
    return best;
}
int nearestVertex(const Object &object, const QVector3D &world) {
    if (!object.mesh)
        return -1;
    int best = -1;
    float distance = 1e30f;
    for (int i = 0; i < object.mesh->positions.size(); ++i) {
        const float d = (object.matrix().map(object.mesh->positions[i]) - world).lengthSquared();
        if (d < distance) {
            distance = d;
            best = i;
        }
    }
    return best;
}
} // namespace

int MainWindow::selectedLinkIndex() const {
    auto *item = linkTree ? linkTree->currentItem() : nullptr;
    if (!item || !scene)
        return -1;
    const auto id = item->data(1, Qt::UserRole + 1).toString();
    for (int i = 0; i < scene->links.size(); ++i)
        if (scene->links[i].id == id)
            return i;
    return -1;
}
int MainWindow::stitchPairAnchor(int row) const {
    if (!stitchPairs || row < 0 || row >= stitchPairs->rowCount())
        return -1;
    if (auto *combo = qobject_cast<QComboBox *>(stitchPairs->cellWidget(row, 0)))
        return combo->currentData().toInt();
    auto *item = stitchPairs->item(row, 0);
    return item ? item->text().toInt() : -1;
}
int MainWindow::stitchPairTarget(int row) const {
    if (!stitchPairs || row < 0 || row >= stitchPairs->rowCount())
        return -1;
    if (auto *combo = qobject_cast<QComboBox *>(stitchPairs->cellWidget(row, 1)))
        return combo->currentData().toInt();
    auto *item = stitchPairs->item(row, 1);
    return item ? item->data(Qt::UserRole).toInt() : -1;
}
void MainWindow::updateStitchPairInfo() {
    if (!stitchPairInfo || !stitchPairs || !scene)
        return;
    const int row = stitchPairs->currentRow();
    if (row < 0) {
        stitchPairInfo->setText(QStringLiteral(
            "选择配对行，点“拾取 A”再点模型 A 表面，随后点模型 B 表面；或直接用“自动配对”。"));
        viewport->clearLinkHighlight();
        return;
    }
    const int ai = stitchPairAnchor(row), bi = stitchPairTarget(row);
    const auto *a = scene->find(linkBodyA->currentData().toString());
    const auto *b = scene->find(linkBodyB->currentData().toString());
    if (!a || !b || ai < 0 || ai >= a->anchors.size()) {
        stitchPairInfo->setText(QStringLiteral("A 锚点无效，请从下拉框重新选择。"));
        return;
    }
    const QVector3D wa = a->anchorMatrix().map(a->anchors[ai]);
    viewport->setAnchorHighlight(a->id, ai);
    if (bi < 0) {
        stitchPairInfo->setText(QStringLiteral("A #%1 (%2, %3, %4) → B 未选择。")
                                    .arg(ai)
                                    .arg(wa.x(), 0, 'f', 2)
                                    .arg(wa.y(), 0, 'f', 2)
                                    .arg(wa.z(), 0, 'f', 2));
        return;
    }
    QVector3D wb;
    QString bText;
    if (stitchAnchorMode->isChecked()) {
        if (bi >= b->anchors.size()) {
            stitchPairInfo->setText(QStringLiteral("B 锚点序号超出范围，请重新选择。"));
            return;
        }
        wb = b->anchorMatrix().map(b->anchors[bi]);
        bText = QStringLiteral("B 锚点 #%1").arg(bi);
    } else {
        if (!b->mesh || bi >= b->mesh->positions.size()) {
            stitchPairInfo->setText(QStringLiteral("B 网格顶点序号超出范围，请重新选择。"));
            return;
        }
        wb = b->matrix().map(b->mesh->positions[bi]);
        bText = QStringLiteral("B 顶点 #%1").arg(bi);
    }
    stitchPairInfo->setText(QStringLiteral("A #%1 (%2, %3, %4) → %5 (%6, %7, %8) · 距离 %9 m")
                                .arg(ai)
                                .arg(wa.x(), 0, 'f', 2)
                                .arg(wa.y(), 0, 'f', 2)
                                .arg(wa.z(), 0, 'f', 2)
                                .arg(bText)
                                .arg(wb.x(), 0, 'f', 2)
                                .arg(wb.y(), 0, 'f', 2)
                                .arg(wb.z(), 0, 'f', 2)
                                .arg((wb - wa).length(), 0, 'f', 4));
    if (auto *distance = stitchPairs->item(row, 2))
        distance->setText(QStringLiteral("%1 m").arg((wb - wa).length(), 0, 'f', 4));
}
void MainWindow::setStitchRow(int row, int anchor, int target) {
    if (!stitchPairs || !scene || row < 0)
        return;
    if (row >= stitchPairs->rowCount())
        stitchPairs->setRowCount(row + 1);
    const auto *a = scene->find(linkBodyA->currentData().toString());
    const auto *b = scene->find(linkBodyB->currentData().toString());
    const bool anchors = stitchAnchorMode && stitchAnchorMode->isChecked();
    auto *aCombo = new QComboBox;
    aCombo->setObjectName(QStringLiteral("stitchAnchorA_%1").arg(row));
    if (a) {
        for (int i = 0; i < a->anchors.size(); ++i)
            aCombo->addItem(anchorLabel(*a, i), i);
        const int index = aCombo->findData(anchor);
        aCombo->setCurrentIndex(index >= 0 ? index : 0);
    }
    aCombo->setEnabled(!editingLocked());
    connect(aCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { updateStitchPairInfo(); updateLinkComposer(); });
    stitchPairs->setCellWidget(row, 0, aCombo);
    if (anchors && b) {
        auto *bCombo = new QComboBox;
        bCombo->setObjectName(QStringLiteral("stitchAnchorB_%1").arg(row));
        for (int i = 0; i < b->anchors.size(); ++i)
            bCombo->addItem(anchorLabel(*b, i), i);
        const int index = bCombo->findData(target);
        bCombo->setCurrentIndex(index >= 0 ? index : 0);
        bCombo->setEnabled(!editingLocked());
        connect(bCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this] { updateStitchPairInfo(); updateLinkComposer(); });
        stitchPairs->setCellWidget(row, 1, bCombo);
    } else {
        auto *item = new QTableWidgetItem(b ? vertexLabel(*b, target) : QStringLiteral("未选择"));
        item->setData(Qt::UserRole, target);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        stitchPairs->setItem(row, 1, item);
    }
    auto *distance = new QTableWidgetItem(QStringLiteral("—"));
    distance->setFlags(distance->flags() & ~Qt::ItemIsEditable);
    distance->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    stitchPairs->setItem(row, 2, distance);
    // Fill the distance immediately from current transforms.
    const int ai = anchor;
    const int bi = target;
    bool validTarget = false;
    QVector3D wa, wb;
    if (a && b && ai >= 0 && ai < a->anchors.size() && bi >= 0) {
        wa = a->anchorMatrix().map(a->anchors[ai]);
        if (anchors && bi < b->anchors.size()) {
            wb = b->anchorMatrix().map(b->anchors[bi]);
            validTarget = true;
        } else if (!anchors && b->mesh && bi < b->mesh->positions.size()) {
            wb = b->matrix().map(b->mesh->positions[bi]);
            validTarget = true;
        }
    }
    if (validTarget)
        distance->setText(QStringLiteral("%1 m").arg((wb - wa).length(), 0, 'f', 4));
    updateStitchPairInfo();
}
void MainWindow::refreshStitchRows() {
    if (!stitchPairs)
        return;
    QVector<int> anchors;
    for (int row = 0; row < stitchPairs->rowCount(); ++row)
        anchors.push_back(stitchPairAnchor(row));
    stitchPairs->setRowCount(0);
    for (int row = 0; row < anchors.size(); ++row)
        setStitchRow(row, anchors[row], 0);
    if (stitchPairs->rowCount())
        stitchPairs->setCurrentCell(0, 0);
    updateStitchPairInfo();
}

void MainWindow::deleteSelectedLink() {
    const int index = selectedLinkIndex();
    if (index < 0) {
        setLinkError(QStringLiteral("请先在列表里选中要删除的约束。"));
        return;
    }
    auto links = scene->links;
    links.removeAt(index);
    commitLinks(links, scene->exclusions, QStringLiteral("删除约束"));
}

bool MainWindow::commitLinks(const QVector<Link> &links, const QVector<QPair<QString, QString>> &exclusions,
                             const QString &label) {
    if (editingLocked())
        return false;
    auto candidate = scene->json();
    QJsonArray array, pairs;
    for (const auto &link : links)
        array.append(linkJson(link));
    for (const auto &pair : exclusions)
        pairs.append(QJsonObject{{"a", pair.first}, {"b", pair.second}});
    candidate["links"] = array;
    candidate["exclusions"] = pairs;
    Scene check;
    QString error;
    if (!check.loadJson(candidate, &error)) {
        setLinkError(QStringLiteral("装配修改被拒绝：") + error);
        log(QStringLiteral("装配修改被拒绝：") + error);
        return false;
    }
    scene->editLinks(links, exclusions, label);
    syncAssemblyPanel();
    return true;
}

void MainWindow::setLinkError(const QString &error) {
    if (!linkError)
        return;
    linkError->setText(error);
    linkError->setVisible(!error.isEmpty());
}

void MainWindow::pickBody(int target) {
    if (editingLocked())
        return;
    pickTarget = target;
    viewport->anchorPicking = true;
    statusBar()->showMessage(target == 4 ? QStringLiteral("在视口点击部件 A（Esc 取消）")
                                        : QStringLiteral("在视口点击部件 B（Esc 取消）"));
}

void MainWindow::focusSelectedLink() {
    const int index = selectedLinkIndex();
    if (index < 0)
        return;
    const auto &link = scene->links[index];
    const auto *a = scene->find(link.a);
    const auto *b = scene->find(link.b);
    if (!a || !b)
        return;
    const auto wa = a->anchorMatrix().map(link.anchorA), wb = b->anchorMatrix().map(link.anchorB);
    viewport->target = (wa + wb) * .5f;
    viewport->distance = std::clamp((wa - wb).length() * 3.f + 2.f, .8f, 60.f);
    viewport->update();
}

bool MainWindow::readLinkForm(Link &link, QString *error) const {
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    const QString type = linkType->currentData().toString();
    if (!Link::typeFromKey(type, link.type))
        return fail(QStringLiteral("未知约束类型"));
    link.a = linkBodyA->currentData().toString();
    link.b = linkBodyB->currentData().toString();
    if (link.a.isEmpty() || link.b.isEmpty())
        return fail(QStringLiteral("请选择两个部件"));
    if (link.a == link.b)
        return fail(QStringLiteral("不能把部件连到它自己"));
    link.id = QStringLiteral("%1-%2-%3").arg(type, link.a, link.b);
    const int selected = selectedLinkIndex();
    if (selected >= 0 && scene->links[selected].type == Link::Type::Rotation) {
        link.angle = scene->links[selected].angle;
        link.angularVelocity = scene->links[selected].angularVelocity;
    }
    if (link.type == Link::Type::Stitch) {
        link.anchorPairs = stitchAnchorMode->isChecked();
        link.stiffness = stitchStiffness->value();
        link.rest = stitchRest->value();
        const Object *aObject = scene ? scene->find(link.a) : nullptr;
        const Object *bObject = scene ? scene->find(link.b) : nullptr;
        if (!aObject || !bObject)
            return fail(QStringLiteral("缝合对象不存在"));
        for (int row = 0; row < stitchPairs->rowCount(); ++row) {
            const int ai = stitchPairAnchor(row);
            const int bi = stitchPairTarget(row);
            if (ai < 0 || ai >= aObject->anchors.size())
                return fail(QStringLiteral("第 %1 行 A 锚点无效；部件 A 有 %2 个锚点，请从下拉框选择。")
                                .arg(row + 1)
                                .arg(aObject->anchors.size()));
            if (link.anchorPairs) {
                if (bi < 0 || bi >= bObject->anchors.size())
                    return fail(QStringLiteral("第 %1 行 B 锚点无效；部件 B 有 %2 个锚点，请从下拉框选择。")
                                    .arg(row + 1)
                                    .arg(bObject->anchors.size()));
            } else {
                if (!bObject->mesh || bi < 0 || bi >= bObject->mesh->positions.size())
                    return fail(QStringLiteral("第 %1 行 B 顶点无效；请用“视口拾取 B”重新选择。").arg(row + 1));
            }
            link.pairs.push_back({ai, bi});
        }
        if (link.pairs.isEmpty())
            return fail(QStringLiteral("缝合约束至少需要一对顶点"));
        link.axis = {0, 0, 1};
        link.strength = 100;
        return true;
    }
    for (int i = 0; i < 3; ++i) {
        link.anchorA[i] = float(linkAnchorA[size_t(i)]->value());
        link.anchorB[i] = float(linkAnchorB[size_t(i)]->value());
        link.axis[i] = float(linkAxis[size_t(i)]->value());
    }
    if (link.type != Link::Type::Spherical && link.type != Link::Type::Fixed && link.axis.lengthSquared() < 1e-12f)
        return fail(QStringLiteral("轴不能为零向量"));
    link.driven = driveEnabled->isChecked() && (link.type == Link::Type::Revolute || link.type == Link::Type::Prismatic);
    link.driveSpeed = driveSpeed->value();
    link.driveRamp = driveRamp->value();
    link.driveStrength = driveStrength->value();
    link.damped = damperEnabled->isChecked() && link.type == Link::Type::Prismatic;
    link.damping = damperCoefficient->value();
    link.strength = linkStrength->value();
    if (link.strength <= 0)
        return fail(QStringLiteral("强度必须大于 0"));
    link.angle = rotationAngle->value();
    link.angularVelocity = rotationSpeed->value();
    link.hasLimit = limitEnabled->isChecked() && (link.type == Link::Type::Prismatic || link.type == Link::Type::Revolute);
    if (link.hasLimit) {
        link.lower = limitLower->value();
        link.upper = limitUpper->value();
        link.limitStrength = limitStrength->value();
        if (link.lower >= link.upper)
            return fail(QStringLiteral("限位下限必须小于上限"));
    }
    return true;
}

void MainWindow::syncLinkForm(const Link &link) {
    if (assemblySyncing)
        return;
    assemblySyncing = true;
    const QSignalBlocker blockType(linkType);
    const int typeIndex = linkType->findData(link.typeKey());
    if (typeIndex >= 0)
        linkType->setCurrentIndex(typeIndex);
    for (auto *combo : {linkBodyA, linkBodyB}) {
        const QSignalBlocker blocker(combo);
        const QString id = combo == linkBodyA ? link.a : link.b;
        const int index = combo->findData(id);
        if (index >= 0)
            combo->setCurrentIndex(index);
    }
    for (int i = 0; i < 3; ++i) {
        linkAnchorA[size_t(i)]->setValue(link.anchorA[i]);
        linkAnchorB[size_t(i)]->setValue(link.anchorB[i]);
        linkAxis[size_t(i)]->setValue(link.axis[i]);
    }
    driveEnabled->setChecked(link.driven);
    driveSpeed->setValue(link.driveSpeed);
    driveRamp->setValue(link.driveRamp);
    driveStrength->setValue(link.driveStrength);
    damperEnabled->setChecked(link.damped);
    damperCoefficient->setValue(link.damping);
    rotationAngle->setValue(link.angle);
    rotationSpeed->setValue(link.angularVelocity);
    linkStrength->setValue(link.strength);
    limitEnabled->setChecked(link.hasLimit);
    limitLower->setValue(link.lower);
    limitUpper->setValue(link.upper);
    limitStrength->setValue(link.limitStrength);
    stitchAnchorMode->setChecked(link.anchorPairs);
    stitchStiffness->setValue(link.stiffness);
    stitchRest->setValue(link.rest);
    stitchPairs->setRowCount(0);
    for (int row = 0; row < link.pairs.size(); ++row)
        setStitchRow(row, link.pairs[row].first, link.pairs[row].second);
    if (stitchPairs->rowCount() > 0)
        stitchPairs->setCurrentCell(0, 0);
    assemblySyncing = false;
    loadedLink = linkJson(link);
    linkComposerTouched = false;
    syncPivotFromAnchors();
    updateLinkComposer();
    if (link.type == Link::Type::Stitch) {
        if (auto *scroll = findChild<QScrollArea *>(QStringLiteral("linkComposerScroll")))
            scroll->ensureWidgetVisible(linkStitchRow, 0, 8);
    }
}

void MainWindow::syncAssemblyPanel() {
    if (!linkTree || !scene || assemblySyncing)
        return;
    assemblySyncing = true;
    const auto *model = displayScene();
    // Object pickers keep their current choice when the object list changes.
    for (auto *combo : {linkBodyA, linkBodyB, exclusionA, exclusionB}) {
        const QString current = combo->currentData().toString();
        {
            const QSignalBlocker blocker(combo);
            combo->clear();
            for (const auto &o : model->objects)
                combo->addItem(o.name + QStringLiteral(" (") + o.id.left(6) + QStringLiteral(")"), o.id);
            const int index = combo->findData(current);
            if (index >= 0)
                combo->setCurrentIndex(index);
        }
        combo->setEnabled(!editingLocked());
    }
    // Defaults for a fresh constraint: current body on A, the first other on B.
    const QString currentObject = model->current() ? model->current()->id : QString{};
    if (selectedLinkIndex() < 0 && !linkComposerTouched && pickTarget == 0 && !currentObject.isEmpty()) {
        const QSignalBlocker blockA(linkBodyA), blockB(linkBodyB);
        const int aIndex = linkBodyA->findData(currentObject);
        if (aIndex >= 0)
            linkBodyA->setCurrentIndex(aIndex);
        if (linkBodyB->currentIndex() < 0 || linkBodyB->currentData().toString() == currentObject) {
            for (int i = 0; i < linkBodyB->count(); ++i)
                if (linkBodyB->itemData(i).toString() != currentObject) {
                    linkBodyB->setCurrentIndex(i);
                    break;
                }
        }
    } else if (linkBodyA->currentData().toString().isEmpty() && !currentObject.isEmpty()) {
        const QSignalBlocker blocker(linkBodyA);
        linkBodyA->setCurrentIndex(linkBodyA->findData(currentObject));
    }
    if (linkBodyB->currentData().toString().isEmpty() ||
        linkBodyB->currentData().toString() == linkBodyA->currentData().toString()) {
        for (int i = 0; i < linkBodyB->count(); ++i)
            if (linkBodyB->itemData(i).toString() != linkBodyA->currentData().toString()) {
                const QSignalBlocker blocker(linkBodyB);
                linkBodyB->setCurrentIndex(i);
                break;
            }
    }
    if (exclusionA->currentIndex() < 0 && exclusionA->count())
        exclusionA->setCurrentIndex(0);
    if (exclusionB->currentIndex() < 0 && exclusionB->count())
        exclusionB->setCurrentIndex(exclusionB->count() > 1 ? 1 : 0);

    // Keep items alive during selection callbacks. Clearing the model here
    // deletes the item while QTreeWidget is still processing its mouse event.
    const QString selectedId =
        linkTree->currentItem() ? linkTree->currentItem()->data(1, Qt::UserRole + 1).toString() : QString{};
    {
        const QSignalBlocker blockTree(linkTree);
        const int scroll = linkTree->verticalScrollBar()->value();
        QSet<QString> ids;
        for (const auto &link : model->links)
            ids.insert(link.id);
        QHash<QString, QTreeWidgetItem *> items;
        for (int i = linkTree->topLevelItemCount() - 1; i >= 0; --i) {
            auto *item = linkTree->topLevelItem(i);
            const auto id = item->data(1, Qt::UserRole + 1).toString();
            if (!ids.contains(id))
                delete linkTree->takeTopLevelItem(i);
            else
                items.insert(id, item);
        }
        for (int i = 0; i < model->links.size(); ++i) {
            const auto &link = model->links[i];
            auto *item = items.value(link.id, nullptr);
            if (!item) {
                item = new QTreeWidgetItem;
                linkTree->insertTopLevelItem(i, item);
                items.insert(link.id, item);
            } else if (linkTree->indexOfTopLevelItem(item) != i) {
                linkTree->takeTopLevelItem(linkTree->indexOfTopLevelItem(item));
                linkTree->insertTopLevelItem(i, item);
            }
            item->setText(0, linkTypeName(link.type));
            item->setText(1, QStringLiteral("%1 → %2").arg(bodyText(model, link.a), bodyText(model, link.b)));
            item->setText(2, linkSummary(link));
            item->setToolTip(0, linkTypeTip(link.type) + QStringLiteral("\nID: %1").arg(link.id));
            item->setToolTip(1, link.id);
            item->setToolTip(2, linkSummary(link));
            item->setIcon(0, linkIcon(link));
            item->setData(0, Qt::UserRole, i);
            item->setData(1, Qt::UserRole + 1, link.id);
        }
        if (auto *selected = items.value(selectedId, nullptr); selected && linkTree->currentItem() != selected)
            linkTree->setCurrentItem(selected);
        if (!linkTree->currentItem() && linkTree->topLevelItemCount() && !linkComposerTouched)
            linkTree->setCurrentItem(linkTree->topLevelItem(0));
        linkTree->verticalScrollBar()->setValue(scroll);
    }
    // Exclusions.
    {
        const QSignalBlocker blocker(exclusionList);
        exclusionList->clear();
        for (int i = 0; i < model->exclusions.size(); ++i) {
            auto *item = new QListWidgetItem(
                QStringLiteral("%1 ↔ %2").arg(bodyText(model, model->exclusions[i].first),
                                              bodyText(model, model->exclusions[i].second)),
                exclusionList);
            item->setData(Qt::UserRole, i);
        }
    }
    // Anchors of the selected object.
    const int anchorRow = anchorList->currentRow();
    {
        const QSignalBlocker anchorBlocker(anchorList);
        anchorList->clear();
        if (const auto *object = model->current()) {
            for (int i = 0; i < object->anchors.size(); ++i) {
                const auto &anchor = object->anchors[i];
                auto *item = new QListWidgetItem(QStringLiteral("#%1  (%2, %3, %4)")
                                                     .arg(i)
                                                     .arg(anchor.x(), 0, 'f', 3)
                                                     .arg(anchor.y(), 0, 'f', 3)
                                                     .arg(anchor.z(), 0, 'f', 3),
                                                 anchorList);
                item->setData(Qt::UserRole, i);
            }
        }
        if (anchorRow >= 0 && anchorRow < anchorList->count())
            anchorList->setCurrentRow(anchorRow);
    }
    const auto *currentObjectPtr = scene->current();
    for (auto *widget : {anchorAdd, anchorPick, anchorRemove, findChild<QPushButton *>("anchorUpdate")})
        widget->setEnabled(!editingLocked() && currentObjectPtr && !currentObjectPtr->deformable);
    for (auto *widget : {(QWidget *)linkNew, (QWidget *)linkAdd, (QWidget *)linkUpdate, (QWidget *)linkRemove,
                         (QWidget *)linkPickBodyA, (QWidget *)linkPickBodyB, (QWidget *)linkSwap,
                         (QWidget *)linkPivotMid, (QWidget *)linkPivotFromA, (QWidget *)linkPivotFromB,
                         (QWidget *)linkPivotPick, (QWidget *)linkAxisFlip, (QWidget *)exclusionAdd,
                         (QWidget *)exclusionRemove})
        widget->setEnabled(!editingLocked());
    assemblySyncing = false;
    const int selected = selectedLinkIndex();
    if (selected >= 0)
        viewport->setLinkHighlight(scene->links[selected].id);
    else
        viewport->clearLinkHighlight();
    if (selected >= 0 && linkJson(scene->links[selected]) != loadedLink)
        syncLinkForm(scene->links[selected]);
    else if (selected < 0)
        loadedLink = {};
    updateLinkComposer();
    updateLinkDiagnostics();
}

void MainWindow::updateLinkComposer() {
    if (!linkType || assemblySyncing)
        return;
    Link::Type type = Link::Type::Prismatic;
    Link::typeFromKey(linkType->currentData().toString(), type);
    const bool stitch = type == Link::Type::Stitch;
    const bool axial = type == Link::Type::Prismatic || type == Link::Type::Revolute || type == Link::Type::Rotation;
    const bool editable = !editingLocked();
    const bool controllable = type == Link::Type::Revolute || type == Link::Type::Prismatic;
    linkTypeHint->setText(linkTypeTip(type));
    linkType->setToolTip(linkTypeTip(type));
    linkPivotGroup->setVisible(!stitch);
    linkAxisGroup->setVisible(axial);
    linkLimitRow->setVisible(type == Link::Type::Prismatic || type == Link::Type::Revolute);
    linkRotationRow->setVisible(type == Link::Type::Rotation);
    linkStitchRow->setVisible(stitch);
    stitchPairs->setVisible(stitch);
    driveRow->setVisible(controllable);
    damperRow->setVisible(type == Link::Type::Prismatic);
    linkAdvancedAnchors->setEnabled(editable && !stitch);
    linkAnchorRow->setVisible(linkAdvancedAnchors->isChecked() && !stitch && editable);
    linkStrength->setEnabled(editable && !stitch);
    for (auto *field : linkPivot)
        field->setEnabled(editable && !stitch);
    for (int i = 0; i < 3; ++i) {
        linkAnchorA[size_t(i)]->setEnabled(editable && !stitch);
        linkAnchorB[size_t(i)]->setEnabled(editable && !stitch);
        linkAxis[size_t(i)]->setEnabled(editable && axial);
    }
    for (auto *button : {linkPickA, linkPickB})
        button->setEnabled(editable && !stitch);
    for (auto *button : {linkPivotMid, linkPivotFromA, linkPivotFromB, linkPivotPick})
        button->setEnabled(editable && !stitch);
    rotationAngle->setEnabled(editable && type == Link::Type::Rotation);
    rotationSpeed->setEnabled(editable && type == Link::Type::Rotation);
    limitEnabled->setEnabled(editable && (type == Link::Type::Prismatic || type == Link::Type::Revolute));
    const bool limitActive = limitEnabled->isChecked() && (type == Link::Type::Prismatic || type == Link::Type::Revolute);
    for (auto *field : {limitLower, limitUpper, limitStrength})
        field->setEnabled(editable && limitActive);
    driveEnabled->setEnabled(editable && controllable);
    const bool driveActive = driveEnabled->isChecked() && controllable;
    for (auto *field : {driveSpeed, driveRamp, driveStrength})
        field->setEnabled(editable && driveActive);
    damperEnabled->setEnabled(editable && type == Link::Type::Prismatic);
    damperCoefficient->setEnabled(editable && type == Link::Type::Prismatic && damperEnabled->isChecked());
    stitchAnchorMode->setEnabled(editable && stitch);
    stitchStiffness->setEnabled(editable && stitch);
    stitchRest->setEnabled(editable && stitch);
    for (auto *widget : {(QWidget *)stitchPairs, (QWidget *)pairsAdd, (QWidget *)pairsRemove, (QWidget *)pairsAuto,
                         (QWidget *)pairsPickA, (QWidget *)pairsPickB, (QWidget *)stitchPairInfo})
        widget->setEnabled(editable && stitch);
    if (!editable && pickTarget) {
        pickTarget = 0;
        viewport->anchorPicking = false;
    }

    const auto *a = scene->find(linkBodyA->currentData().toString());
    const auto *b = scene->find(linkBodyB->currentData().toString());
    if (a && b) {
        QVector3D localA(linkAnchorA[0]->value(), linkAnchorA[1]->value(), linkAnchorA[2]->value());
        QVector3D localB(linkAnchorB[0]->value(), linkAnchorB[1]->value(), linkAnchorB[2]->value());
        const auto worldA = a->anchorMatrix().map(localA), worldB = b->anchorMatrix().map(localB);
        const float distance = (worldA - worldB).length();
        linkPivotHint->setText(QStringLiteral("A 世界 (%1, %2, %3)  ·  B 世界 (%4, %5, %6)%7")
                                   .arg(worldA.x(), 0, 'f', 3)
                                   .arg(worldA.y(), 0, 'f', 3)
                                   .arg(worldA.z(), 0, 'f', 3)
                                   .arg(worldB.x(), 0, 'f', 3)
                                   .arg(worldB.y(), 0, 'f', 3)
                                   .arg(worldB.z(), 0, 'f', 3)
                                   .arg(distance > 1e-4f
                                            ? QStringLiteral("\n⚠ 两侧锚点相距 %1 m；点击“两部件中点”或编辑连接点可将它们对齐。")
                                                  .arg(distance, 0, 'f', 4)
                                            : QStringLiteral("\n两侧锚点已重合，形成清晰的装配连接点。")));
    } else {
        linkPivotHint->setText(QStringLiteral("选择两个部件后，可在这里设置共同的连接点。"));
    }
}

void MainWindow::syncPivotFromAnchors() {
    if (!linkPivot[0] || assemblySyncing)
        return;
    const auto *a = scene->find(linkBodyA->currentData().toString());
    const auto *b = scene->find(linkBodyB->currentData().toString());
    if (!a || !b)
        return;
    const QVector3D localA(linkAnchorA[0]->value(), linkAnchorA[1]->value(), linkAnchorA[2]->value());
    const QVector3D localB(linkAnchorB[0]->value(), linkAnchorB[1]->value(), linkAnchorB[2]->value());
    const auto pivot = (a->anchorMatrix().map(localA) + b->anchorMatrix().map(localB)) * .5f;
    {
        const QSignalBlocker blockX(linkPivot[0]), blockY(linkPivot[1]), blockZ(linkPivot[2]);
        for (int i = 0; i < 3; ++i)
            linkPivot[size_t(i)]->setValue(pivot[i]);
    }
    updateLinkComposer();
}

bool MainWindow::applyPivotToAnchors() {
    const auto *a = scene->find(linkBodyA->currentData().toString());
    const auto *b = scene->find(linkBodyB->currentData().toString());
    if (!a || !b || assemblySyncing)
        return false;
    const QVector3D pivot(float(linkPivot[0]->value()), float(linkPivot[1]->value()), float(linkPivot[2]->value()));
    const auto localA = a->anchorMatrix().inverted().map(pivot);
    const auto localB = b->anchorMatrix().inverted().map(pivot);
    {
        const QSignalBlocker blockAx(linkAnchorA[0]), blockAy(linkAnchorA[1]), blockAz(linkAnchorA[2]);
        const QSignalBlocker blockBx(linkAnchorB[0]), blockBy(linkAnchorB[1]), blockBz(linkAnchorB[2]);
        for (int i = 0; i < 3; ++i) {
            linkAnchorA[size_t(i)]->setValue(localA[i]);
            linkAnchorB[size_t(i)]->setValue(localB[i]);
        }
    }
    updateLinkComposer();
    return true;
}

void MainWindow::updateLinkDiagnostics() {
    if (!linkDiagnostics)
        return;
    auto *document = activeDocument();
    const int selected = selectedLinkIndex();
    if (!document || !document->simulation.active() || document->simulation.linkStats().isEmpty()) {
        if (selected >= 0 && selected < scene->links.size()) {
            const auto &link = scene->links[selected];
            linkDiagnostics->setText(QStringLiteral("%1：A = %2，B = %3。%4启动模拟后这里显示行程 / 角度 / 缝合误差。")
                                         .arg(linkTypeName(link.type), bodyText(scene, link.a),
                                              bodyText(scene, link.b), linkSummary(link) + QStringLiteral("；")));
        } else {
            linkDiagnostics->setText(QStringLiteral(
                "选中一条约束可加载到编排器；运行后这里显示每条约束的实时行程 / 角度 / 缝合误差。"));
        }
        linkTree->setToolTip(linkDiagnostics->text());
        return;
    }
    QStringList parts;
    const QString selectedId = selected >= 0 ? scene->links[selected].id : QString{};
    for (const auto &stat : document->simulation.linkStats()) {
        QString text = stat.id;
        if (stat.hasDistance)
            text += QStringLiteral(" d=%1").arg(stat.distance, 0, 'f', 4);
        if (stat.hasAngle)
            text += QStringLiteral(" θ=%1").arg(stat.angle, 0, 'f', 4);
        const auto link =
            std::find_if(scene->links.cbegin(), scene->links.cend(), [&](const Link &l) { return l.id == stat.id; });
        if (link != scene->links.cend()) {
            if (link->driven)
                text += QStringLiteral(" 目标=%1").arg(stat.aim, 0, 'f', 4);
            if (link->damped)
                text += QStringLiteral(" v=%1 m/s  F=%2 N").arg(stat.velocity, 0, 'f', 4).arg(stat.force, 0, 'f', 2);
        }
        if (!stat.hasDistance && !stat.hasAngle)
            text += QStringLiteral(" 误差=%1").arg(stat.error, 0, 'g', 3);
        const double value = stat.hasDistance ? stat.distance : stat.angle;
        const bool outOfRange = stat.hasLimit && (stat.hasDistance || stat.hasAngle) &&
                                (value < stat.limitLower || value > stat.limitUpper);
        if (outOfRange)
            text += QStringLiteral(" ⚠越界");
        for (int i = 0; i < linkTree->topLevelItemCount(); ++i) {
            auto *item = linkTree->topLevelItem(i);
            if (item->data(1, Qt::UserRole + 1).toString() == stat.id) {
                item->setText(2, text);
                item->setToolTip(2, text);
                if (outOfRange)
                    item->setForeground(2, QColor("#b42318"));
            }
        }
        if (stat.id == selectedId)
            parts.push_back(text);
    }
    linkDiagnostics->setText(parts.isEmpty() ? QStringLiteral("运行中：选中约束查看该约束的实时诊断。")
                                             : parts.join(QStringLiteral("  ·  ")));
    linkTree->setToolTip(linkDiagnostics->text());
}

void MainWindow::handleViewportPick(const QVector3D &world, const QString &objectId) {
    if (pickTarget == 0 || editingLocked())
        return;
    auto *object = scene->find(objectId);
    if (!object) {
        log(QStringLiteral("没有点中物体；请点击模型表面。"));
        return;
    }
    // Stitch pair picking: choose the nearest A anchor, then the nearest B
    // anchor or mesh vertex, without asking the user to type raw indices.
    if (pickTarget == 7 || pickTarget == 8) {
        const auto *a = scene->find(linkBodyA->currentData().toString());
        const auto *b = scene->find(linkBodyB->currentData().toString());
        if (!a || !b) {
            setLinkError(QStringLiteral("请先为配对选择部件 A 和 B。"));
            return;
        }
        int row = stitchPairs->currentRow();
        if (row < 0) {
            row = stitchPairs->rowCount();
            setStitchRow(row, 0, 0);
            stitchPairs->setCurrentCell(row, 0);
        }
        if (pickTarget == 7) {
            if (object->id != a->id) {
                setLinkError(QStringLiteral("这是部件 B；请点击部件 A（%1）的表面。").arg(a->name));
                return;
            }
            const int anchor = nearestAnchor(*a, world);
            if (anchor < 0) {
                setLinkError(QStringLiteral("部件 A 没有锚点；请先在“锚点”页添加。"));
                return;
            }
            const int oldTarget = stitchPairTarget(row);
            setStitchRow(row, anchor, oldTarget);
            stitchPairs->setCurrentCell(row, 0);
            pickTarget = 8;
            viewport->anchorPicking = true;
            setLinkError({});
            statusBar()->showMessage(QStringLiteral("已选 A 锚点 #%1；继续点击部件 B 的表面选择目标").arg(anchor));
        } else {
            if (object->id != b->id) {
                setLinkError(QStringLiteral("这是部件 A；请点击部件 B（%1）的表面。").arg(b->name));
                return;
            }
            const int target = stitchAnchorMode->isChecked() ? nearestAnchor(*b, world) : nearestVertex(*b, world);
            if (target < 0) {
                setLinkError(QStringLiteral("部件 B 没有可用的锚点或网格顶点。"));
                return;
            }
            const int anchor = stitchPairAnchor(row);
            setStitchRow(row, anchor, target);
            stitchPairs->setCurrentCell(row, 0);
            pickTarget = 0;
            viewport->anchorPicking = false;
            setLinkError({});
            statusBar()->showMessage(stitchAnchorMode->isChecked()
                                         ? QStringLiteral("已选 B 锚点 #%1；配对已更新").arg(target)
                                         : QStringLiteral("已选 B 顶点 #%1；配对已更新").arg(target),
                                     3000);
        }
        updateStitchPairInfo();
        updateLinkComposer();
        return;
    }
    // Body slot picking: A then automatically continue with B.
    if (pickTarget == 4 || pickTarget == 5) {
        linkComposerTouched = true;
        auto *combo = pickTarget == 4 ? linkBodyA : linkBodyB;
        const int index = combo->findData(object->id);
        if (index >= 0) {
            const QSignalBlocker blocker(combo);
            combo->setCurrentIndex(index);
        }
        if (pickTarget == 4) {
            pickTarget = 5;
            viewport->anchorPicking = true;
            statusBar()->showMessage(QStringLiteral("已选部件 A：%1；继续点击部件 B").arg(object->name));
        } else {
            pickTarget = 0;
            viewport->anchorPicking = false;
            statusBar()->showMessage(QStringLiteral("已选部件 B：%1").arg(object->name), 2500);
            syncPivotFromAnchors();
        }
        updateLinkComposer();
        return;
    }
    // A world-space pivot places both local anchors on the same point.
    if (pickTarget == 6) {
        linkComposerTouched = true;
        {
            const QSignalBlocker blockX(linkPivot[0]), blockY(linkPivot[1]), blockZ(linkPivot[2]);
            for (int i = 0; i < 3; ++i)
                linkPivot[size_t(i)]->setValue(world[i]);
        }
        applyPivotToAnchors();
        pickTarget = 0;
        viewport->anchorPicking = false;
        statusBar()->showMessage(QStringLiteral("连接点已设为 (%1, %2, %3)")
                                     .arg(world.x(), 0, 'f', 3)
                                     .arg(world.y(), 0, 'f', 3)
                                     .arg(world.z(), 0, 'f', 3),
                                 3000);
        updateLinkComposer();
        return;
    }
    auto *combo = pickTarget == 1 ? linkBodyA : linkBodyB;
    auto &anchorEditor = pickTarget == 1 ? linkAnchorA : linkAnchorB;
    linkComposerTouched = true;
    auto local = object->anchorMatrix().inverted().map(world);
    // Optional geometry snapping turns a free surface hit into an exact vertex
    // or face centre, which is what assembly constraints normally need.
    const QString snap = anchorSnap ? anchorSnap->currentData().toString() : QStringLiteral("surface");
    if (snap != QStringLiteral("surface")) {
        const auto bounds = localBounds(*object);
        QVector<QVector3D> candidates;
        if (snap == QStringLiteral("vertex")) {
            for (int i = 0; i < 8; ++i)
                candidates.push_back(object->matrix().map({(i & 1) ? bounds.second.x() : bounds.first.x(),
                                                           (i & 2) ? bounds.second.y() : bounds.first.y(),
                                                           (i & 4) ? bounds.second.z() : bounds.first.z()}));
        } else if (snap == QStringLiteral("face")) {
            const auto center = (bounds.first + bounds.second) * .5f;
            for (int axis = 0; axis < 3; ++axis)
                for (float side : {bounds.first[axis], bounds.second[axis]}) {
                    auto point = center;
                    point[axis] = side;
                    candidates.push_back(object->matrix().map(point));
                }
        } else if (snap == QStringLiteral("meshvertex") && object->mesh) {
            for (const auto &p : object->mesh->positions)
                candidates.push_back(object->matrix().map(p));
        }
        float best = 1e30f;
        QVector3D snappedWorld = world;
        for (const auto &candidate : candidates) {
            const float distance = (candidate - world).lengthSquared();
            if (distance < best) {
                best = distance;
                snappedWorld = candidate;
            }
        }
        local = object->anchorMatrix().inverted().map(snappedWorld);
    }
    if (pickTarget == 3) {
        if (object->deformable) {
            log(QStringLiteral("虚拟锚点只能添加到刚体上；弹性体连接使用网格顶点。"));
            return;
        }
        const QString objectIdCopy = object->id, objectNameCopy = object->name;
        auto next = *object;
        next.anchors.push_back(local);
        scene->edit(next, QStringLiteral("添加锚点（视口）"));
        scene->select(objectIdCopy);
        viewport->setAnchorHighlight(objectIdCopy, int(next.anchors.size()) - 1);
        log(QStringLiteral("已在 %1 添加锚点 #%2：(%3, %4, %5)")
                .arg(objectNameCopy)
                .arg(next.anchors.size() - 1)
                .arg(local.x(), 0, 'f', 3)
                .arg(local.y(), 0, 'f', 3)
                .arg(local.z(), 0, 'f', 3));
        pickTarget = 0;
        viewport->anchorPicking = false;
        syncAssemblyPanel();
        return;
    }
    const int index = combo->findData(object->id);
    if (index >= 0) {
        const QSignalBlocker blocker(combo);
        combo->setCurrentIndex(index);
    }
    for (int i = 0; i < 3; ++i)
        anchorEditor[size_t(i)]->setValue(local[i]);
    if (pickTarget == 1)
        viewport->setAnchorHighlight(object->id, -1);
    statusBar()->showMessage(QStringLiteral("已拾取%1锚点：%2 局部坐标 (%3, %4, %5)")
                                 .arg(pickTarget == 1 ? QStringLiteral(" A ") : QStringLiteral(" B "), object->name)
                                 .arg(local.x(), 0, 'f', 3)
                                 .arg(local.y(), 0, 'f', 3)
                                 .arg(local.z(), 0, 'f', 3),
                             4000);
    pickTarget = 0;
    viewport->anchorPicking = false;
    syncPivotFromAnchors();
    updateLinkComposer();
}
} // namespace bagua
