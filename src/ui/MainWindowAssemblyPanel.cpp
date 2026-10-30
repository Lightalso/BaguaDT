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
void MainWindow::setupAssemblyDock() {
    auto *dock = new QDockWidget(QStringLiteral("装配 · 约束"), this);
    dock->setObjectName("assemblyDock");
    dock->setMinimumWidth(430);
    setTabPosition(Qt::RightDockWidgetArea, QTabWidget::North);
    auto *tabs = new QTabWidget;
    tabs->setObjectName("assemblyTabs");

    // ---------------------------------------------------------------------------------
    // Constraint / link composer
    // ---------------------------------------------------------------------------------
    auto *linkTab = new QWidget;
    auto *linkRoot = new QVBoxLayout(linkTab);
    linkRoot->setContentsMargins(6, 4, 6, 4);
    linkRoot->setSpacing(4);
    auto *commandBar = new QHBoxLayout;
    commandBar->setSpacing(6);
    linkNew = actionButton(QStringLiteral("清空"), "linkNew");
    linkNew->setToolTip(QStringLiteral("清空当前表单，并以场景中选中的部件开始一条新约束"));
    linkAdd = actionButton(QStringLiteral("＋ 新建约束"), "linkAdd");
    linkUpdate = actionButton(QStringLiteral("应用"), "linkUpdate");
    linkRemove = actionButton(QStringLiteral("删除"), "linkRemove");
    linkAdd->setToolTip(QStringLiteral("用下方 A / B 与连接点新建一条约束"));
    linkUpdate->setToolTip(QStringLiteral("把编排器中的参数写回当前选中的约束"));
    linkRemove->setToolTip(QStringLiteral("删除当前选中的约束 (Delete)"));
    auto *vehicleWizard = actionButton(QStringLiteral("车辆装配向导…"), "vehicleAssemblyWizardButton");
    vehicleWizard->setToolTip(QStringLiteral("选择车架和四个车轮，自动建立悬挂、转动副与轮辋-轮胎缝合"));
    connect(vehicleWizard, &QPushButton::clicked, this, &MainWindow::showVehicleAssemblyWizard);
    linkAdd->setStyleSheet(QStringLiteral("QPushButton{background:#0f6cbd;color:white;border:1px solid #0b5a9e;"
                                          "border-radius:3px;padding:3px 10px;} QPushButton:hover{background:#1780da;}"));
    commandBar->addWidget(linkNew);
    commandBar->addWidget(linkAdd);
    commandBar->addWidget(linkUpdate);
    commandBar->addWidget(linkRemove);
    commandBar->addWidget(vehicleWizard);
    commandBar->addStretch();
    linkRoot->addLayout(commandBar);

    // The constraint list sits above the composer: this dock is a Details-style
    // side panel, so the list is a compact band and the composer gets the rest.
    auto *listPanel = new QWidget;
    auto *listLayout = new QVBoxLayout(listPanel);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(3);
    listPanel->setMaximumHeight(120);
    auto *linkFilter = new QLineEdit;
    linkFilter->setObjectName("linkFilter");
    linkFilter->setPlaceholderText(QStringLiteral("筛选约束 / 部件…"));
    linkFilter->setClearButtonEnabled(true);
    linkFilter->setMinimumHeight(22);
    linkTree = new QTreeWidget;
    linkTree->setObjectName("linkTree");
    linkTree->setColumnCount(3);
    linkTree->setHeaderLabels({QStringLiteral("类型"), QStringLiteral("部件 (A → B)"), QStringLiteral("状态 / 实时")});
    linkTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    linkTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    linkTree->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    linkTree->header()->resizeSection(2, 96);
    linkTree->setRootIsDecorated(false);
    linkTree->setAlternatingRowColors(true);
    linkTree->setUniformRowHeights(true);
    linkTree->setSelectionMode(QAbstractItemView::SingleSelection);
    linkTree->setMinimumHeight(60);
    linkTree->setContextMenuPolicy(Qt::CustomContextMenu);
    auto *filterRow = new QHBoxLayout;
    filterRow->setSpacing(4);
    filterRow->addWidget(linkFilter, 1);
    showAssemblyViewport = new QCheckBox(QStringLiteral("视口"));
    showAssemblyViewport->setObjectName("showAssemblyViewport");
    showAssemblyViewport->setToolTip(QStringLiteral("在视口中显示装配连线与选中高亮（Ctrl+Shift+L）"));
    showAssemblyViewport->setChecked(viewport->showLinks);
    connect(showAssemblyViewport, &QCheckBox::toggled, this, [this](bool on) {
        if (showLinksAction)
            showLinksAction->setChecked(on);
    });
    filterRow->addWidget(showAssemblyViewport);
    listLayout->addLayout(filterRow);
    listLayout->addWidget(linkTree, 1);

    auto *composerScroll = new QScrollArea;
    composerScroll->setObjectName("linkComposerScroll");
    composerScroll->setWidgetResizable(true);
    composerScroll->setFrameShape(QFrame::NoFrame);
    composerScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *composer = new QWidget;
    auto *composerLayout = new QVBoxLayout(composer);
    composerLayout->setContentsMargins(4, 0, 4, 4);
    composerLayout->setSpacing(6);
    linkError = new QLabel;
    linkError->setObjectName("linkError");
    linkError->setWordWrap(true);
    linkError->setStyleSheet(QStringLiteral("color:#b42318;background:#fff4f2;border:1px solid #f3c2bc;"
                                            "border-radius:3px;padding:3px 6px;"));
    linkError->hide();
    composerLayout->addWidget(linkError);

    // 1. Type and bodies -------------------------------------------------------------
    linkBodiesGroup = new CollapsibleGroupBox(QStringLiteral("1 · 类型与部件"), composer);
    auto *bodiesGrid = new QGridLayout(linkBodiesGroup->body());
    bodiesGrid->setHorizontalSpacing(6);
    bodiesGrid->setVerticalSpacing(4);
    bodiesGrid->addWidget(fieldLabel(QStringLiteral("类型")), 0, 0);
    linkType = new QComboBox;
    linkType->setObjectName("linkType");
    linkType->addItem(QStringLiteral("移动副"), QStringLiteral("prismatic"));
    linkType->addItem(QStringLiteral("转动副"), QStringLiteral("revolute"));
    linkType->addItem(QStringLiteral("球铰"), QStringLiteral("spherical"));
    linkType->addItem(QStringLiteral("固定副"), QStringLiteral("fixed"));
    linkType->addItem(QStringLiteral("转动约束"), QStringLiteral("rotation"));
    linkType->addItem(QStringLiteral("缝合 / 弹簧"), QStringLiteral("stitch"));
    linkType->setMinimumHeight(24);
    bodiesGrid->addWidget(linkType, 0, 1, 1, 2);
    linkTypeHint = new QLabel;
    linkTypeHint->setWordWrap(true);
    linkTypeHint->setStyleSheet(QStringLiteral("color:#657282;"));
    // The rule text is long; keep it as the type combo tooltip so the pivot
    // row (the precision-critical part of the composer) stays above the fold.
    linkTypeHint->hide();
    bodiesGrid->addWidget(linkTypeHint, 1, 1, 1, 3);
    bodiesGrid->addWidget(fieldLabel(QStringLiteral("部件 A")), 2, 0);
    linkBodyA = new QComboBox;
    linkBodyA->setObjectName("linkBodyA");
    linkBodyA->setMinimumHeight(24);
    linkPickBodyA = actionButton(QStringLiteral("视口拾取"), "linkPickBodyA");
    linkPickBodyA->setToolTip(QStringLiteral("点击视口中的一个部件，作为约束 A 端"));
    bodiesGrid->addWidget(linkBodyA, 2, 1, 1, 2);
    bodiesGrid->addWidget(linkPickBodyA, 2, 3);
    bodiesGrid->addWidget(fieldLabel(QStringLiteral("部件 B")), 3, 0);
    linkBodyB = new QComboBox;
    linkBodyB->setObjectName("linkBodyB");
    linkBodyB->setMinimumHeight(24);
    linkPickBodyB = actionButton(QStringLiteral("视口拾取"), "linkPickBodyB");
    linkPickBodyB->setToolTip(QStringLiteral("点击视口中的一个部件，作为约束 B 端"));
    bodiesGrid->addWidget(linkBodyB, 3, 1, 1, 2);
    bodiesGrid->addWidget(linkPickBodyB, 3, 3);
    linkSwap = new QToolButton;
    linkSwap->setObjectName("linkSwap");
    linkSwap->setText(QStringLiteral("⇅ A/B"));
    linkSwap->setToolButtonStyle(Qt::ToolButtonTextOnly);
    linkSwap->setAutoRaise(true);
    linkSwap->setToolTip(QStringLiteral("交换两侧部件和锚点"));
    bodiesGrid->addWidget(linkSwap, 0, 3);
    bodiesGrid->setColumnStretch(1, 1);
    composerLayout->addWidget(linkBodiesGroup);

    // 2. Shared world pivot ----------------------------------------------------------
    linkPivotGroup = new CollapsibleGroupBox(QStringLiteral("2 · 连接点（世界）"), composer);
    auto *pivotLayout = new QVBoxLayout(linkPivotGroup->body());
    pivotLayout->setContentsMargins(0, 0, 0, 0);
    pivotLayout->setSpacing(4);
    auto *pivotRow = new QHBoxLayout;
    pivotRow->setSpacing(4);
    pivotRow->addWidget(fieldLabel(QStringLiteral("连接点")));
    for (int i = 0; i < 3; ++i) {
        linkPivot[size_t(i)] = vectorField(linkPivotGroup, ("linkPivot" + QString(QChar("XYZ"[i]))).toUtf8());
        pivotRow->addWidget(new QLabel(QChar("XYZ"[i])));
        pivotRow->addWidget(linkPivot[size_t(i)]);
    }
    pivotLayout->addLayout(pivotRow);
    auto *pivotButtons = new QHBoxLayout;
    pivotButtons->setSpacing(4);
    linkPivotMid = actionButton(QStringLiteral("两部件中点"), "linkPivotMid");
    linkPivotFromA = actionButton(QStringLiteral("A 中心"), "linkPivotFromA");
    linkPivotFromB = actionButton(QStringLiteral("B 中心"), "linkPivotFromB");
    linkPivotPick = actionButton(QStringLiteral("视口拾取连接点"), "linkPivotPick");
    pivotButtons->addWidget(linkPivotMid);
    pivotButtons->addWidget(linkPivotFromA);
    pivotButtons->addWidget(linkPivotFromB);
    pivotButtons->addWidget(linkPivotPick);
    pivotButtons->addStretch();
    pivotLayout->addLayout(pivotButtons);
    linkPivotHint = new QLabel;
    linkPivotHint->setWordWrap(true);
    linkPivotHint->setStyleSheet(QStringLiteral("color:#657282;"));
    pivotLayout->addWidget(linkPivotHint);

    linkAdvancedAnchors = new QCheckBox(QStringLiteral("高级 · 局部锚点"), linkPivotGroup);
    linkAdvancedAnchors->setObjectName("linkAdvancedAnchors");
    linkAdvancedAnchors->setToolTip(QStringLiteral("通常使用上方的世界连接点即可；需要逐侧精确锚点坐标时展开。"));
    linkAdvancedAnchors->setChecked(false);
    pivotLayout->addWidget(linkAdvancedAnchors);

    linkAnchorRow = new QWidget;
    auto *anchorGrid = new QGridLayout(linkAnchorRow);
    anchorGrid->setContentsMargins(0, 2, 0, 0);
    anchorGrid->setHorizontalSpacing(4);
    anchorGrid->setVerticalSpacing(3);
    anchorGrid->addWidget(new QLabel(QStringLiteral("坐标")), 0, 0);
    for (int i = 0; i < 3; ++i)
        anchorGrid->addWidget(new QLabel(QChar("XYZ"[i])), 0, 1 + i);
    for (int i = 0; i < 3; ++i) {
        linkAnchorA[size_t(i)] = vectorField(linkAnchorRow, ("linkAnchorA" + QString(QChar("XYZ"[i]))).toUtf8());
        linkAnchorB[size_t(i)] = vectorField(linkAnchorRow, ("linkAnchorB" + QString(QChar("XYZ"[i]))).toUtf8());
        linkAnchorA[size_t(i)]->setToolTip(QStringLiteral("A 端局部锚点 / %1").arg(QChar("XYZ"[i])));
        linkAnchorB[size_t(i)]->setToolTip(QStringLiteral("B 端局部锚点 / %1").arg(QChar("XYZ"[i])));
        anchorGrid->setColumnStretch(1 + i, 1);
    }
    anchorGrid->addWidget(new QLabel(QStringLiteral("A 局部")), 1, 0);
    for (int i = 0; i < 3; ++i)
        anchorGrid->addWidget(linkAnchorA[size_t(i)], 1, 1 + i);
    linkPickA = actionButton(QStringLiteral("拾取"), "linkPickA");
    anchorGrid->addWidget(linkPickA, 1, 4);
    anchorGrid->addWidget(new QLabel(QStringLiteral("B 局部")), 2, 0);
    for (int i = 0; i < 3; ++i)
        anchorGrid->addWidget(linkAnchorB[size_t(i)], 2, 1 + i);
    linkPickB = actionButton(QStringLiteral("拾取"), "linkPickB");
    anchorGrid->addWidget(linkPickB, 2, 4);
    auto *anchorAdvancedHint = new QLabel(QStringLiteral(
        "锚点使用部件局部坐标（米）。通常只编辑上面的世界连接点；改动局部锚点后，视口以锚点为准。"));
    anchorAdvancedHint->setWordWrap(true);
    anchorAdvancedHint->setStyleSheet(QStringLiteral("color:#7b8794;"));
    anchorGrid->addWidget(anchorAdvancedHint, 3, 0, 1, 5);
    pivotLayout->addWidget(linkAnchorRow);
    linkAnchorRow->setVisible(false);
    connect(linkAdvancedAnchors, &QCheckBox::toggled, this, [this](bool on) {
        Link::Type type = Link::Type::Prismatic;
        Link::typeFromKey(linkType->currentData().toString(), type);
        linkAnchorRow->setVisible(on && type != Link::Type::Stitch && !editingLocked());
    });
    composerLayout->addWidget(linkPivotGroup);

    // 3. Axis -----------------------------------------------------------------------
    linkAxisGroup = new CollapsibleGroupBox(QStringLiteral("3 · 轴向"), composer);
    auto *axisLayout = new QVBoxLayout(linkAxisGroup->body());
    axisLayout->setContentsMargins(0, 0, 0, 0);
    axisLayout->setSpacing(4);
    auto *axisFields = new QHBoxLayout;
    axisFields->setSpacing(4);
    axisFields->addWidget(fieldLabel(QStringLiteral("世界轴")));
    for (int i = 0; i < 3; ++i) {
        linkAxis[size_t(i)] = vectorField(linkAxisGroup, ("linkAxis" + QString(QChar("XYZ"[i]))).toUtf8());
        axisFields->addWidget(new QLabel(QChar("XYZ"[i])));
        axisFields->addWidget(linkAxis[size_t(i)]);
    }
    linkAxis[1]->setValue(1);
    axisLayout->addLayout(axisFields);
    auto *axisPresets = new QHBoxLayout;
    axisPresets->setSpacing(3);
    axisPresets->addWidget(new QLabel(QStringLiteral("世界")));
    for (const char *axisName : {"X", "Y", "Z"}) {
        auto *preset = new QToolButton;
        preset->setText(QString::fromLatin1(axisName));
        preset->setAutoRaise(true);
        preset->setObjectName(QString("linkAxisPreset") + QString::fromLatin1(axisName));
        connect(preset, &QToolButton::clicked, this, [this, axisName] {
            const QString axis = QString::fromLatin1(axisName);
            for (int i = 0; i < 3; ++i)
                linkAxis[size_t(i)]->setValue(QString(QChar("XYZ"[i])) == axis ? 1.0 : 0.0);
            updateLinkComposer();
        });
        axisPresets->addWidget(preset);
    }
    axisPresets->addSpacing(8);
    axisPresets->addWidget(new QLabel(QStringLiteral("A 局部")));
    for (const char *axisName : {"X", "Y", "Z"}) {
        auto *preset = new QToolButton;
        preset->setText(QString::fromLatin1(axisName));
        preset->setAutoRaise(true);
        preset->setObjectName(QString("linkLocalAxisPreset") + QString::fromLatin1(axisName));
        connect(preset, &QToolButton::clicked, this, [this, axisName] {
            const auto *a = scene->find(linkBodyA->currentData().toString());
            if (!a)
                return;
            QVector3D local;
            for (int i = 0; i < 3; ++i)
                if (QString(QChar("XYZ"[i])) == QString::fromLatin1(axisName))
                    local[i] = 1.f;
            const auto world = QQuaternion::fromEulerAngles(a->rotation).rotatedVector(local).normalized();
            for (int i = 0; i < 3; ++i)
                linkAxis[size_t(i)]->setValue(world[i]);
            updateLinkComposer();
        });
        axisPresets->addWidget(preset);
    }
    linkAxisFlip = actionButton(QStringLiteral("反向"), "linkAxisFlip");
    axisPresets->addSpacing(8);
    axisPresets->addWidget(linkAxisFlip);
    axisPresets->addStretch();
    axisLayout->addLayout(axisPresets);
    auto *axisHint = new QLabel(QStringLiteral("轴为世界方向；转动副 / 移动副 / 转动约束使用该轴。"
                                               "“A 局部”按部件 A 当前旋转换算。"));
    axisHint->setWordWrap(true);
    axisHint->setStyleSheet(QStringLiteral("color:#7b8794;"));
    axisLayout->addWidget(axisHint);
    linkAxisGroup->setExpanded(false);
    composerLayout->addWidget(linkAxisGroup);

    // 4. Parameters ------------------------------------------------------------------
    linkParamsGroup = new CollapsibleGroupBox(QStringLiteral("4 · 参数与驱动"), composer);
    auto *paramsLayout = new QVBoxLayout(linkParamsGroup->body());
    paramsLayout->setContentsMargins(0, 0, 0, 0);
    paramsLayout->setSpacing(4);
    auto *strengthRow = new QHBoxLayout;
    strengthRow->addWidget(fieldLabel(QStringLiteral("强度比值")));
    linkStrength = vectorField(linkParamsGroup, "linkStrength", 1, 3);
    linkStrength->setRange(.001, 1e9);
    linkStrength->setValue(100);
    linkStrength->setToolTip(QStringLiteral("libuipc 的约束强度比值：K = 比值 × (mA + mB)。"));
    strengthRow->addWidget(linkStrength);
    strengthRow->addStretch();
    paramsLayout->addLayout(strengthRow);

    linkLimitRow = new QWidget;
    auto *limitLayout = new QVBoxLayout(linkLimitRow);
    limitLayout->setContentsMargins(0, 0, 0, 0);
    limitLayout->setSpacing(3);
    limitEnabled = new QCheckBox(QStringLiteral("限位"));
    limitEnabled->setObjectName("limitEnabled");
    limitLower = vectorField(linkLimitRow, "limitLower");
    limitUpper = vectorField(linkLimitRow, "limitUpper");
    limitStrength = vectorField(linkLimitRow, "limitStrength", 10);
    limitStrength->setRange(.001, 1e12);
    limitStrength->setValue(100);
    auto *limitRange = new QHBoxLayout;
    limitRange->setSpacing(4);
    limitRange->addWidget(limitEnabled);
    limitRange->addWidget(new QLabel(QStringLiteral("下限")));
    limitRange->addWidget(limitLower);
    limitRange->addWidget(new QLabel(QStringLiteral("上限")));
    limitRange->addWidget(limitUpper);
    limitRange->addStretch();
    limitLayout->addLayout(limitRange);
    auto *limitStiffnessRow = new QHBoxLayout;
    limitStiffnessRow->setSpacing(4);
    limitStiffnessRow->addWidget(new QLabel(QStringLiteral("限位刚度")));
    limitStiffnessRow->addWidget(limitStrength);
    limitStiffnessRow->addStretch();
    limitLayout->addLayout(limitStiffnessRow);
    paramsLayout->addWidget(linkLimitRow);

    linkRotationRow = new QWidget;
    auto *rotationLayout = new QHBoxLayout(linkRotationRow);
    rotationLayout->setContentsMargins(0, 0, 0, 0);
    rotationAngle = vectorField(linkRotationRow, "rotationAngle");
    rotationSpeed = vectorField(linkRotationRow, "rotationSpeed");
    rotationLayout->addWidget(new QLabel(QStringLiteral("目标角 (rad)")));
    rotationLayout->addWidget(rotationAngle);
    rotationLayout->addWidget(new QLabel(QStringLiteral("角速度 (rad/s)")));
    rotationLayout->addWidget(rotationSpeed);
    rotationLayout->addStretch();
    paramsLayout->addWidget(linkRotationRow);

    driveRow = new QWidget;
    auto *driveLayout = new QVBoxLayout(driveRow);
    driveLayout->setContentsMargins(0, 0, 0, 0);
    driveLayout->setSpacing(3);
    driveEnabled = new QCheckBox(QStringLiteral("启用驱动"));
    driveEnabled->setObjectName("driveEnabled");
    driveSpeed = vectorField(driveRow, "driveSpeed");
    driveSpeed->setRange(-1e4, 1e4);
    driveRamp = vectorField(driveRow, "driveRamp");
    driveRamp->setRange(0, 1000);
    driveRamp->setValue(.5);
    driveStrength = vectorField(driveRow, "driveStrength");
    driveStrength->setRange(.001, 1e12);
    driveStrength->setValue(1e4);
    auto *driveFirstRow = new QHBoxLayout;
    driveFirstRow->setSpacing(4);
    driveFirstRow->addWidget(driveEnabled);
    driveFirstRow->addWidget(new QLabel(QStringLiteral("速度")));
    driveFirstRow->addWidget(driveSpeed);
    driveFirstRow->addWidget(new QLabel(QStringLiteral("斜坡 (s)")));
    driveFirstRow->addWidget(driveRamp);
    driveFirstRow->addStretch();
    driveLayout->addLayout(driveFirstRow);
    auto *driveSecondRow = new QHBoxLayout;
    driveSecondRow->setSpacing(4);
    driveSecondRow->addWidget(new QLabel(QStringLiteral("驱动强度")));
    driveSecondRow->addWidget(driveStrength);
    driveSecondRow->addStretch();
    driveLayout->addLayout(driveSecondRow);
    paramsLayout->addWidget(driveRow);

    damperRow = new QWidget;
    auto *damperLayout = new QHBoxLayout(damperRow);
    damperLayout->setContentsMargins(0, 0, 0, 0);
    damperEnabled = new QCheckBox(QStringLiteral("启用轴向阻尼"));
    damperEnabled->setObjectName("damperEnabled");
    damperCoefficient = vectorField(damperRow, "damperCoefficient");
    damperCoefficient->setRange(0, 1e9);
    damperLayout->addWidget(damperEnabled);
    damperLayout->addWidget(new QLabel(QStringLiteral("系数 (N·s/m)")));
    damperLayout->addWidget(damperCoefficient);
    damperLayout->addStretch();
    paramsLayout->addWidget(damperRow);

    linkStitchRow = new QWidget;
    linkStitchRow->setObjectName("linkStitchRow");
    auto *stitchLayout = new QVBoxLayout(linkStitchRow);
    stitchLayout->setContentsMargins(0, 0, 0, 0);
    stitchLayout->setSpacing(3);
    stitchAnchorMode = new QCheckBox(QStringLiteral("两侧都是锚点"));
    stitchAnchorMode->setObjectName("stitchAnchorMode");
    stitchStiffness = vectorField(linkStitchRow, "stitchStiffness", 1000, 3);
    stitchStiffness->setRange(.001, 1e12);
    stitchStiffness->setValue(20000);
    stitchRest = vectorField(linkStitchRow, "stitchRest");
    stitchRest->setRange(0, 1e5);
    pairsAdd = actionButton(QStringLiteral("添加"), "pairsAdd");
    pairsAdd->setToolTip(QStringLiteral("添加配对：选择下一个未使用的 A 锚点，并匹配最近的 B 目标"));
    pairsRemove = actionButton(QStringLiteral("删除"), "pairsRemove");
    pairsAuto = actionButton(QStringLiteral("自动配对"), "pairsAuto");
    pairsPickA = actionButton(QStringLiteral("拾取 A"), "pairsPickA");
    pairsPickB = actionButton(QStringLiteral("拾取 B"), "pairsPickB");
    pairsPickA->setToolTip(QStringLiteral("在当前配对行：点击部件 A 的表面，自动选取最近的 A 锚点"));
    pairsPickB->setToolTip(QStringLiteral("在当前配对行：点击部件 B 的表面，自动选取最近的顶点或锚点"));
    auto *stitchValues = new QHBoxLayout;
    stitchValues->setSpacing(4);
    stitchValues->addWidget(stitchAnchorMode);
    stitchValues->addWidget(new QLabel(QStringLiteral("刚度")));
    stitchValues->addWidget(stitchStiffness);
    stitchValues->addWidget(new QLabel(QStringLiteral("静止长度")));
    stitchValues->addWidget(stitchRest);
    stitchValues->addStretch();
    stitchLayout->addLayout(stitchValues);
    auto *stitchButtons = new QHBoxLayout;
    stitchButtons->setSpacing(4);
    stitchButtons->addWidget(pairsAdd);
    stitchButtons->addWidget(pairsRemove);
    stitchButtons->addWidget(pairsAuto);
    stitchButtons->addWidget(pairsPickA);
    stitchButtons->addWidget(pairsPickB);
    stitchButtons->addStretch();
    stitchLayout->addLayout(stitchButtons);
    paramsLayout->addWidget(linkStitchRow);
    stitchPairInfo = new QLabel(QStringLiteral(
        "选择配对行，点“拾取 A”再点模型 A 表面，随后点模型 B 表面；或直接用“自动配对”。"));
    stitchPairInfo->setObjectName("stitchPairInfo");
    stitchPairInfo->setWordWrap(true);
    stitchPairInfo->setStyleSheet(QStringLiteral("color:#4c5a68;"));
    paramsLayout->addWidget(stitchPairInfo);
    stitchPairs = new QTableWidget;
    stitchPairs->setObjectName("stitchPairs");
    stitchPairs->setColumnCount(3);
    stitchPairs->setHorizontalHeaderLabels(
        {QStringLiteral("A 锚点（下拉选择）"), QStringLiteral("B 顶点 / 锚点"), QStringLiteral("距离")});
    stitchPairs->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    stitchPairs->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    stitchPairs->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    stitchPairs->verticalHeader()->setVisible(false);
    stitchPairs->setSelectionBehavior(QAbstractItemView::SelectRows);
    stitchPairs->setEditTriggers(QAbstractItemView::NoEditTriggers);
    stitchPairs->setMinimumHeight(90);
    stitchPairs->setMaximumHeight(140);
    paramsLayout->addWidget(stitchPairs);
    composerLayout->addWidget(linkParamsGroup);
    composerLayout->addStretch();
    composerScroll->setWidget(composer);

    linkRoot->addWidget(listPanel);
    linkRoot->addWidget(composerScroll, 1);
    linkDiagnostics = new QLabel(QStringLiteral("选中约束后，状态列显示实时行程 / 角度 / 缝合误差；运行时越界会标红。"));
    linkDiagnostics->setObjectName("linkDiagnostics");
    linkDiagnostics->setWordWrap(true);
    linkDiagnostics->setMinimumHeight(20);
    linkDiagnostics->setStyleSheet(QStringLiteral("color:#4c5a68;background:#f4f7fa;border:1px solid #dce3eb;"
                                                  "border-radius:3px;padding:3px 6px;"));
    linkRoot->addWidget(linkDiagnostics);
    // The live diagnostics also update the tree status column; keeping the
    // descriptive label out of the height budget leaves room for the pivot and
    // parameter sections, and its text is available as the list tooltip.
    linkDiagnostics->hide();
    tabs->addTab(linkTab, QStringLiteral("约束"));

    // ---------------------------------------------------------------------------------
    // Anchors
    // ---------------------------------------------------------------------------------
    auto *anchorPage = new QWidget;
    auto *anchorLayout = new QVBoxLayout(anchorPage);
    anchorLayout->setContentsMargins(6, 4, 6, 4);
    anchorLayout->setSpacing(4);
    auto *anchorButtons = new QHBoxLayout;
    anchorButtons->setSpacing(4);
    anchorAdd = actionButton(QStringLiteral("中心添加"), "anchorAdd");
    anchorPick = actionButton(QStringLiteral("视口点选"), "anchorPick");
    anchorRemove = actionButton(QStringLiteral("删除"), "anchorRemove");
    anchorButtons->addWidget(anchorAdd);
    anchorButtons->addWidget(anchorPick);
    anchorButtons->addWidget(anchorRemove);
    anchorButtons->addStretch();
    anchorLayout->addLayout(anchorButtons);
    auto *anchorSnapRow = new QHBoxLayout;
    anchorSnapRow->setSpacing(4);
    anchorSnapRow->addWidget(new QLabel(QStringLiteral("吸附模式")));
    anchorSnap = new QComboBox;
    anchorSnap->setObjectName("anchorSnap");
    anchorSnap->addItem(QStringLiteral("表面（自由）"), QStringLiteral("surface"));
    anchorSnap->addItem(QStringLiteral("最近顶点"), QStringLiteral("vertex"));
    anchorSnap->addItem(QStringLiteral("面中心"), QStringLiteral("face"));
    anchorSnap->addItem(QStringLiteral("网格顶点"), QStringLiteral("meshvertex"));
    anchorSnap->setToolTip(QStringLiteral("点选锚点时吸附到几何特征，便于建立精确的装配连接点。"));
    anchorSnapRow->addWidget(anchorSnap, 1);
    anchorLayout->addLayout(anchorSnapRow);
    auto *anchorEditRow = new QHBoxLayout;
    anchorEditRow->setSpacing(4);
    for (int i = 0; i < 3; ++i) {
        anchorEdit[size_t(i)] = vectorField(anchorPage, ("anchorEdit" + QString(QChar("XYZ"[i]))).toUtf8());
        anchorEditRow->addWidget(new QLabel(QChar("XYZ"[i])));
        anchorEditRow->addWidget(anchorEdit[size_t(i)]);
    }
    auto *applyAnchor = actionButton(QStringLiteral("更新锚点"), "anchorUpdate");
    anchorEditRow->addWidget(applyAnchor);
    anchorEditRow->addStretch();
    anchorLayout->addLayout(anchorEditRow);
    anchorList = new QListWidget;
    anchorList->setObjectName("anchorList");
    anchorList->setAlternatingRowColors(true);
    anchorLayout->addWidget(anchorList, 1);
    auto *anchorHint = new QLabel(
        QStringLiteral("锚点属于物体，保存为物体局部坐标；关节直接使用锚点 A/B 坐标，缝合链接按序号引用锚点。"
                       "选中物体后可在这里增删，视口里以绿色圆点显示；点选时按上方吸附模式对齐几何特征。"));
    anchorHint->setWordWrap(true);
    anchorHint->setStyleSheet(QStringLiteral("color:#657282;"));
    anchorLayout->addWidget(anchorHint);
    tabs->addTab(anchorPage, QStringLiteral("锚点"));

    // ---------------------------------------------------------------------------------
    // Contact exclusions
    // ---------------------------------------------------------------------------------
    auto *excludePage = new QWidget;
    auto *excludeLayout = new QVBoxLayout(excludePage);
    excludeLayout->setContentsMargins(6, 4, 6, 4);
    excludeLayout->setSpacing(4);
    auto *excludeRow = new QHBoxLayout;
    excludeRow->setSpacing(4);
    exclusionA = new QComboBox;
    exclusionA->setObjectName("exclusionBodyA");
    exclusionA->setMinimumHeight(24);
    exclusionB = new QComboBox;
    exclusionB->setObjectName("exclusionBodyB");
    exclusionB->setMinimumHeight(24);
    exclusionAdd = actionButton(QStringLiteral("关闭接触"), "exclusionAdd");
    exclusionRemove = actionButton(QStringLiteral("恢复接触"), "exclusionRemove");
    excludeRow->addWidget(exclusionA, 1);
    excludeRow->addWidget(new QLabel(QStringLiteral("↔")));
    excludeRow->addWidget(exclusionB, 1);
    excludeLayout->addLayout(excludeRow);
    auto *excludeButtons = new QHBoxLayout;
    excludeButtons->setSpacing(4);
    excludeButtons->addWidget(exclusionAdd);
    excludeButtons->addWidget(exclusionRemove);
    excludeButtons->addStretch();
    excludeLayout->addLayout(excludeButtons);
    exclusionList = new QListWidget;
    exclusionList->setObjectName("exclusionList");
    exclusionList->setAlternatingRowColors(true);
    excludeLayout->addWidget(exclusionList, 1);
    auto *excludeHint =
        new QLabel(QStringLiteral("装配体内故意重叠的部件（导柱与滑块、轮毂与轮辋）需要关闭它们之间的接触，"
                                  "否则初始相交会让求解器直接拒绝启动。"));
    excludeHint->setWordWrap(true);
    excludeHint->setStyleSheet(QStringLiteral("color:#657282;"));
    excludeLayout->addWidget(excludeHint);
    tabs->addTab(excludePage, QStringLiteral("接触排除"));

    dock->setWidget(tabs);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    assemblyPanel = dock;
    if (auto *inspectorDock = findChild<QDockWidget *>("inspectorDock")) {
        tabifyDockWidget(inspectorDock, dock);
        inspectorDock->raise();
    }
    resizeDocks({dock}, {440}, Qt::Horizontal);
    if (auto *replayDock = findChild<QDockWidget *>("replayDock")) {
        if (auto *workspaceDock = findChild<QDockWidget *>("workspaceDock"))
            resizeDocks({replayDock, workspaceDock}, {150, 110}, Qt::Vertical);
    }
    // Keep the existing View menu order: file, edit, create, view.
    menuBar()->actions()[3]->menu()->addAction(dock->toggleViewAction());

    // ---------------------------------------------------------------------------------
    // Behaviour
    // ---------------------------------------------------------------------------------
    connect(linkAdd, &QPushButton::clicked, this, [this] {
        Link link;
        QString error;
        if (!readLinkForm(link, &error)) {
            setLinkError(error);
            log(QStringLiteral("无法创建约束：") + error);
            return;
        }
        link.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto links = scene->links;
        links.push_back(link);
        if (!commitLinks(links, scene->exclusions, QStringLiteral("新建约束")))
            return;
        setLinkError({});
        for (int i = 0; i < linkTree->topLevelItemCount(); ++i)
            if (linkTree->topLevelItem(i)->data(1, Qt::UserRole + 1).toString() == link.id)
                linkTree->setCurrentItem(linkTree->topLevelItem(i));
        log(QStringLiteral("已创建%1：%2").arg(linkTypeName(link.type), link.id.left(8)));
    });
    connect(linkUpdate, &QPushButton::clicked, this, [this] {
        const int index = selectedLinkIndex();
        if (index < 0) {
            setLinkError(QStringLiteral("请先在列表中选择要修改的约束。"));
            return;
        }
        Link link;
        QString error;
        if (!readLinkForm(link, &error)) {
            setLinkError(error);
            log(QStringLiteral("无法更新约束：") + error);
            return;
        }
        auto links = scene->links;
        link.id = links[index].id;
        links[index] = link;
        if (commitLinks(links, scene->exclusions, QStringLiteral("更新约束")))
            setLinkError({});
    });
    connect(linkRemove, &QPushButton::clicked, this, &MainWindow::deleteSelectedLink);
    connect(linkNew, &QPushButton::clicked, this, [this] {
        if (editingLocked())
            return;
        linkComposerTouched = false;
        linkTree->clearSelection();
        linkTree->setCurrentItem(nullptr);
        const auto *current = scene->current();
        {
            const QSignalBlocker blockA(linkBodyA), blockB(linkBodyB);
            if (current)
                linkBodyA->setCurrentIndex(linkBodyA->findData(current->id));
            if (linkBodyA->currentIndex() < 0 && linkBodyA->count())
                linkBodyA->setCurrentIndex(0);
            linkBodyB->setCurrentIndex(-1);
            for (int i = 0; i < linkBodyB->count(); ++i)
                if (linkBodyB->itemData(i).toString() != linkBodyA->currentData().toString()) {
                    linkBodyB->setCurrentIndex(i);
                    break;
                }
        }
        {
            const QSignalBlocker blockAx(linkAnchorA[0]), blockAy(linkAnchorA[1]), blockAz(linkAnchorA[2]);
            const QSignalBlocker blockBx(linkAnchorB[0]), blockBy(linkAnchorB[1]), blockBz(linkAnchorB[2]);
            for (int i = 0; i < 3; ++i) {
                linkAnchorA[size_t(i)]->setValue(0);
                linkAnchorB[size_t(i)]->setValue(0);
            }
        }
        const auto *a = scene->find(linkBodyA->currentData().toString());
        const auto *b = scene->find(linkBodyB->currentData().toString());
        if (a && b) {
            const auto pivot = (objectCenterWorld(*a) + objectCenterWorld(*b)) * .5f;
            {
                const QSignalBlocker blockX(linkPivot[0]), blockY(linkPivot[1]), blockZ(linkPivot[2]);
                for (int i = 0; i < 3; ++i)
                    linkPivot[size_t(i)]->setValue(pivot[i]);
            }
            applyPivotToAnchors();
        }
        setLinkError({});
        viewport->clearLinkHighlight();
        // Keep the list from auto-selecting a link on the next scene refresh;
        // this is a fresh composer session, not an edit of the previous link.
        linkComposerTouched = true;
        updateLinkComposer();
        statusBar()->showMessage(QStringLiteral("已清空编排：选择部件后设置连接点并创建约束"), 2500);
    });
    connect(linkTree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item, QTreeWidgetItem *) {
        if (assemblySyncing || syncing || !item)
            return;
        const int index = selectedLinkIndex();
        if (index >= 0) {
            syncLinkForm(scene->links[index]);
            viewport->setLinkHighlight(scene->links[index].id);
        }
    });
    connect(linkTree, &QTreeWidget::customContextMenuRequested, this, [this](QPoint point) {
        auto *item = linkTree->itemAt(point);
        if (!item)
            return;
        linkTree->setCurrentItem(item);
        QMenu menu;
        auto *focus = menu.addAction(QStringLiteral("聚焦连接点"));
        connect(focus, &QAction::triggered, this, &MainWindow::focusSelectedLink);
        auto *duplicate = menu.addAction(QStringLiteral("复制约束"));
        connect(duplicate, &QAction::triggered, this, [this] {
            const int index = selectedLinkIndex();
            if (index < 0 || editingLocked())
                return;
            auto link = scene->links[index];
            link.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            auto links = scene->links;
            links.push_back(link);
            if (commitLinks(links, scene->exclusions, QStringLiteral("复制约束")))
                log(QStringLiteral("已复制约束：") + link.id.left(8));
        });
        menu.addSeparator();
        menu.addAction(QStringLiteral("删除约束"), this, &MainWindow::deleteSelectedLink);
        menu.exec(linkTree->viewport()->mapToGlobal(point));
    });
    connect(linkFilter, &QLineEdit::textChanged, this, [this, linkFilter] {
        const QString needle = linkFilter->text().trimmed();
        for (int i = 0; i < linkTree->topLevelItemCount(); ++i) {
            auto *item = linkTree->topLevelItem(i);
            item->setHidden(!needle.isEmpty() && !item->text(0).contains(needle, Qt::CaseInsensitive) &&
                            !item->text(1).contains(needle, Qt::CaseInsensitive));
        }
    });
    connect(linkType, &QComboBox::currentIndexChanged, this, [this] {
        if (assemblySyncing)
            return;
        updateLinkComposer();
        if (linkType->currentData().toString() == QStringLiteral("stitch")) {
            if (auto *scroll = findChild<QScrollArea *>(QStringLiteral("linkComposerScroll")))
                scroll->ensureWidgetVisible(linkStitchRow, 0, 8);
        }
    });
    connect(linkBodyA, &QComboBox::currentIndexChanged, this, [this] {
        if (assemblySyncing)
            return;
        linkComposerTouched = true;
        // A changing selection is the start of a composer session. Give the
        // user a useful default while they are not editing an existing link.
        if (selectedLinkIndex() < 0 && linkBodyA->currentData().toString() == linkBodyB->currentData().toString()) {
            for (int i = 0; i < linkBodyB->count(); ++i)
                if (linkBodyB->itemData(i).toString() != linkBodyA->currentData().toString()) {
                    const QSignalBlocker blocker(linkBodyB);
                    linkBodyB->setCurrentIndex(i);
                    break;
                }
        }
        updateLinkComposer();
    });
    connect(linkBodyB, &QComboBox::currentIndexChanged, this, [this] {
        if (!assemblySyncing) {
            linkComposerTouched = true;
            updateLinkComposer();
        }
    });
    connect(linkSwap, &QToolButton::clicked, this, [this] {
        if (assemblySyncing)
            return;
        linkComposerTouched = true;
        const QString a = linkBodyA->currentData().toString();
        const QString b = linkBodyB->currentData().toString();
        const int ai = linkBodyA->findData(b), bi = linkBodyB->findData(a);
        if (ai < 0 || bi < 0)
            return;
        {
            const QSignalBlocker blockA(linkBodyA), blockB(linkBodyB);
            linkBodyA->setCurrentIndex(ai);
            linkBodyB->setCurrentIndex(bi);
        }
        for (int i = 0; i < 3; ++i) {
            const double aValue = linkAnchorA[size_t(i)]->value();
            const double bValue = linkAnchorB[size_t(i)]->value();
            const QSignalBlocker blockA(linkAnchorA[size_t(i)]), blockB(linkAnchorB[size_t(i)]);
            linkAnchorA[size_t(i)]->setValue(bValue);
            linkAnchorB[size_t(i)]->setValue(aValue);
        }
        syncPivotFromAnchors();
        updateLinkComposer();
    });
    for (auto *field : linkPivot)
        connect(field, &QDoubleSpinBox::editingFinished, this, [this] {
            if (!assemblySyncing) {
                linkComposerTouched = true;
                applyPivotToAnchors();
            }
        });
    for (auto *field : linkAnchorA)
        connect(field, &QDoubleSpinBox::editingFinished, this, [this] {
            if (!assemblySyncing) {
                linkComposerTouched = true;
                updateLinkComposer();
            }
        });
    for (auto *field : linkAnchorB)
        connect(field, &QDoubleSpinBox::editingFinished, this, [this] {
            if (!assemblySyncing) {
                linkComposerTouched = true;
                updateLinkComposer();
            }
        });
    connect(linkPivotMid, &QPushButton::clicked, this, [this] {
        linkComposerTouched = true;
        const auto *a = scene->find(linkBodyA->currentData().toString());
        const auto *b = scene->find(linkBodyB->currentData().toString());
        if (!a || !b)
            return;
        const auto pivot = (objectCenterWorld(*a) + objectCenterWorld(*b)) * .5f;
        {
            const QSignalBlocker blockerA(linkPivot[0]), blockerB(linkPivot[1]), blockerC(linkPivot[2]);
            for (int i = 0; i < 3; ++i)
                linkPivot[size_t(i)]->setValue(pivot[i]);
        }
        applyPivotToAnchors();
    });
    connect(linkPivotFromA, &QPushButton::clicked, this, [this] {
        linkComposerTouched = true;
        if (const auto *a = scene->find(linkBodyA->currentData().toString())) {
            const auto pivot = objectCenterWorld(*a);
            {
                const QSignalBlocker blockerA(linkPivot[0]), blockerB(linkPivot[1]), blockerC(linkPivot[2]);
                for (int i = 0; i < 3; ++i)
                    linkPivot[size_t(i)]->setValue(pivot[i]);
            }
            applyPivotToAnchors();
        }
    });
    connect(linkPivotFromB, &QPushButton::clicked, this, [this] {
        linkComposerTouched = true;
        if (const auto *b = scene->find(linkBodyB->currentData().toString())) {
            const auto pivot = objectCenterWorld(*b);
            {
                const QSignalBlocker blockerA(linkPivot[0]), blockerB(linkPivot[1]), blockerC(linkPivot[2]);
                for (int i = 0; i < 3; ++i)
                    linkPivot[size_t(i)]->setValue(pivot[i]);
            }
            applyPivotToAnchors();
        }
    });
    connect(linkPivotPick, &QPushButton::clicked, this, [this] {
        pickTarget = 6;
        viewport->anchorPicking = true;
        statusBar()->showMessage(QStringLiteral("在视口点击连接点位置（Esc 取消）"));
    });
    connect(linkPickBodyA, &QPushButton::clicked, this, [this] { pickBody(4); });
    connect(linkPickBodyB, &QPushButton::clicked, this, [this] { pickBody(5); });
    connect(linkPickA, &QPushButton::clicked, this, [this] {
        pickTarget = 1;
        viewport->anchorPicking = true;
        statusBar()->showMessage(QStringLiteral("在视口点击一个物体，用作约束 A 端锚点（Esc 取消）"));
    });
    connect(linkPickB, &QPushButton::clicked, this, [this] {
        pickTarget = 2;
        viewport->anchorPicking = true;
        statusBar()->showMessage(QStringLiteral("在视口点击一个物体，用作约束 B 端锚点（Esc 取消）"));
    });
    connect(linkAxisFlip, &QPushButton::clicked, this, [this] {
        for (auto *field : linkAxis)
            field->setValue(-field->value());
        updateLinkComposer();
    });
    connect(pairsAdd, &QPushButton::clicked, this, [this] {
        const auto *a = scene->find(linkBodyA->currentData().toString());
        const auto *b = scene->find(linkBodyB->currentData().toString());
        if (!a || !b) {
            setLinkError(QStringLiteral("添加配对前请先选择部件 A 和 B。"));
            return;
        }
        if (a->anchors.isEmpty()) {
            setLinkError(QStringLiteral("部件 A 没有锚点；请先在“锚点”页添加锚点，再用视口拾取。"));
            return;
        }
        QSet<int> used;
        for (int i = 0; i < stitchPairs->rowCount(); ++i)
            used.insert(stitchPairAnchor(i));
        int ai = 0;
        while (ai < a->anchors.size() && used.contains(ai))
            ++ai;
        if (ai >= a->anchors.size())
            ai = 0;
        const QVector3D wa = a->anchorMatrix().map(a->anchors[ai]);
        int bi = 0;
        float best = 1e30f;
        if (stitchAnchorMode->isChecked()) {
            for (int i = 0; i < b->anchors.size(); ++i) {
                const float d = (b->anchorMatrix().map(b->anchors[i]) - wa).lengthSquared();
                if (d < best) {
                    best = d;
                    bi = i;
                }
            }
        } else if (b->mesh) {
            for (int i = 0; i < b->mesh->positions.size(); ++i) {
                const float d = (b->matrix().map(b->mesh->positions[i]) - wa).lengthSquared();
                if (d < best) {
                    best = d;
                    bi = i;
                }
            }
        }
        const int row = stitchPairs->rowCount();
        setStitchRow(row, ai, bi);
        stitchPairs->setCurrentCell(row, 0);
        updateStitchPairInfo();
        setLinkError({});
    });
    connect(pairsRemove, &QPushButton::clicked, this, [this] {
        if (stitchPairs->currentRow() >= 0)
            stitchPairs->removeRow(stitchPairs->currentRow());
        updateStitchPairInfo();
        updateLinkComposer();
    });
    connect(pairsPickA, &QPushButton::clicked, this, [this] {
        if (stitchPairs->currentRow() < 0) {
            pairsAdd->click();
        }
        pickTarget = 7;
        viewport->anchorPicking = true;
        statusBar()->showMessage(QStringLiteral("点击部件 A 的表面，自动选取最近的 A 锚点（Esc 取消）"));
    });
    connect(pairsPickB, &QPushButton::clicked, this, [this] {
        if (stitchPairs->currentRow() < 0) {
            pairsAdd->click();
        }
        pickTarget = 8;
        viewport->anchorPicking = true;
        statusBar()->showMessage(QStringLiteral("点击部件 B 的表面，自动选取最近的顶点/锚点（Esc 取消）"));
    });
    connect(stitchPairs, &QTableWidget::currentCellChanged, this,
            [this] { updateStitchPairInfo(); });
    connect(stitchAnchorMode, &QCheckBox::toggled, this, [this] {
        if (!assemblySyncing) {
            refreshStitchRows();
            updateLinkComposer();
        }
    });
    connect(pairsAuto, &QPushButton::clicked, this, [this] {
        // Match each A anchor to the closest unused B point in world space.
        const Object *a = scene->find(linkBodyA->currentData().toString());
        const Object *b = scene->find(linkBodyB->currentData().toString());
        if (!a || !b) {
            setLinkError(QStringLiteral("自动配对需要先选择两个物体。"));
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
            setLinkError(QStringLiteral("请为 A 刚体添加锚点，并为 B 选择锚点或网格顶点。"));
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
            setStitchRow(count, i, best);
            ++count;
        }
        if (stitchPairs->rowCount())
            stitchPairs->setCurrentCell(0, 0);
        updateStitchPairInfo();
        log(QStringLiteral("已自动配对 %1 对；可在表中逐行调整。").arg(count));
    });
    connect(driveEnabled, &QCheckBox::toggled, this, [this] {
        if (!assemblySyncing)
            updateLinkComposer();
    });
    connect(damperEnabled, &QCheckBox::toggled, this, [this] {
        if (!assemblySyncing)
            updateLinkComposer();
    });
    connect(limitEnabled, &QCheckBox::toggled, this, [this] {
        if (!assemblySyncing)
            updateLinkComposer();
    });
    connect(anchorAdd, &QPushButton::clicked, this, [this] {
        auto *o = scene->current();
        if (!o) {
            log(QStringLiteral("请先选中一个物体。"));
            return;
        }
        const auto bounds = localBounds(*o);
        auto next = *o;
        next.anchors.push_back((bounds.first + bounds.second) * .5f);
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
    connect(anchorList, &QListWidget::currentRowChanged, this, [this](int i) {
        auto *o = displayScene()->current();
        if (!o || i < 0 || i >= o->anchors.size())
            return;
        for (int k = 0; k < 3; ++k)
            anchorEdit[size_t(k)]->setValue(o->anchors[i][k]);
        viewport->setAnchorHighlight(o->id, i);
    });
    connect(applyAnchor, &QPushButton::clicked, this, [this] {
        const int i = anchorList->currentRow();
        const auto *o = scene->current();
        if (editingLocked() || !o || i < 0 || i >= o->anchors.size())
            return;
        auto next = *o;
        for (int k = 0; k < 3; ++k)
            next.anchors[i][k] = float(anchorEdit[size_t(k)]->value());
        scene->edit(next, QStringLiteral("更新锚点"));
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
            statusBar()->showMessage(QStringLiteral("已取消视口拾取"), 2000);
        }
    });
    syncAssemblyPanel();
}

} // namespace bagua
