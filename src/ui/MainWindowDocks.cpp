#include "MainWindow.h"
#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QIcon>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyle>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>

namespace bagua {
void MainWindow::setupDocks() {
    auto *sceneDock = new QDockWidget(QStringLiteral("场景"), this);
    sceneDock->setObjectName("sceneDock");
    sceneDock->setMinimumWidth(165);
    auto *scenePanel = new QWidget;
    auto *scenePanelLayout = new QVBoxLayout(scenePanel);
    scenePanelLayout->setContentsMargins(4, 4, 4, 4);
    scenePanelLayout->setSpacing(4);
    sceneFilter = new QLineEdit;
    sceneFilter->setObjectName("sceneFilter");
    sceneFilter->setPlaceholderText(QStringLiteral("搜索对象 / 类型…"));
    sceneFilter->setClearButtonEnabled(true);
    sceneFilter->setMinimumHeight(22);
    scenePanelLayout->addWidget(sceneFilter);
    tree = new QTreeWidget;
    tree->setObjectName("sceneTree");
    tree->setHeaderLabels({QStringLiteral("对象"), QStringLiteral("类型")});
    tree->setRootIsDecorated(false);
    tree->setAlternatingRowColors(true);
    tree->setUniformRowHeights(true);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->header()->setStretchLastSection(false);
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    scenePanelLayout->addWidget(tree, 1);
    sceneDock->setWidget(scenePanel);
    addDockWidget(Qt::LeftDockWidgetArea, sceneDock);
    connect(sceneFilter, &QLineEdit::textChanged, this, [this] { syncSceneFilter(); });
    connect(tree, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (syncing)
            return;
        viewport->cancelDrag();
        auto items = tree->selectedItems();
        displayScene()->select(items.isEmpty() ? QString{} : items.first()->data(0, Qt::UserRole).toString());
    });
    connect(tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *, int) {
        viewport->frameSelected();
    });
    connect(tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *i, int column) {
        if (syncing || editingLocked() || column != 0)
            return;
        auto *p = scene->find(i->data(0, Qt::UserRole).toString());
        if (!p)
            return;
        Object o = *p;
        o.name = i->text(0).trimmed();
        if (o.name.isEmpty()) {
            syncTree();
            return;
        }
        o.visible = i->checkState(0) == Qt::Checked;
        scene->edit(o, QStringLiteral("修改对象"));
    });
    connect(tree, &QTreeWidget::customContextMenuRequested, this, [this](QPoint point) {
        auto *item = tree->itemAt(point);
        if (!item)
            return;
        const QString id = item->data(0, Qt::UserRole).toString();
        QMenu menu;
        auto *focus = menu.addAction(QStringLiteral("聚焦选中对象"));
        focus->setShortcut(QKeySequence("F"));
        connect(focus, &QAction::triggered, this, [this, id] {
            displayScene()->select(id);
            viewport->frameSelected();
        });
        auto *rename = menu.addAction(QStringLiteral("重命名"));
        connect(rename, &QAction::triggered, this, [this, item] { tree->editItem(item, 0); });
        menu.addSeparator();
        menu.addAction(duplicateAction);
        menu.addAction(deleteAction);
        menu.addSeparator();
        auto *asA = menu.addAction(QStringLiteral("在装配中作为部件 A"));
        connect(asA, &QAction::triggered, this, [this, id] {
            if (auto *combo = findChild<QComboBox *>("linkBodyA"); combo && combo->findData(id) >= 0) {
                combo->setCurrentIndex(combo->findData(id));
                if (auto *dock = findChild<QDockWidget *>("assemblyDock")) {
                    dock->show();
                    dock->raise();
                }
                if (auto *tabs = findChild<QTabWidget *>("assemblyTabs"))
                    tabs->setCurrentIndex(0);
            }
        });
        auto *asB = menu.addAction(QStringLiteral("在装配中作为部件 B"));
        connect(asB, &QAction::triggered, this, [this, id] {
            if (auto *combo = findChild<QComboBox *>("linkBodyB"); combo && combo->findData(id) >= 0) {
                combo->setCurrentIndex(combo->findData(id));
                if (auto *dock = findChild<QDockWidget *>("assemblyDock")) {
                    dock->show();
                    dock->raise();
                }
                if (auto *tabs = findChild<QTabWidget *>("assemblyTabs"))
                    tabs->setCurrentIndex(0);
            }
        });
        menu.exec(tree->viewport()->mapToGlobal(point));
    });
    auto *properties = new QDockWidget(QStringLiteral("属性"), this);
    properties->setObjectName("inspectorDock");
    properties->setMinimumWidth(280);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    inspector = new QWidget;
    auto *layout = new QVBoxLayout(inspector);
    layout->setSpacing(6);
    layout->setContentsMargins(6, 6, 6, 6);
    auto *objectGroup = new CollapsibleGroupBox(QStringLiteral("对象"), inspector);
    auto *objectForm = new QFormLayout(objectGroup->body());
    objectForm->setContentsMargins(0, 0, 0, 0);
    objectForm->setSpacing(4);
    typeLabel = new QLabel;
    objectForm->addRow(QStringLiteral("类型"), typeLabel);
    name = new QLineEdit;
    name->setObjectName("objectName");
    name->setMaxLength(160);
    name->setMinimumHeight(22);
    objectForm->addRow(QStringLiteral("名称"), name);
    visible = new QCheckBox(QStringLiteral("在视口中显示"));
    visible->setObjectName("objectVisible");
    objectForm->addRow(QStringLiteral("显示"), visible);
    layout->addWidget(objectGroup);
    auto *transformGroup = new CollapsibleGroupBox(QStringLiteral("变换"), inspector);
    auto *transformGrid = new QGridLayout(transformGroup->body());
    auto *geometryGroup = new CollapsibleGroupBox(QStringLiteral("几何"), inspector);
    auto *geometryGrid = new QGridLayout(geometryGroup->body());
    for (auto *grid : {transformGrid, geometryGrid}) {
        grid->setContentsMargins(0, 2, 0, 0);
        grid->setHorizontalSpacing(4);
        grid->setVerticalSpacing(4);
        for (int a = 0; a < 3; ++a) {
            auto *axis = new QLabel(QString("XYZ").mid(a, 1));
            axis->setAlignment(Qt::AlignCenter);
            auto palette = axis->palette();
            palette.setColor(QPalette::WindowText, QColor(QStringList{"#b7333b", "#168343", "#246bc9"}[a]));
            axis->setPalette(palette);
            grid->addWidget(axis, 0, a + 1);
            grid->setColumnStretch(a + 1, 1);
        }
        auto *resetHeader = new QLabel(QStringLiteral("↺"));
        resetHeader->setToolTip(QStringLiteral("每行重置为默认值"));
        resetHeader->setAlignment(Qt::AlignCenter);
        resetHeader->setStyleSheet(QStringLiteral("color:#7b8794;"));
        grid->addWidget(resetHeader, 0, 4);
    }
    const QStringList rows = {QStringLiteral("位置 (m)"), QStringLiteral("旋转 (°)"), QStringLiteral("缩放"),
                              QStringLiteral("尺寸 (m)")};
    for (int row = 0; row < 4; ++row) {
        auto *grid = row < 3 ? transformGrid : geometryGrid;
        const int gridRow = row < 3 ? row + 1 : 1;
        grid->addWidget(new QLabel(rows[row]), gridRow, 0);
        for (int a = 0; a < 3; ++a) {
            auto *field = new DragSpinBox;
            fields[row * 3 + a] = field;
            field->setObjectName(QString("field_%1_%2").arg(row).arg(a));
            field->setDecimals(3);
            field->setRange(row >= 2 ? .001 : -1e6, 1e6);
            field->setSingleStep(row == 1 ? 1 : .01);
            field->setScrubStep(row == 1 ? .25 : .002);
            field->setResetValue(row == 2 ? 1 : 0);
            field->setMinimumWidth(0);
            field->setMinimumHeight(22);
            field->setAlignment(Qt::AlignRight);
            field->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            field->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
            field->setToolTip(rows[row] + " / " + QString("XYZ").mid(a, 1) +
                              QStringLiteral("\n拖动数值可刷值，Shift 更精细，Esc 还原。"));
            grid->addWidget(field, gridRow, a + 1);
            connect(field, &QDoubleSpinBox::editingFinished, this, &MainWindow::commitFields);
            // Unity/Unreal style live preview: scrub the field and watch the
            // object move; one undo command is committed when the drag ends.
            connect(field, &DragSpinBox::scrubStarted, this, [this] {
                if (editingLocked() || !scene->current())
                    return;
                inspectorScrubBefore = *scene->current();
                inspectorScrubbing = true;
            });
            connect(field, &DragSpinBox::scrubChanged, this, [this] {
                if (!inspectorScrubbing || !scene->current())
                    return;
                Object o = *scene->current();
                QVector3D *vectors[] = {&o.position, &o.rotation, &o.scale, &o.dimensions};
                for (int index = 0; index < 12; ++index)
                    (*vectors[index / 3])[index % 3] = float(fields[index]->value());
                scene->preview(o);
            });
            connect(field, &DragSpinBox::scrubFinished, this, [this] {
                if (!inspectorScrubbing)
                    return;
                inspectorScrubbing = false;
                if (!inspectorScrubBefore.id.isEmpty() && scene->current())
                    scene->finishPreview(inspectorScrubBefore, QStringLiteral("修改变换或几何尺寸"));
                inspectorScrubBefore = {};
            });
        }
        auto *reset = new QToolButton;
        reset->setObjectName(QString("resetRow%1").arg(row));
        reset->setText(QStringLiteral("↺"));
        reset->setAutoRaise(true);
        reset->setToolTip(QStringLiteral("把“%1”恢复为默认值").arg(rows[row]));
        reset->setFixedWidth(22);
        resetRow[size_t(row)] = reset;
        grid->addWidget(reset, gridRow, 4);
        connect(reset, &QToolButton::clicked, this, [this, row] { resetInspectorRow(row); });
    }
    geometryGroup->setToolTip(
        QStringLiteral("球 / 圆柱：X、Z 为直径，Y 为高度。\n平面位于局部 XZ 面，Y 尺寸不参与显示。"));
    layout->addWidget(transformGroup);
    layout->addWidget(geometryGroup);
    auto *appearanceGroup = new CollapsibleGroupBox(QStringLiteral("外观"), inspector);
    auto *appearanceForm = new QFormLayout(appearanceGroup->body());
    appearanceForm->setContentsMargins(0, 0, 0, 0);
    color = new QPushButton(QStringLiteral("选择颜色…"));
    color->setObjectName("objectColor");
    color->setMinimumHeight(22);
    appearanceForm->addRow(QStringLiteral("颜色"), color);
    layout->addWidget(appearanceGroup);
    auto *physicsGroup = new CollapsibleGroupBox(QStringLiteral("刚体物理"), inspector);
    auto *physicsForm = new QFormLayout(physicsGroup->body());
    physicsForm->setContentsMargins(0, 0, 0, 0);
    physicsForm->setSpacing(4);
    collision = new QCheckBox(QStringLiteral("启用碰撞"));
    collision->setObjectName("objectCollision");
    collision->setToolTip(QStringLiteral("关闭后仍受重力影响，但不与其他物体发生接触。隐藏对象仍参与物理模拟。"));
    fixedBody = new QCheckBox(QStringLiteral("固定刚体"));
    fixedBody->setObjectName("objectFixed");
    fixedBody->setToolTip(
        QStringLiteral("固定网格使用原始三角面碰撞；动态网格使用凸包和包围盒近似惯量。平面始终固定。"));
    mass = new DragSpinBox;
    mass->setObjectName("objectMass");
    mass->setDecimals(3);
    mass->setRange(.001, 1e9);
    mass->setKeyboardTracking(false);
    mass->setScrubStep(.02);
    physicsForm->addRow(QStringLiteral("接触"), collision);
    physicsForm->addRow(QStringLiteral("运动"), fixedBody);
    physicsForm->addRow(QStringLiteral("质量 (kg)"), mass);
    layout->addWidget(physicsGroup);
    auto commitPhysics = [this] {
        if (syncing || editingLocked() || !scene->current())
            return;
        Object o = *scene->current();
        if (sender() == collision)
            o.collisionEnabled = collision->isChecked();
        else if (sender() == fixedBody)
            o.fixed = fixedBody->isChecked() || o.shape == Shape::Plane;
        else if (sender() == mass)
            o.mass = mass->value();
        scene->edit(o, QStringLiteral("修改刚体参数"));
    };
    connect(collision, &QCheckBox::toggled, this, commitPhysics);
    connect(fixedBody, &QCheckBox::toggled, this, commitPhysics);
    connect(mass, &QDoubleSpinBox::editingFinished, this, commitPhysics);
    layout->addWidget(makeVelocityInspector());
    layout->addWidget(makeFemInspector());
    layout->addStretch();
    scroll->setWidget(inspector);
    properties->setWidget(scroll);
    addDockWidget(Qt::RightDockWidgetArea, properties);
    connect(name, &QLineEdit::editingFinished, this, [this] {
        if (syncing || !scene->current())
            return;
        Object o = *scene->current();
        o.name = name->text().trimmed();
        if (o.name.isEmpty()) {
            syncInspector();
            return;
        }
        scene->edit(o, QStringLiteral("重命名对象"));
    });
    connect(visible, &QCheckBox::toggled, this, [this](bool on) {
        if (syncing || !scene->current())
            return;
        Object o = *scene->current();
        o.visible = on;
        scene->edit(o, QStringLiteral("修改可见性"));
    });
    connect(color, &QPushButton::clicked, this, [this] {
        if (!scene->current())
            return;
        const QString id = scene->selected;
        QColor c = QColorDialog::getColor(scene->current()->color, this, QStringLiteral("对象颜色"));
        if (c.isValid() && scene->find(id)) {
            Object o = *scene->find(id);
            o.color = c;
            scene->edit(o, QStringLiteral("修改颜色"));
        }
    });
    auto *bottom = new QDockWidget(QStringLiteral("输出与资源"), this);
    bottom->setObjectName("workspaceDock");
    bottom->setMinimumHeight(90);
    auto *tabs = new QTabWidget;
    console = new QPlainTextEdit;
    console->setObjectName("logConsole");
    console->setReadOnly(true);
    console->setMaximumBlockCount(1000);
    QFont logFont(QStringLiteral("Microsoft YaHei UI"), 9);
    console->setFont(logFont);
    console->document()->setDocumentMargin(4);
    tabs->addTab(console, QStringLiteral("日志"));
    auto *assets = new QWidget;
    assets->setObjectName("geometryAssets");
    auto *assetLayout = new QHBoxLayout(assets);
    assetLayout->setContentsMargins(6, 6, 6, 6);
    assetLayout->setSpacing(4);
    assetLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    for (int i = 0; i < 4; ++i) {
        auto *button = new QPushButton(QStringList{"◇  ", "◉  ", "▥  ", "▱  "}[i] + shapeName(Shape(i)));
        button->setMinimumHeight(24);
        connect(button, &QPushButton::clicked, this, [this, i] {
            if (editingLocked())
                return;
            viewport->cancelDrag();
            scene->create(Shape(i));
        });
        assetLayout->addWidget(button);
    }
    assetLayout->addStretch();
    tabs->addTab(assets, QStringLiteral("基础几何资源"));
    bottom->setWidget(tabs);
    addDockWidget(Qt::BottomDockWidgetArea, bottom);
    resizeDocks({sceneDock, properties}, {200, 320}, Qt::Horizontal);
    resizeDocks({bottom}, {120}, Qt::Vertical);
    auto *view = menuBar()->actions().at(3)->menu();
    view->addSeparator();
    for (auto *dock : {sceneDock, properties, bottom})
        view->addAction(dock->toggleViewAction());
}
void MainWindow::syncTree() {
    if (!scene || !tree)
        return;
    syncAssets();
    QSignalBlocker b(tree);
    syncing = true;
    const int scroll = tree->verticalScrollBar()->value();
    bool rebuild = tree->topLevelItemCount() != displayScene()->objects.size();
    if (!rebuild)
        for (int n = 0; n < displayScene()->objects.size(); ++n)
            if (tree->topLevelItem(n)->data(0, Qt::UserRole).toString() != displayScene()->objects[n].id) {
                rebuild = true;
                break;
            }
    if (rebuild)
        tree->clear();
    int n = 0;
    for (const auto &o : displayScene()->objects) {
        auto *i = rebuild ? new QTreeWidgetItem(tree) : tree->topLevelItem(n);
        ++n;
        i->setText(0, o.name);
        i->setText(1, shapeName(o.shape));
        i->setData(0, Qt::UserRole, o.id);
        QPixmap swatch(10, 10);
        swatch.fill(Qt::transparent);
        {
            QPainter painter(&swatch);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(QPen(o.color.darker(135), 1));
            painter.setBrush(o.color);
            painter.drawEllipse(QRectF(1, 1, 8, 8));
        }
        i->setIcon(0, QIcon(swatch));
        i->setToolTip(1, o.color.name().toUpper());
        i->setFlags(i->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
        i->setCheckState(0, o.visible ? Qt::Checked : Qt::Unchecked);
        i->setToolTip(0, o.id);
        if (o.id == displayScene()->selected)
            tree->setCurrentItem(i);
    }
    tree->verticalScrollBar()->setValue(scroll);
    syncing = false;
    syncSceneFilter();
    syncSelection();
}
void MainWindow::syncSceneFilter() {
    if (!tree || !sceneFilter || !scene)
        return;
    const QString needle = sceneFilter->text().trimmed();
    QSignalBlocker blocker(tree);
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        auto *item = tree->topLevelItem(i);
        const bool match = needle.isEmpty() || item->text(0).contains(needle, Qt::CaseInsensitive) ||
                           item->text(1).contains(needle, Qt::CaseInsensitive);
        item->setHidden(!match);
    }
    const QString selected = displayScene()->selected;
    if (!selected.isEmpty()) {
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            auto *item = tree->topLevelItem(i);
            if (item->data(0, Qt::UserRole).toString() == selected && !item->isHidden())
                tree->scrollToItem(item);
        }
    }
}
void MainWindow::syncSelection() {
    if (!tree || !scene)
        return;
    QSignalBlocker b(tree);
    tree->clearSelection();
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        auto *item = tree->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == displayScene()->selected) {
            tree->setCurrentItem(item);
            item->setSelected(true);
            break;
        }
    }
    deleteAction->setEnabled(displayScene()->current() != nullptr && !editingLocked());
    duplicateAction->setEnabled(displayScene()->current() != nullptr && !editingLocked());
    countLabel->setText(
        QStringLiteral("%1 个对象  ·  %2")
            .arg(displayScene()->objects.size())
            .arg(displayScene()->current() ? displayScene()->current()->name : QStringLiteral("未选择对象")));
}
void MainWindow::syncInspector() {
    if (!scene) {
        if (inspector)
            inspector->setEnabled(false);
        return;
    }
    // A numeric scrub previews the scene continuously. Re-syncing the fields
    // from the model on every preview would fight the drag and snap it back.
    if (inspectorScrubbing)
        return;
    syncing = true;
    const auto *o = displayScene()->current();
    inspector->setEnabled(o != nullptr && !editingLocked());
    syncFemInspector();
    syncVelocityInspector();
    collision->setChecked(o && o->collisionEnabled);
    fixedBody->setChecked(o && (o->fixed || o->shape == Shape::Plane));
    fixedBody->setEnabled(o && o->shape != Shape::Plane);
    {
        QSignalBlocker b(mass);
        mass->setValue(o ? o->mass : 1);
    }
    mass->setEnabled(o && !o->fixed && o->shape != Shape::Plane);
    typeLabel->setText(o ? shapeName(o->shape) : QStringLiteral("请选择一个对象"));
    name->setText(o ? o->name : QString{});
    visible->setChecked(o && o->visible);
    for (int row = 0; row < 4; ++row)
        for (int a = 0; a < 3; ++a) {
            auto *field = fields[row * 3 + a];
            QSignalBlocker b(field);
            field->setValue(o ? (row == 0   ? o->position[a]
                                 : row == 1 ? o->rotation[a]
                                 : row == 2 ? o->scale[a]
                                            : o->dimensions[a])
                              : 0);
            field->setEnabled(!(o && o->shape == Shape::Plane && row == 3 && a == 1));
        }
    for (auto *button : resetRow)
        if (button)
            button->setEnabled(o != nullptr && !editingLocked());
    if (o) {
        QPixmap swatch(16, 12);
        swatch.fill(o->color);
        color->setIcon(QIcon(swatch));
        color->setText(o->color.name().toUpper() + QStringLiteral("…"));
    } else {
        color->setIcon({});
        color->setText(QStringLiteral("选择颜色…"));
    }
    syncing = false;
    syncAssemblyPanel();
}
void MainWindow::resetInspectorRow(int row) {
    if (editingLocked() || !scene->current())
        return;
    Object o = *scene->current();
    if (row == 0)
        o.position = QVector3D(0, 0, 0);
    else if (row == 1)
        o.rotation = QVector3D(0, 0, 0);
    else if (row == 2)
        o.scale = QVector3D(1, 1, 1);
    else if (o.shape == Shape::Plane)
        o.dimensions = QVector3D(6, 1, 6);
    else
        o.dimensions = QVector3D(1, 1, 1);
    scene->edit(o, row == 3 ? QStringLiteral("重置几何尺寸") : QStringLiteral("重置变换"));
    syncInspector();
}
void MainWindow::commitFields() {
    if (syncing || editingLocked() || !scene->current())
        return;
    Object o = *scene->current();
    for (int index = 0; index < 12; ++index)
        if (sender() == fields[index]) {
            QVector3D *vectors[] = {&o.position, &o.rotation, &o.scale, &o.dimensions};
            (*vectors[index / 3])[index % 3] = float(fields[index]->value());
            break;
        }
    scene->edit(o, QStringLiteral("修改变换或几何尺寸"));
}
void MainWindow::finishInspectorEdit() {
    if (editingLocked())
        return;
    auto *focus = QApplication::focusWidget();
    if (!focus || !inspector || !inspector->isAncestorOf(focus))
        return;
    for (auto *field : fields)
        if (field == focus || field->isAncestorOf(focus)) {
            field->interpretText();
            QMetaObject::invokeMethod(field, "editingFinished", Qt::DirectConnection);
            return;
        }
    for (QAbstractSpinBox *field : std::array<QAbstractSpinBox *, 4>{femYoung, femPoisson, femStiffness, femResolution})
        if (focus == field || field->isAncestorOf(focus)) {
            field->interpretText();
            QMetaObject::invokeMethod(field, "editingFinished", Qt::DirectConnection);
            return;
        }
    for(auto *field:velocityFields)if(focus==field || field->isAncestorOf(focus)){field->interpretText();QMetaObject::invokeMethod(field,"editingFinished",Qt::DirectConnection);return;}
    if (focus == name)
        QMetaObject::invokeMethod(name, "editingFinished", Qt::DirectConnection);
    if (mass == focus || mass->isAncestorOf(focus)) {
        mass->interpretText();
        QMetaObject::invokeMethod(mass, "editingFinished", Qt::DirectConnection);
    }
}
} // namespace bagua

