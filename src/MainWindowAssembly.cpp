#include "MainWindow.h"
#include <QComboBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QUuid>
#include <algorithm>
namespace bagua {
namespace {
QDoubleSpinBox *vectorField(QWidget *parent, const char *objectName, double step = 0.01) {
    auto *field = new QDoubleSpinBox(parent);
    field->setObjectName(objectName);
    field->setRange(-1e5, 1e5);
    field->setDecimals(4);
    field->setSingleStep(step);
    field->setKeyboardTracking(false);
    field->setMinimumWidth(72);
    return field;
}
QString linkSummary(const Link &link) {
    if (link.type == Link::Type::Stitch) {
        QString text = QStringLiteral("%1 对 · k=%2").arg(link.pairs.size()).arg(link.stiffness, 0, 'g', 4);
        if (link.rest > 0)
            text += QStringLiteral(" · rest=%1").arg(link.rest, 0, 'g', 4);
        return text;
    }
    QString text = QStringLiteral("强度 %1").arg(link.strength, 0, 'g', 4);
    if (link.hasLimit)
        text += QStringLiteral(" · 限位 [%1, %2]").arg(link.lower, 0, 'g', 3).arg(link.upper, 0, 'g', 3);
    return text;
}
} // namespace
void MainWindow::setupAssemblyDock() {
    auto *dock = new QDockWidget(QStringLiteral("装配 · 链接"), this);
    dock->setObjectName("assemblyDock");
    auto *tabs = new QTabWidget;
    tabs->setObjectName("assemblyTabs");

    // --- links -------------------------------------------------------------------------
    auto *linkPage = new QWidget;
    auto *linkLayout = new QVBoxLayout(linkPage);
    linkLayout->setContentsMargins(6, 4, 6, 4);
    linkLayout->setSpacing(3);
    auto *topRow = new QHBoxLayout;
    topRow->setSpacing(4);
    linkType = new QComboBox;
    linkType->setObjectName("linkType");
    linkType->addItem(QStringLiteral("移动副"), QStringLiteral("prismatic"));
    linkType->addItem(QStringLiteral("转动副"), QStringLiteral("revolute"));
    linkType->addItem(QStringLiteral("球铰"), QStringLiteral("spherical"));
    linkType->addItem(QStringLiteral("固定副"), QStringLiteral("fixed"));
    linkType->addItem(QStringLiteral("缝合/弹簧"), QStringLiteral("stitch"));
    linkType->addItem(QStringLiteral("转动约束"), QStringLiteral("rotation"));
    linkBodyA = new QComboBox;
    linkBodyA->setObjectName("linkBodyA");
    linkBodyB = new QComboBox;
    linkBodyB->setObjectName("linkBodyB");
    linkAdd = new QPushButton(QStringLiteral("新建链接"));
    linkAdd->setObjectName("linkAdd");
    linkUpdate = new QPushButton(QStringLiteral("更新"));
    linkUpdate->setObjectName("linkUpdate");
    linkRemove = new QPushButton(QStringLiteral("删除"));
    linkRemove->setObjectName("linkRemove");
    topRow->addWidget(linkType);
    topRow->addWidget(new QLabel(QStringLiteral("A")));
    topRow->addWidget(linkBodyA, 1);
    topRow->addWidget(new QLabel(QStringLiteral("B")));
    topRow->addWidget(linkBodyB, 1);
    topRow->addWidget(linkAdd);
    topRow->addWidget(linkUpdate);
    topRow->addWidget(linkRemove);
    linkLayout->addLayout(topRow);

    auto *anchorRow = new QHBoxLayout;
    anchorRow->setSpacing(4);
    anchorRow->addWidget(new QLabel(QStringLiteral("锚点 A")));
    for (int i = 0; i < 3; ++i) {
        linkAnchorA[size_t(i)] = vectorField(linkPage, ("linkAnchorA" + QString(QChar("XYZ"[i]))).toUtf8());
        anchorRow->addWidget(linkAnchorA[size_t(i)]);
    }
    linkPickA = new QPushButton(QStringLiteral("在视口点选"));
    linkPickA->setObjectName("linkPickA");
    anchorRow->addWidget(linkPickA);
    anchorRow->addSpacing(8);
    anchorRow->addWidget(new QLabel(QStringLiteral("锚点 B")));
    for (int i = 0; i < 3; ++i) {
        linkAnchorB[size_t(i)] = vectorField(linkPage, ("linkAnchorB" + QString(QChar("XYZ"[i]))).toUtf8());
        anchorRow->addWidget(linkAnchorB[size_t(i)]);
    }
    linkPickB = new QPushButton(QStringLiteral("在视口点选"));
    linkPickB->setObjectName("linkPickB");
    anchorRow->addWidget(linkPickB);
    anchorRow->addStretch();
    linkAnchorRow = new QWidget;
    linkAnchorRow->setLayout(anchorRow);
    linkLayout->addWidget(linkAnchorRow);

    auto *axisRow = new QHBoxLayout;
    axisRow->setSpacing(4);
    axisRow->addWidget(new QLabel(QStringLiteral("轴（世界）")));
    for (int i = 0; i < 3; ++i) {
        linkAxis[size_t(i)] = vectorField(linkPage, ("linkAxis" + QString(QChar("XYZ"[i]))).toUtf8());
        axisRow->addWidget(linkAxis[size_t(i)]);
    }
    linkAxis[1]->setValue(1); // a vertical guide/hinge is the common case
    for (const char *name : {"X", "Y", "Z"}) {
        auto *preset = new QToolButton;
        preset->setText(QString::fromLatin1(name));
        preset->setAutoRaise(true);
        preset->setObjectName(QString("linkAxisPreset") + QString::fromLatin1(name));
        connect(preset, &QToolButton::clicked, this, [this, name] {
            const QString axis = QString::fromLatin1(name);
            for (int i = 0; i < 3; ++i)
                linkAxis[size_t(i)]->setValue(QString(QChar("XYZ"[i])) == axis ? 1.0 : 0.0);
        });
        axisRow->addWidget(preset);
    }
    axisRow->addSpacing(8);
    axisRow->addWidget(new QLabel(QStringLiteral("强度比值")));
    linkStrength = vectorField(linkPage, "linkStrength", 1);
    linkStrength->setRange(.001, 1e9);
    linkStrength->setDecimals(2);
    linkStrength->setValue(100);
    axisRow->addWidget(linkStrength);
    axisRow->addStretch();
    linkLayout->addLayout(axisRow);

    auto *limitRow = new QHBoxLayout;
    limitRow->setSpacing(4);
    limitEnabled = new QCheckBox(QStringLiteral("限位"));
    limitEnabled->setObjectName("limitEnabled");
    limitLower = vectorField(linkPage, "limitLower");
    limitUpper = vectorField(linkPage, "limitUpper");
    limitStrength = vectorField(linkPage, "limitStrength", 10);
    limitStrength->setRange(.001, 1e12);
    limitStrength->setValue(100);
    connect(limitEnabled, &QCheckBox::toggled, this, [this] { syncAssemblyPanel(); });
    limitRow->addWidget(limitEnabled);
    limitRow->addWidget(new QLabel(QStringLiteral("下限")));
    limitRow->addWidget(limitLower);
    limitRow->addWidget(new QLabel(QStringLiteral("上限")));
    limitRow->addWidget(limitUpper);
    limitRow->addWidget(new QLabel(QStringLiteral("限位强度")));
    limitRow->addWidget(limitStrength);
    limitRow->addStretch();
    linkLimitRow = new QWidget;
    linkLimitRow->setLayout(limitRow);
    linkLayout->addWidget(linkLimitRow);
    auto *rotationRow = new QHBoxLayout;
    rotationRow->addWidget(new QLabel(QStringLiteral("目标角（rad）")));
    rotationAngle = vectorField(linkPage, "rotationAngle");
    rotationRow->addWidget(rotationAngle);
    rotationRow->addWidget(new QLabel(QStringLiteral("角速度（rad/s）")));
    rotationSpeed = vectorField(linkPage, "rotationSpeed");
    rotationRow->addWidget(rotationSpeed);
    rotationRow->addStretch();
    linkRotationRow = new QWidget;
    linkRotationRow->setLayout(rotationRow);
    linkLayout->addWidget(linkRotationRow);

    auto *stitchRow = new QHBoxLayout;
    stitchRow->setSpacing(4);
    stitchAnchorMode = new QCheckBox(QStringLiteral("两侧都是锚点"));
    stitchAnchorMode->setObjectName("stitchAnchorMode");
    stitchStiffness = vectorField(linkPage, "stitchStiffness", 1000);
    stitchStiffness->setRange(.001, 1e12);
    stitchStiffness->setValue(20000);
    stitchRest = vectorField(linkPage, "stitchRest");
    stitchRest->setRange(0, 1e5);
    pairsAdd = new QPushButton(QStringLiteral("添加对"));
    pairsAdd->setObjectName("pairsAdd");
    pairsRemove = new QPushButton(QStringLiteral("删除对"));
    pairsRemove->setObjectName("pairsRemove");
    pairsAuto = new QPushButton(QStringLiteral("自动配对"));
    pairsAuto->setObjectName("pairsAuto");
    stitchRow->addWidget(stitchAnchorMode);
    stitchRow->addWidget(new QLabel(QStringLiteral("刚度")));
    stitchRow->addWidget(stitchStiffness);
    stitchRow->addWidget(new QLabel(QStringLiteral("静止长度")));
    stitchRow->addWidget(stitchRest);
    stitchRow->addWidget(pairsAdd);
    stitchRow->addWidget(pairsRemove);
    stitchRow->addWidget(pairsAuto);
    stitchRow->addStretch();
    linkStitchRow = new QWidget;
    linkStitchRow->setLayout(stitchRow);
    linkLayout->addWidget(linkStitchRow);

    driveRow = new QWidget;
    auto *driveLayout = new QHBoxLayout(driveRow);
    driveEnabled = new QCheckBox(QStringLiteral("启用驱动"));
    driveEnabled->setObjectName("driveEnabled");
    driveSpeed = vectorField(linkPage, "driveSpeed");
    driveSpeed->setRange(-1e4, 1e4);
    driveRamp = vectorField(linkPage, "driveRamp");
    driveRamp->setRange(0, 1000);
    driveRamp->setValue(.5);
    driveStrength = vectorField(linkPage, "driveStrength");
    driveStrength->setRange(.001, 1e12);
    driveStrength->setValue(1e4);
    driveLayout->addWidget(driveEnabled);
    driveLayout->addWidget(new QLabel(QStringLiteral("速度（rad/s 或 m/s）")));
    driveLayout->addWidget(driveSpeed);
    driveLayout->addWidget(new QLabel(QStringLiteral("斜坡（s）")));
    driveLayout->addWidget(driveRamp);
    driveLayout->addWidget(new QLabel(QStringLiteral("强度")));
    driveLayout->addWidget(driveStrength);
    driveLayout->addStretch();
    linkLayout->addWidget(driveRow);
    damperRow = new QWidget;
    auto *damperLayout = new QHBoxLayout(damperRow);
    damperEnabled = new QCheckBox(QStringLiteral("启用轴向阻尼"));
    damperEnabled->setObjectName("damperEnabled");
    damperCoefficient = vectorField(linkPage, "damperCoefficient");
    damperCoefficient->setRange(0, 1e9);
    damperLayout->addWidget(damperEnabled);
    damperLayout->addWidget(new QLabel(QStringLiteral("系数（N·s/m）")));
    damperLayout->addWidget(damperCoefficient);
    damperLayout->addStretch();
    linkLayout->addWidget(damperRow);
    connect(driveEnabled, &QCheckBox::toggled, this, [this] { syncAssemblyPanel(); });
    connect(damperEnabled, &QCheckBox::toggled, this, [this] { syncAssemblyPanel(); });
    stitchPairs = new QTableWidget;
    stitchPairs->setObjectName("stitchPairs");
    stitchPairs->setColumnCount(2);
    stitchPairs->setHorizontalHeaderLabels({QStringLiteral("A 锚点序号"), QStringLiteral("B 顶点 / 锚点序号")});
    stitchPairs->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    stitchPairs->verticalHeader()->setVisible(false);
    stitchPairs->setMaximumHeight(96);
    linkLayout->addWidget(stitchPairs);

    linkTree = new QTreeWidget;
    linkTree->setObjectName("linkTree");
    linkTree->setColumnCount(3);
    linkTree->setHeaderLabels({QStringLiteral("类型"), QStringLiteral("连接"), QStringLiteral("参数 / 实时")});
    linkTree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    linkTree->setRootIsDecorated(false);
    linkTree->setAlternatingRowColors(true);
    linkTree->setMinimumHeight(90);
    linkLayout->addWidget(linkTree, 1);
    linkDiagnostics = new QLabel(QStringLiteral("链接编辑：选中一条可加载到上方表单"));
    linkDiagnostics->setObjectName("linkDiagnostics");
    linkDiagnostics->setWordWrap(true);
    linkLayout->addWidget(linkDiagnostics);
    auto *linkScroll = new QScrollArea;
    linkScroll->setWidgetResizable(true);
    linkScroll->setFrameShape(QFrame::NoFrame);
    linkLayout->removeWidget(linkTree);
    linkLayout->removeWidget(linkDiagnostics);
    linkScroll->setWidget(linkPage);
    auto *linkTab = new QWidget;
    auto *linkTabLayout = new QVBoxLayout(linkTab);
    linkTabLayout->setContentsMargins(4, 2, 4, 2);
    linkTabLayout->setSpacing(2);
    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(linkTree);
    splitter->addWidget(linkScroll);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({300, 900});
    linkTabLayout->addWidget(splitter, 1);
    linkTabLayout->addWidget(linkDiagnostics);
    tabs->addTab(linkTab, QStringLiteral("链接"));

    // --- anchors -----------------------------------------------------------------------
    auto *anchorPage = new QWidget;
    auto *anchorLayout = new QVBoxLayout(anchorPage);
    anchorLayout->setContentsMargins(6, 4, 6, 4);
    anchorLayout->setSpacing(3);
    auto *anchorButtons = new QHBoxLayout;
    anchorAdd = new QPushButton(QStringLiteral("在物体中心添加"));
    anchorAdd->setObjectName("anchorAdd");
    anchorPick = new QPushButton(QStringLiteral("在视口点选添加"));
    anchorPick->setObjectName("anchorPick");
    anchorRemove = new QPushButton(QStringLiteral("删除锚点"));
    anchorRemove->setObjectName("anchorRemove");
    anchorButtons->addWidget(anchorAdd);
    anchorButtons->addWidget(anchorPick);
    anchorButtons->addWidget(anchorRemove);
    anchorButtons->addStretch();
    anchorLayout->addLayout(anchorButtons);
    auto *anchorEditRow = new QHBoxLayout;
    for (int i = 0; i < 3; ++i) {
        anchorEdit[size_t(i)] = vectorField(anchorPage, ("anchorEdit" + QString(QChar("XYZ"[i]))).toUtf8());
        anchorEditRow->addWidget(anchorEdit[size_t(i)]);
    }
    auto *applyAnchor = new QPushButton(QStringLiteral("更新锚点"));
    applyAnchor->setObjectName("anchorUpdate");
    anchorEditRow->addWidget(applyAnchor);
    anchorEditRow->addStretch();
    anchorLayout->addLayout(anchorEditRow);
    connect(applyAnchor, &QPushButton::clicked, this, [this] {
        const int i = anchorList->currentRow();
        const auto *o = scene->current();
        if (editingLocked() || !o || i < 0 || i >= o->anchors.size())
            return;
        auto next = *o;
        for (int k = 0; k < 3; ++k)
            next.anchors[i][k] = float(anchorEdit[size_t(k)]->value());
        scene->edit(next, QStringLiteral("更新锚点"));
    });

    anchorList = new QListWidget;
    anchorList->setObjectName("anchorList");
    anchorLayout->addWidget(anchorList, 1);
    connect(anchorList, &QListWidget::currentRowChanged, this, [this](int i) {
        auto *o = displayScene()->current();
        if (!o || i < 0 || i >= o->anchors.size())
            return;
        for (int k = 0; k < 3; ++k)
            anchorEdit[size_t(k)]->setValue(o->anchors[i][k]);
        viewport->setAnchorHighlight(o->id, i);
    });
    auto *anchorHint = new QLabel(
        QStringLiteral("锚点属于物体，保存为物体局部坐标；关节直接使用锚点 A/B 坐标，缝合链接按序号引用锚点。"
                       "选中物体后可在这里增删，视口里以绿色圆点显示。"));
    anchorHint->setWordWrap(true);
    anchorLayout->addWidget(anchorHint);
    tabs->addTab(anchorPage, QStringLiteral("锚点"));

    // --- exclusions --------------------------------------------------------------------
    auto *excludePage = new QWidget;
    auto *excludeLayout = new QVBoxLayout(excludePage);
    excludeLayout->setContentsMargins(6, 4, 6, 4);
    excludeLayout->setSpacing(3);
    auto *excludeRow = new QHBoxLayout;
    exclusionA = new QComboBox;
    exclusionA->setObjectName("exclusionBodyA");
    exclusionB = new QComboBox;
    exclusionB->setObjectName("exclusionBodyB");
    exclusionAdd = new QPushButton(QStringLiteral("关闭接触"));
    exclusionAdd->setObjectName("exclusionAdd");
    exclusionRemove = new QPushButton(QStringLiteral("恢复接触"));
    exclusionRemove->setObjectName("exclusionRemove");
    excludeRow->addWidget(exclusionA, 1);
    excludeRow->addWidget(exclusionB, 1);
    excludeRow->addWidget(exclusionAdd);
    excludeRow->addWidget(exclusionRemove);
    excludeRow->addStretch();
    excludeLayout->addLayout(excludeRow);
    exclusionList = new QListWidget;
    exclusionList->setObjectName("exclusionList");
    excludeLayout->addWidget(exclusionList, 1);
    auto *excludeHint =
        new QLabel(QStringLiteral("装配体内故意重叠的部件（导柱与滑块、轮毂与轮辋）需要关闭它们之间的接触，"
                                  "否则初始相交会让求解器直接拒绝启动。"));
    excludeHint->setWordWrap(true);
    excludeLayout->addWidget(excludeHint);
    tabs->addTab(excludePage, QStringLiteral("接触排除"));

    dock->setWidget(tabs);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
    assemblyPanel = dock;
    if (auto *recordDock = findChild<QDockWidget *>("replayDock")) {
        tabifyDockWidget(recordDock, dock);
        recordDock->raise();
    }
    menuBar()->actions()[3]->menu()->addAction(dock->toggleViewAction());

    connect(linkAdd, &QPushButton::clicked, this, [this] {
        Link link;
        QString error;
        if (!readLinkForm(link, &error)) {
            log(QStringLiteral("无法创建链接：") + error);
            return;
        }
        link.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto links = scene->links;
        links.push_back(link);
        if (!commitLinks(links, scene->exclusions, QStringLiteral("新建链接")))
            return;
        for (int i = 0; i < linkTree->topLevelItemCount(); ++i)
            if (linkTree->topLevelItem(i)->data(1, Qt::UserRole + 1).toString() == link.id)
                linkTree->setCurrentItem(linkTree->topLevelItem(i));
        log(QStringLiteral("已创建链接 %1：%2").arg(link.id, linkSummary(link)));
    });
    connect(linkUpdate, &QPushButton::clicked, this, [this] {
        const int index = selectedLinkIndex();
        if (index < 0) {
            log(QStringLiteral("请先在列表里选中要更新的链接。"));
            return;
        }
        Link link;
        QString error;
        if (!readLinkForm(link, &error)) {
            log(QStringLiteral("无法更新链接：") + error);
            return;
        }
        auto links = scene->links;
        link.id = links[index].id;
        links[index] = link;
        commitLinks(links, scene->exclusions, QStringLiteral("更新链接"));
    });
    connect(linkRemove, &QPushButton::clicked, this, &MainWindow::deleteSelectedLink);
    connect(linkTree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item, QTreeWidgetItem *) {
        if (assemblySyncing || syncing || !item)
            return;
        const int index = selectedLinkIndex();
        if (index >= 0)
            syncLinkForm(scene->links[index]);
    });
    connect(linkPickA, &QPushButton::clicked, this, [this] {
        pickTarget = 1;
        viewport->anchorPicking = true;
        statusBar()->showMessage(QStringLiteral("在视口点击一个物体，用作链接的 A 侧锚点（Esc 取消）"));
    });
    connect(linkPickB, &QPushButton::clicked, this, [this] {
        pickTarget = 2;
        viewport->anchorPicking = true;
        statusBar()->showMessage(QStringLiteral("在视口点击一个物体，用作链接的 B 侧锚点（Esc 取消）"));
    });
    connect(linkType, &QComboBox::currentIndexChanged, this, [this] { syncAssemblyPanel(); });
    connect(pairsAdd, &QPushButton::clicked, this, [this] {
        const int row = stitchPairs->rowCount();
        stitchPairs->insertRow(row);
        stitchPairs->setItem(row, 0, new QTableWidgetItem(QString::number(0)));
        stitchPairs->setItem(row, 1, new QTableWidgetItem(QString::number(0)));
    });
    connect(pairsRemove, &QPushButton::clicked, this, [this] {
        if (stitchPairs->currentRow() >= 0)
            stitchPairs->removeRow(stitchPairs->currentRow());
    });
    connect(pairsAuto, &QPushButton::clicked, this, [this] {
        // Match each A anchor to the closest unused B point in world space.
        const Object *a = scene->find(linkBodyA->currentData().toString());
        const Object *b = scene->find(linkBodyB->currentData().toString());
        if (!a || !b) {
            log(QStringLiteral("自动配对需要先选择两个物体。"));
            return;
        }
        const bool bothAnchors = stitchAnchorMode->isChecked();
        QVector<QVector3D> targets;
        if (bothAnchors)
            for (auto p : b->anchors)
                targets.push_back(b->anchorMatrix().map(p));
        else if (b->mesh)
            for (auto p : b->mesh->positions)
                targets.push_back(b->matrix().map(p));
        if (a->deformable || a->anchors.isEmpty() || targets.isEmpty()) {
            log(QStringLiteral("请为 A 刚体添加锚点，并为 B 选择锚点或网格顶点。"));
            return;
        }
        QSet<int> used;
        stitchPairs->setRowCount(0);
        int count = 0;
        for (int i = 0; i < a->anchors.size() && count < targets.size(); ++i) {
            auto p = a->anchorMatrix().map(a->anchors[i]);
            int best = -1;
            float distance = 1e30f;
            for (int j = 0; j < targets.size(); ++j)
                if (!used.contains(j) && (targets[j] - p).lengthSquared() < distance) {
                    best = j;
                    distance = (targets[j] - p).lengthSquared();
                }
            if (best < 0)
                break;
            used.insert(best);
            stitchPairs->insertRow(count);
            stitchPairs->setItem(count, 0, new QTableWidgetItem(QString::number(i)));
            stitchPairs->setItem(count, 1, new QTableWidgetItem(QString::number(best)));
            ++count;
        }
        log(QStringLiteral("已自动配对 %1 对。").arg(count));
    });
    connect(anchorAdd, &QPushButton::clicked, this, [this] {
        auto *o = scene->current();
        if (!o) {
            log(QStringLiteral("请先选中一个物体。"));
            return;
        }
        auto next = *o;
        next.anchors.push_back({0, 0, 0});
        scene->edit(next, QStringLiteral("添加锚点"));
        syncAssemblyPanel();
    });
    connect(anchorPick, &QPushButton::clicked, this, [this] {
        if (!scene->current()) {
            log(QStringLiteral("请先选中一个物体。"));
            return;
        }
        pickTarget = 3;
        viewport->anchorPicking = true;
        statusBar()->showMessage(QStringLiteral("在视口点击物体表面，把该点添加为锚点（Esc 取消）"));
    });
    connect(anchorRemove, &QPushButton::clicked, this, [this] {
        auto *o = scene->current();
        if (!o || !anchorList->currentItem())
            return;
        const int index = anchorList->currentItem()->data(Qt::UserRole).toInt();
        scene->removeAnchor(o->id, index);
        syncAssemblyPanel();
    });
    connect(exclusionAdd, &QPushButton::clicked, this, [this] {
        const QString a = exclusionA->currentData().toString(), b = exclusionB->currentData().toString();
        if (a.isEmpty() || b.isEmpty() || a == b) {
            log(QStringLiteral("接触排除需要两个不同的物体。"));
            return;
        }
        auto exclusions = scene->exclusions;
        for (const auto &pair : exclusions)
            if ((pair.first == a && pair.second == b) || (pair.first == b && pair.second == a))
                return;
        exclusions.push_back({a, b});
        commitLinks(scene->links, exclusions, QStringLiteral("关闭接触"));
    });
    connect(exclusionRemove, &QPushButton::clicked, this, [this] {
        if (!exclusionList->currentItem())
            return;
        const int index = exclusionList->currentItem()->data(Qt::UserRole).toInt();
        auto exclusions = scene->exclusions;
        if (index < 0 || index >= exclusions.size())
            return;
        exclusions.removeAt(index);
        commitLinks(scene->links, exclusions, QStringLiteral("恢复接触"));
    });
    connect(viewport, &Viewport::clicked, this, &MainWindow::handleViewportPick);
    connect(viewport, &Viewport::cancelled, this, [this] {
        if (pickTarget) {
            pickTarget = 0;
            viewport->anchorPicking = false;
            statusBar()->showMessage(QStringLiteral("已取消锚点拾取"), 2000);
        }
    });
    syncAssemblyPanel();
}
// Resolve the stable ID against the authored scene; rows can move after edits.
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
void MainWindow::deleteSelectedLink() {
    const int index = selectedLinkIndex();
    if (index < 0) {
        log(QStringLiteral("请先在列表里选中要删除的链接。"));
        return;
    }
    auto links = scene->links;
    links.removeAt(index);
    commitLinks(links, scene->exclusions, QStringLiteral("删除链接"));
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
        log(QStringLiteral("装配修改被拒绝：") + error);
        return false;
    }
    scene->editLinks(links, exclusions, label);
    syncAssemblyPanel();
    return true;
}
bool MainWindow::readLinkForm(Link &link, QString *error) const {
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    const QString type = linkType->currentData().toString();
    if (!Link::typeFromKey(type, link.type))
        return fail(QStringLiteral("未知链接类型"));
    link.a = linkBodyA->currentData().toString();
    link.b = linkBodyB->currentData().toString();
    if (link.a.isEmpty() || link.b.isEmpty())
        return fail(QStringLiteral("请选择两个物体"));
    if (link.a == link.b)
        return fail(QStringLiteral("不能把物体连到它自己"));
    const QString id = QStringLiteral("%1-%2-%3").arg(type, link.a, link.b);
    link.id = id;
    const int selected = selectedLinkIndex();
    if (selected >= 0 && scene->links[selected].type == Link::Type::Rotation) {
        link.angle = scene->links[selected].angle;
        link.angularVelocity = scene->links[selected].angularVelocity;
    }
    if (link.type == Link::Type::Stitch) {
        link.anchorPairs = stitchAnchorMode->isChecked();
        link.stiffness = stitchStiffness->value();
        link.rest = stitchRest->value();
        for (int row = 0; row < stitchPairs->rowCount(); ++row) {
            auto *a = stitchPairs->item(row, 0);
            auto *b = stitchPairs->item(row, 1);
            if (!a || !b)
                continue;
            bool okA = false, okB = false;
            const int ai = a->text().toInt(&okA), bi = b->text().toInt(&okB);
            if (!okA || !okB || ai < 0 || bi < 0)
                return fail(QStringLiteral("缝合序号必须是非负整数"));
            link.pairs.push_back({ai, bi});
        }
        if (link.pairs.isEmpty())
            return fail(QStringLiteral("缝合链接至少需要一对顶点"));
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
    link.driven =
        driveEnabled->isChecked() && (link.type == Link::Type::Revolute || link.type == Link::Type::Prismatic);
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
    link.hasLimit =
        limitEnabled->isChecked() && (link.type == Link::Type::Prismatic || link.type == Link::Type::Revolute);
    if (link.hasLimit) {
        if (link.type != Link::Type::Prismatic && link.type != Link::Type::Revolute)
            return fail(QStringLiteral("只有移动副和转动副支持限位"));
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
    for (int row = 0; row < link.pairs.size(); ++row) {
        stitchPairs->insertRow(row);
        stitchPairs->setItem(row, 0, new QTableWidgetItem(QString::number(link.pairs[row].first)));
        stitchPairs->setItem(row, 1, new QTableWidgetItem(QString::number(link.pairs[row].second)));
    }
    assemblySyncing = false;
    loadedLink = linkJson(link);
    syncAssemblyPanel();
}
void MainWindow::syncAssemblyPanel() {
    if (!linkTree || !scene || assemblySyncing)
        return;
    assemblySyncing = true;
    const auto *model = displayScene();
    // Object pickers keep their current choice when the object list changes.
    for (auto *combo : {linkBodyA, linkBodyB, exclusionA, exclusionB}) {
        const QString current = combo->currentData().toString();
        combo->clear();
        for (const auto &o : model->objects)
            combo->addItem(o.name + QStringLiteral(" (") + o.id.left(6) + QStringLiteral(")"), o.id);
        const int index = combo->findData(current);
        if (index >= 0)
            combo->setCurrentIndex(index);
        combo->setEnabled(!editingLocked());
    }
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
            item->setText(0, link.typeKey());
            item->setText(1, QStringLiteral("%1 → %2").arg(model->find(link.a) ? model->find(link.a)->name : link.a,
                                                           model->find(link.b) ? model->find(link.b)->name : link.b));
            item->setText(2, linkSummary(link));
            item->setToolTip(0, link.id);
            item->setData(0, Qt::UserRole, i);
            item->setData(1, Qt::UserRole + 1, link.id);
        }
        if (auto *selected = items.value(selectedId, nullptr); selected && linkTree->currentItem() != selected)
            linkTree->setCurrentItem(selected);
        if (!linkTree->currentItem() && linkTree->topLevelItemCount())
            linkTree->setCurrentItem(linkTree->topLevelItem(0));
        linkTree->verticalScrollBar()->setValue(scroll);
    }
    // Exclusions.
    exclusionList->clear();
    for (int i = 0; i < model->exclusions.size(); ++i) {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1 ↔ %2").arg(model->exclusions[i].first, model->exclusions[i].second), exclusionList);
        item->setData(Qt::UserRole, i);
    }
    // Anchors of the selected object.
    const int anchorRow = anchorList->currentRow();
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
    // Enable only the fields the chosen type uses.
    Link::Type type = Link::Type::Prismatic;
    Link::typeFromKey(linkType->currentData().toString(), type);
    const bool stitch = type == Link::Type::Stitch;
    linkAnchorRow->setVisible(!stitch);
    linkLimitRow->setVisible(type == Link::Type::Prismatic || type == Link::Type::Revolute);
    linkRotationRow->setVisible(type == Link::Type::Rotation);
    linkStitchRow->setVisible(stitch);
    stitchPairs->setVisible(stitch);
    const bool axial = type == Link::Type::Prismatic || type == Link::Type::Revolute || type == Link::Type::Rotation;
    const bool editable = !editingLocked();
    const bool controllable = type == Link::Type::Revolute || type == Link::Type::Prismatic;
    driveRow->setVisible(controllable);
    damperRow->setVisible(type == Link::Type::Prismatic);
    driveEnabled->setEnabled(editable);
    damperEnabled->setEnabled(editable);
    for (auto *field : {driveSpeed, driveRamp, driveStrength})
        field->setEnabled(editable && controllable && driveEnabled->isChecked());
    damperCoefficient->setEnabled(editable && type == Link::Type::Prismatic && damperEnabled->isChecked());
    if (!editable) {
        pickTarget = 0;
        viewport->anchorPicking = false;
    }
    for (int i = 0; i < 3; ++i) {
        linkAnchorA[size_t(i)]->setEnabled(editable && !stitch);
        linkAnchorB[size_t(i)]->setEnabled(editable && !stitch);
        linkAxis[size_t(i)]->setEnabled(editable && axial);
    }
    for (auto *button : {linkPickA, linkPickB})
        button->setEnabled(editable && !stitch);
    rotationAngle->setEnabled(editable && type == Link::Type::Rotation);
    rotationSpeed->setEnabled(editable && type == Link::Type::Rotation);
    linkStrength->setEnabled(editable && !stitch);
    limitEnabled->setEnabled(editable && axial && type != Link::Type::Rotation);
    for (auto *field : {limitLower, limitUpper, limitStrength})
        field->setEnabled(editable && type != Link::Type::Rotation && axial && limitEnabled->isChecked());
    stitchAnchorMode->setEnabled(editable && stitch);
    stitchStiffness->setEnabled(editable && stitch);
    stitchRest->setEnabled(editable && stitch);
    for (auto *widget : {(QWidget *)stitchPairs, (QWidget *)pairsAdd, (QWidget *)pairsRemove, (QWidget *)pairsAuto})
        widget->setEnabled(editable && stitch);
    for (auto *widget :
         {(QWidget *)linkAdd, (QWidget *)linkUpdate, (QWidget *)linkRemove, (QWidget *)anchorAdd, (QWidget *)anchorPick,
          (QWidget *)anchorRemove, (QWidget *)exclusionAdd, (QWidget *)exclusionRemove})
        widget->setEnabled(editable);
    const auto *currentObject = scene->current();
    for (auto *widget : {anchorAdd, anchorPick, anchorRemove, findChild<QPushButton *>("anchorUpdate")})
        widget->setEnabled(editable && currentObject && !currentObject->deformable);
    assemblySyncing = false;
    const int selected = selectedLinkIndex();
    if (selected >= 0 && linkJson(scene->links[selected]) != loadedLink)
        syncLinkForm(scene->links[selected]);
    else if (selected < 0)
        loadedLink = {};
    updateLinkDiagnostics();
}
void MainWindow::handleViewportPick(const QVector3D &world, const QString &objectId) {
    if (pickTarget == 0 || editingLocked())
        return;
    auto *object = scene->find(objectId);
    if (!object) {
        log(QStringLiteral("没有点中物体；请点击模型表面。"));
        return;
    }
    const QVector3D local = object->anchorMatrix().inverted().map(world);
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
    auto *combo = pickTarget == 1 ? linkBodyA : linkBodyB;
    auto &fields = pickTarget == 1 ? linkAnchorA : linkAnchorB;
    const int index = combo->findData(object->id);
    if (index >= 0)
        combo->setCurrentIndex(index);
    for (int i = 0; i < 3; ++i)
        fields[size_t(i)]->setValue(local[i]);
    if (pickTarget == 1)
        viewport->setAnchorHighlight(object->id, -1);
    statusBar()->showMessage(QStringLiteral("已拾取锚点：%1 局部坐标 (%2, %3, %4)")
                                 .arg(object->name)
                                 .arg(local.x(), 0, 'f', 3)
                                 .arg(local.y(), 0, 'f', 3)
                                 .arg(local.z(), 0, 'f', 3),
                             4000);
    pickTarget = 0;
    viewport->anchorPicking = false;
    syncAssemblyPanel();
}
void MainWindow::updateLinkDiagnostics() {
    if (!linkDiagnostics)
        return;
    auto *document = activeDocument();
    if (!document || !document->physics.active() || document->physics.linkStats().isEmpty()) {
        linkDiagnostics->setText(
            QStringLiteral("链接编辑：选中一条可加载到上方表单；运行后这里显示每条链接的实时行程 / 角度 / 缝合误差。"));
        return;
    }
    QStringList parts;
    const int selected = selectedLinkIndex();
    const QString selectedId = selected >= 0 ? scene->links[selected].id : QString{};
    for (const auto &stat : document->physics.linkStats()) {
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
        if (stat.hasLimit && (stat.hasDistance || stat.hasAngle) &&
            (value < stat.limitLower || value > stat.limitUpper))
            text += QStringLiteral(" ⚠越界");
        for (int i = 0; i < linkTree->topLevelItemCount(); ++i) {
            auto *item = linkTree->topLevelItem(i);
            if (item->data(1, Qt::UserRole + 1).toString() == stat.id) {
                item->setText(2, text);
                item->setToolTip(2, text);
            }
        }
        if (stat.id == selectedId)
            parts.push_back(text);
    }
    linkDiagnostics->setText(parts.join(QStringLiteral("  ·  ")));
}
} // namespace bagua
