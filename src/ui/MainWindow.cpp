#include "MainWindow.h"
#include "Backends.h"
#include "ProjectFile.h"
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QColor>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QIcon>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QRectF>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
namespace bagua {
namespace {
QIcon transformIcon(int tool) {
    QPixmap pixmap(32, 32);
    pixmap.setDevicePixelRatio(2);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(48, 91, 131), 1.3, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    if (tool == 0) {
        p.drawLine(2, 8, 14, 8);
        p.drawLine(8, 2, 8, 14);
        p.drawPolyline(QPolygonF{{5, 5}, {8, 2}, {11, 5}});
        p.drawPolyline(QPolygonF{{5, 11}, {8, 14}, {11, 11}});
        p.drawPolyline(QPolygonF{{5, 5}, {2, 8}, {5, 11}});
        p.drawPolyline(QPolygonF{{11, 5}, {14, 8}, {11, 11}});
    } else if (tool == 1) {
        p.drawArc(QRectF(2, 2, 12, 12), 35 * 16, 285 * 16);
        p.drawPolyline(QPolygonF{{9, 2}, {14, 2}, {14, 7}});
    } else {
        p.drawRect(QRectF(2, 8, 6, 6));
        p.drawLine(6, 10, 14, 2);
        p.drawPolyline(QPolygonF{{8, 2}, {14, 2}, {14, 8}});
    }
    return QIcon(pixmap);
}
} // namespace
MainWindow::MainWindow(QWidget *p, bool initialScene) : QMainWindow(p) {
    setObjectName("BaguaMainWindow");
    setAcceptDrops(true);
    resize(1280, 800);
    setMinimumSize(960, 640);
    setDockNestingEnabled(true);
    undoGroup = new QUndoGroup(this);
    viewport = new Viewport(nullptr, this);
    setupSceneTabs();
    loadPhysicsSettings();
    setupMenus();
    setupDocks();
    setupReplayDock();
    setupAssemblyDock();
    statusBar()->showMessage(QStringLiteral("就绪 · 场景编辑器"));
    countLabel = new QLabel;
    statusBar()->addPermanentWidget(countLabel);
    connect(viewport, &Viewport::message, this, &MainWindow::log);
    connect(viewport, &Viewport::toolChanged, this, [this] { toolActions[int(viewport->tool)]->setChecked(true); });
    connect(viewport, &Viewport::dragDelta, this, [this](const QString &text) {
        if (!text.isEmpty())
            statusBar()->showMessage(text);
    });
    connect(viewport, &Viewport::hoverChanged, this, [this](const QString &id) {
        if (id.isEmpty())
            return;
        if (const auto *object = displayScene()->find(id))
            statusBar()->showMessage(QStringLiteral("%1 · %2").arg(object->name, shapeName(object->shape)), 1200);
    });
    replayTimer.setInterval(16);
    replayTimer.setTimerType(Qt::PreciseTimer);
    connect(&replayTimer, &QTimer::timeout, this, &MainWindow::advanceReplay);
    if (initialScene) {
        newScene(true);
    } else {
        setWindowTitle(QStringLiteral("BaguaDT · 项目管理"));
        statusBar()->showMessage(QStringLiteral("请选择或新建项目"));
    }
    log(QStringLiteral("BaguaDT 0.3 · 编辑器已启动。单位：米；坐标系：右手系，Y 轴向上。"));
    log(QStringLiteral("左键选择；拖动轴 / 平面 / 中心柄变换。W/E/R 切换工具，L 世界/局部，G 吸附，Shift "
                       "精细，Ctrl 临时吸附。"));
    log(QStringLiteral("装配：选中部件后打开右侧“装配 · 约束”，用 A/B 拾取与共同连接点快速建立约束。"));
    if (initialScene)
        QTimer::singleShot(250, this, [this] { validate(); });
}
void MainWindow::setupMenus() {
    auto *file = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    auto *edit = menuBar()->addMenu(QStringLiteral("编辑(&E)"));
    auto *create = menuBar()->addMenu(QStringLiteral("创建(&C)"));
    auto *view = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    auto *tools = menuBar()->addMenu(QStringLiteral("工具(&T)"));
    auto *vehicleImport=tools->addAction(QStringLiteral("导入 HSC 整车装配…"));vehicleImport->setObjectName("importHscVehicle");connect(vehicleImport,&QAction::triggered,this,&MainWindow::importHscVehicle);
    auto *vehicleExample=tools->addAction(QStringLiteral("打开 HSC 整车仿真示例"));
    vehicleExample->setObjectName("openHscVehicle");
    connect(vehicleExample,&QAction::triggered,this,[this]{
        auto path=QCoreApplication::applicationDirPath()+"/examples/hsc-vehicle/HSC-Vehicle.bgp";
        if(!QFileInfo::exists(path))path=QDir::currentPath()+"/examples/hsc-vehicle/HSC-Vehicle.bgp";
        if(openPath(path)){auto options=physicsOptions();options.enabled=true;setPhysicsOptions(options);}
    });
    auto *settings = tools->addAction(QStringLiteral("物理模拟设置…"));
    settings->setObjectName("physicsSettings");
    connect(settings, &QAction::triggered, this, &MainWindow::showPhysicsSettings);
    auto *coupled = tools->addAction(QStringLiteral("打开 HSC 刚柔耦合示例"));
    connect(coupled, &QAction::triggered, this, [this] {
        auto p = QCoreApplication::applicationDirPath() + "/examples/hsc-coupled.bagua.json";
        if (!QFileInfo::exists(p))
            p = QDir::currentPath() + "/examples/hsc-coupled.bagua.json";
        openPath(p);
    });
    auto *nativeContact = tools->addAction(QStringLiteral("打开 HSC 原生接触示例"));
    connect(nativeContact, &QAction::triggered, this, [this] {
        auto p = QCoreApplication::applicationDirPath() + "/examples/hsc-native-contact.bagua.json";
        if (!QFileInfo::exists(p))
            p = QDir::currentPath() + "/examples/hsc-native-contact.bagua.json";
        openPath(p);
    });
    auto *linkCart = tools->addAction(QStringLiteral("打开装配示例（双轮悬架）"));
    connect(linkCart, &QAction::triggered, this, [this] {
        auto p = QCoreApplication::applicationDirPath() + "/examples/link-cart.bagua.json";
        if (!QFileInfo::exists(p))
            p = QDir::currentPath() + "/examples/link-cart.bagua.json";
        openPath(p);
    });
    auto *vehicleWizard = tools->addAction(QStringLiteral("车辆四轮装配向导…"));
    vehicleWizard->setObjectName("vehicleAssemblyWizard");
    connect(vehicleWizard, &QAction::triggered, this, &MainWindow::showVehicleAssemblyWizard);
    auto *toolbar = addToolBar(QStringLiteral("主工具栏"));
    toolbar->setObjectName("mainToolbar");
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->setIconSize({16, 16});
    toolbar->setContentsMargins(2, 1, 2, 1);
    toolbar->layout()->setSpacing(2);
    auto action = [&](QMenu *menu, QString text, QKeySequence key, std::function<void()> fn) {
        auto *a = menu->addAction(text);
        a->setShortcut(key);
        const bool editsScene = menu == edit || menu == create;
        connect(a, &QAction::triggered, this, [this, fn, editsScene] {
            if (editsScene && editingLocked())
                return;
            finishInspectorEdit();
            viewport->cancelDrag();
            fn();
        });
        return a;
    };
    auto *newAction = action(file, QStringLiteral("新建"), QKeySequence::New, [this] { newScene(); });
    auto *openAction = action(file, QStringLiteral("打开…"), QKeySequence::Open, [this] {
        auto f = QFileDialog::getOpenFileName(this, QStringLiteral("打开场景 / 项目 / 回放"), {},
                                              "BaguaDT (*.bgp *.bagua.json *.bagua.project.json *.brec);;JSON (*.json)");
        if (!f.isEmpty())
            openPath(f);
    });
    auto *saveAction = action(file, QStringLiteral("保存"), QKeySequence::Save, [this] { save(); });
    newAction->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
    openAction->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    saveAction->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    action(file, QStringLiteral("另存为…"), QKeySequence::SaveAs, [this] { save(true); });
    action(file, QStringLiteral("关闭当前场景"), QKeySequence("Ctrl+W"),
           [this] { closeScene(sceneTabs->currentIndex()); });
    file->addSeparator();
    action(file, QStringLiteral("导入网格资产…"), QKeySequence("Ctrl+I"),
           [this] { importMeshDialog(); });
    action(file, QStringLiteral("导入组件…"), {}, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("导入装配组件"), {},
            QStringLiteral("BaguaDT 组件 (*.bagua.json);;所有文件 (*)"));
        if (!path.isEmpty())
            importComponent(path);
    });
    action(file, QStringLiteral("拆分全部对象为组件…"), {}, [this] { exportComponents(false); });
    action(file, QStringLiteral("导出当前对象为组件…"), {}, [this] { exportComponents(true); });
    action(file, QStringLiteral("打开回放…"), {}, [this] {
        auto p = QFileDialog::getOpenFileName(this, QStringLiteral("打开记录"), {}, "BaguaDT recording (*.brec)");
        if (!p.isEmpty())
            openRecording(p);
    });
    exportRecordingAction = action(file, QStringLiteral("保存录制记录…"), QKeySequence("Ctrl+Shift+R"),
                                   [this] { exportRecordingDialog(); });
    file->addSeparator();
    action(file, QStringLiteral("退出"), QKeySequence::Quit, [this] { close(); });
    for (auto *a : {newAction, openAction, saveAction})
        toolbar->addAction(a);
    auto *projectButton = toolbar->addAction(style()->standardIcon(QStyle::SP_DirIcon), QStringLiteral("项目"));
    projectButton->setObjectName("projectManagerButton");
    projectButton->setToolTip(QStringLiteral("项目管理：最近项目、场景清单、新建与打开 .bgp"));
    connect(projectButton, &QAction::triggered, this, &MainWindow::showProjectManager);
    toolbar->addSeparator();
    auto *undo = action(edit, QStringLiteral("撤销"), QKeySequence::Undo, [this] { undoGroup->undo(); });
    auto *redo = action(edit, QStringLiteral("重做"), {}, [this] { undoGroup->redo(); });
    redo->setShortcuts({QKeySequence::Redo, QKeySequence("Ctrl+Shift+Z")});
    undo->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    redo->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    undo->setIconText(QStringLiteral("撤销"));
    redo->setIconText(QStringLiteral("重做"));
    toolbar->addAction(undo);
    toolbar->addAction(redo);
    toolbar->addSeparator();
    undo->setEnabled(false);
    redo->setEnabled(false);
    connect(undoGroup, &QUndoGroup::canUndoChanged, undo, &QAction::setEnabled);
    connect(undoGroup, &QUndoGroup::canRedoChanged, redo, &QAction::setEnabled);
    connect(undoGroup, &QUndoGroup::undoTextChanged, undo,
            [undo](const QString &text) { undo->setText(QStringLiteral("撤销 ") + text); });
    connect(undoGroup, &QUndoGroup::redoTextChanged, redo,
            [redo](const QString &text) { redo->setText(QStringLiteral("重做 ") + text); });
    edit->addSeparator();
    duplicateAction =
        action(edit, QStringLiteral("复制对象"), QKeySequence("Ctrl+D"), [this] { scene->duplicateSelected(); });
    deleteAction = action(edit, QStringLiteral("删除对象"), QKeySequence::Delete, [this] { scene->removeSelected(); });
    action(edit, QStringLiteral("重命名"), QKeySequence("F2"), [this] {
        if (auto *i = tree->currentItem(); i && scene->current())
            tree->editItem(i, 0);
    });
    auto *group = new QActionGroup(this);
    group->setExclusive(true);
    const QStringList labels = {QStringLiteral("移动"), QStringLiteral("旋转"), QStringLiteral("缩放")};
    for (int i = 0; i < 3; ++i) {
        auto *a = toolbar->addAction(labels[i]);
        a->setIcon(transformIcon(i));
        a->setToolTip(labels[i] + QString(" (%1)").arg(QString("WER").mid(i, 1)));
        a->setCheckable(true);
        group->addAction(a);
        toolActions[i] = a;
        connect(a, &QAction::triggered, this, [this, i] { viewport->setTool(Tool(i)); });
    }
    toolActions[0]->setChecked(true);
    toolbar->addSeparator();
    // Unity/Unreal style transform options: world/local orientation and grid
    // snapping live next to the tool switch so the current mode is always visible.
    spaceAction = toolbar->addAction(QStringLiteral("世界"));
    spaceAction->setObjectName("transformSpaceAction");
    spaceAction->setCheckable(true);
    spaceAction->setToolTip(QStringLiteral("变换坐标系：世界 / 局部 (L)"));
    snapAction = toolbar->addAction(QStringLiteral("吸附"));
    snapAction->setObjectName("snapToggle");
    snapAction->setCheckable(true);
    snapAction->setToolTip(QStringLiteral("网格吸附 (G)；拖动时按住 Ctrl 可临时吸附，Shift 使用精细步长"));
    snapStep = new QComboBox;
    snapStep->setObjectName("snapStep");
    snapStep->setToolTip(QStringLiteral("吸附步长（米 / 度）"));
    snapStep->addItem(QStringLiteral("0.01"), .01);
    snapStep->addItem(QStringLiteral("0.05"), .05);
    snapStep->addItem(QStringLiteral("0.10"), .10);
    snapStep->addItem(QStringLiteral("0.25"), .25);
    snapStep->addItem(QStringLiteral("0.50"), .50);
    snapStep->addItem(QStringLiteral("1.00"), 1.00);
    snapStep->setCurrentIndex(2);
    snapStep->setFixedWidth(62);
    toolbar->addWidget(new QLabel(QStringLiteral(" 步长 ")));
    toolbar->addWidget(snapStep);
    connect(spaceAction, &QAction::toggled, this, [this](bool local) {
        viewport->setTransformSpace(local ? TransformSpace::Local : TransformSpace::World);
    });
    connect(snapAction, &QAction::toggled, this, [this](bool on) { viewport->setSnapEnabled(on); });
    connect(snapStep, &QComboBox::currentIndexChanged, this, [this] {
        const float step = float(snapStep->currentData().toDouble());
        viewport->setSnapSteps(step, step >= .25 ? 15.f : 5.f, std::max(.01f, step * .5f));
    });
    connect(viewport, &Viewport::transformSpaceChanged, this, [this] {
        const bool local = viewport->transformSpace == TransformSpace::Local;
        QSignalBlocker blocker(spaceAction);
        spaceAction->setChecked(local);
        spaceAction->setText(local ? QStringLiteral("局部") : QStringLiteral("世界"));
    });
    connect(viewport, &Viewport::snapChanged, this, [this] {
        QSignalBlocker blocker(snapAction);
        snapAction->setChecked(viewport->snapEnabled);
        snapAction->setText(viewport->snapEnabled ? QStringLiteral("吸附 开") : QStringLiteral("吸附"));
    });
    viewport->setSnapSteps(.1f, 5.f, .05f);
    toolbar->addSeparator();
    auto *createMenu = new QMenu(this);
    for (int i = 0; i < 4; ++i) {
        auto *a = create->addAction(shapeName(Shape(i)));
        a->setShortcut(QKeySequence(QString("Ctrl+%1").arg(i + 1)));
        connect(a, &QAction::triggered, this, [this, i] {
            if (editingLocked())
                return;
            viewport->cancelDrag();
            scene->create(Shape(i));
        });
        createMenu->addAction(a);
    }
    auto *addButton = new QToolButton;
    addButton->setObjectName("createObjectButton");
    addButton->setText(QStringLiteral("创建"));
    addButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    addButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    addButton->setPopupMode(QToolButton::InstantPopup);
    addButton->setMenu(createMenu);
    toolbar->addWidget(addButton);
    auto *simulation = menuBar()->addMenu(QStringLiteral("仿真(&S)"));
    toolbar->addSeparator();
    playAction = action(simulation, QStringLiteral("开始模拟"), QKeySequence("F5"), [this] { startSimulation(); });
    pauseAction = action(simulation, QStringLiteral("暂停模拟"), QKeySequence("F6"), [this] { pauseSimulation(); });
    stepAction = action(simulation, QStringLiteral("单步模拟"), QKeySequence("F7"), [this] { stepSimulation(); });
    resetAction = action(simulation, QStringLiteral("重置模拟"), QKeySequence("F8"), [this] { resetSimulation(); });
    playAction->setObjectName("playSimulation");
    pauseAction->setObjectName("pauseSimulation");
    stepAction->setObjectName("stepSimulation");
    resetAction->setObjectName("resetSimulation");
    playAction->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    pauseAction->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    stepAction->setIcon(style()->standardIcon(QStyle::SP_MediaSkipForward));
    resetAction->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    for (auto *a : {playAction, pauseAction, stepAction, resetAction})
        toolbar->addAction(a);
    physicsStatus = new QLabel;
    physicsStatus->setObjectName("physicsStatus");
    toolbar->addWidget(physicsStatus);

    // Project actions live in their own top-level menu. The view menu is added
    // before this point, so existing dock-menu indices remain stable.
    auto *project = menuBar()->addMenu(QStringLiteral("项目(&P)"));
    auto *managerAction = action(project, QStringLiteral("项目管理…"), QKeySequence("Ctrl+Shift+O"),
                                 [this] { showProjectManager(); });
    managerAction->setObjectName("projectManagerAction");
    auto *newProjectAction = action(project, QStringLiteral("从当前页签新建项目…"), QKeySequence("Ctrl+Shift+N"),
                                    [this] { createProjectFromTabs(); });
    newProjectAction->setObjectName("newProjectAction");
    auto *openProjectAction = action(project, QStringLiteral("打开项目…"), QKeySequence("Ctrl+Alt+O"), [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("打开 BaguaDT 项目"), {},
            QStringLiteral("BaguaDT 项目 (*.bgp);;旧版项目 (*.bagua.project.json);;所有文件 (*)"));
        if (!path.isEmpty())
            openProject(path);
    });
    openProjectAction->setObjectName("openProjectAction");
    auto *saveProjectAction = action(project, QStringLiteral("保存项目"), QKeySequence("Ctrl+Alt+S"),
                                     [this] { saveCurrentProject(); });
    saveProjectAction->setObjectName("saveProjectAction");
    auto *saveProjectAsAction = action(project, QStringLiteral("项目另存为…"), {}, [this] {
        const QString suggested = projectPath.isEmpty() ? QDir::current().filePath(QStringLiteral("project.bgp"))
                                                        : projectPath;
        QString path = QFileDialog::getSaveFileName(this, QStringLiteral("项目另存为"), suggested,
                                                    QStringLiteral("BaguaDT 项目 (*.bgp)"));
        if (!path.isEmpty())
            saveProject(withProjectSuffix(path));
    });
    saveProjectAsAction->setObjectName("saveProjectAsAction");
    project->addSeparator();
    action(project, QStringLiteral("打开 HSC 车辆回放项目"), {}, [this] {
        QString path = QDir(QCoreApplication::applicationDirPath()).filePath("examples/HSC/HSC.bgp");
        if (!QFileInfo::exists(path))
            path = QDir::current().filePath("examples/HSC/HSC.bgp");
        openProject(path);
    });
    action(project, QStringLiteral("打开 HSC 整车示例"), {}, [this] {
        QString path = QDir(QCoreApplication::applicationDirPath()).filePath("examples/hsc-vehicle/HSC-Vehicle.bgp");
        if (!QFileInfo::exists(path))
            path = QDir::current().filePath("examples/hsc-vehicle/HSC-Vehicle.bgp");
        if (openPath(path)) {
            auto options = physicsOptions();
            options.enabled = true;
            setPhysicsOptions(options);
        }
    });
    QWidget *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);
    auto *version = new QLabel(QStringLiteral("BaguaDT 0.3  "));
    toolbar->addWidget(version);
    action(view, QStringLiteral("聚焦选中对象"), {}, [this] { viewport->frameSelected(); });
    action(view, QStringLiteral("重置相机"), QKeySequence("Home"), [this] { viewport->home(); });
    action(view, QStringLiteral("下一个场景"), QKeySequence("Ctrl+Tab"),
           [this] { sceneTabs->setCurrentIndex((sceneTabs->currentIndex() + 1) % sceneTabs->count()); });
    action(view, QStringLiteral("上一个场景"), QKeySequence("Ctrl+Shift+Tab"), [this] {
        sceneTabs->setCurrentIndex((sceneTabs->currentIndex() + sceneTabs->count() - 1) % sceneTabs->count());
    });
    gridAction = view->addAction(QStringLiteral("显示地面网格"));
    gridAction->setCheckable(true);
    gridAction->setChecked(true);
    connect(gridAction, &QAction::toggled, this, [this](bool on) {
        viewport->showGrid = on;
        viewport->update();
    });
    showLinksAction = view->addAction(QStringLiteral("显示装配关系"));
    showLinksAction->setObjectName("showLinksAction");
    showLinksAction->setCheckable(true);
    showLinksAction->setChecked(true);
    showLinksAction->setShortcut(QKeySequence("Ctrl+Shift+L"));
    showLinksAction->setStatusTip(QStringLiteral("在视口中显示约束连线、类型和选中部件高亮（Ctrl+Shift+L）"));
    connect(showLinksAction, &QAction::toggled, this, [this](bool on) {
        viewport->showLinks = on;
        if (showAssemblyViewport) {
            const QSignalBlocker blocker(showAssemblyViewport);
            showAssemblyViewport->setChecked(on);
        }
        viewport->update();
    });
    showAnchorsAction = view->addAction(QStringLiteral("显示锚点"));
    showAnchorsAction->setObjectName("showAnchorsAction");
    showAnchorsAction->setCheckable(true);
    showAnchorsAction->setChecked(true);
    showAnchorsAction->setShortcut(QKeySequence("Ctrl+Shift+A"));
    connect(showAnchorsAction, &QAction::toggled, this, [this](bool on) {
        viewport->showAnchors = on;
        viewport->update();
    });
    showHudAction = view->addAction(QStringLiteral("显示工具提示"));
    showHudAction->setObjectName("showHudAction");
    showHudAction->setCheckable(true);
    showHudAction->setChecked(true);
    showHudAction->setShortcut(QKeySequence("Ctrl+Shift+H"));
    showHudAction->setStatusTip(QStringLiteral("在视口左上角显示当前工具与吸附状态（Ctrl+Shift+H）"));
    connect(showHudAction, &QAction::toggled, this, [this](bool on) {
        viewport->showTransformHud = on;
        viewport->update();
    });
    view->addSeparator();
    view->addAction(spaceAction);
    view->addAction(snapAction);
    auto *frameAllAction = action(view, QStringLiteral("框选全部对象"), QKeySequence("Ctrl+Home"), [this] {
        viewport->frameAll();
    });
    frameAllAction->setObjectName("frameAllAction");
    action(tools, QStringLiteral("验证 CUDA / Chrono 后端"), {}, [this] { validate(); });
    auto *help = menuBar()->addMenu(QStringLiteral("帮助"));
    action(help, QStringLiteral("操作说明"), QKeySequence("F1"), [this] {
        QMessageBox::information(
            this, QStringLiteral("操作说明"),
            QStringLiteral(
                "视口（Unreal / Unity 风格）\n"
                "  左键：选择物体；双击：聚焦。拖动轴柄、平面柄或中心柄变换。\n"
                "  W / E / R：移动 / 旋转 / 缩放    L：世界 / 局部坐标系    G：吸附\n"
                "  拖动时按住 Ctrl 临时吸附，Shift 使用精细步长；方向键微调 X/Z，PageUp/Down 微调 "
                "Y（Shift 加大步长）。\n"
                "  F：聚焦选中    Ctrl+Home：框选全部    Esc：取消拖动    Home：重置相机\n"
                "  右键按住：鼠标转向，W/S 前后、A/D 左右、Q/E 下上；Shift 加速，滚轮调速\n"
                "  Alt+左键：环绕   中键：平移   滚轮：缩放\n\n"
                "属性面板\n"
                "  数值框可直接输入，也可水平拖动拖动刷值；Esc 还原输入前的值。\n"
                "  每行末尾的 ↺ 恢复该行的默认变换；分组标题可折叠。\n\n"
                "装配（约束）\n"
                "  1. 打开右侧“装配 · 约束”页签；选中部件后，编排器默认把它作为部件 A。\n"
                "  2. 用 A / B 下拉框或“视口拾取”选择两个部件；拾取 A 后会自动进入 B。\n"
                "  3. 用“两部件中点 / A 中心 / B 中心 / 视口拾取连接点”确定共同连接点，两侧锚点自动换算。\n"
                "  4. 选择类型与轴向（世界 XYZ 或 A 的局部轴），设置限位 / 驱动 / 阻尼。\n"
                "  5. 点“新建约束”创建；在上方列表选中约束时，视口会高亮其锚点、轴线和实时诊断。\n\n"
                "资产与整车装配\n"
                "  底部“资产记录”可导入网格 / 组件；“拆分全部…”把当前场景对象写成独立组件。\n"
                "  工具 → 车辆四轮装配向导：选择车架、四轮转向节/轮辋/轮胎、松散轮胎，自动建立悬挂与胎唇缝合。\n\n"
                "场景与文件\n"
                "  Ctrl+1/2/3/4：创建立方体 / 球 / 圆柱 / 平面    Ctrl+D：复制    Delete：删除\n"
                "  F2：重命名    Ctrl+Z / Ctrl+Y：撤销 / 重做    Ctrl+S：保存    Ctrl+Shift+S：另存为\n"
                "  项目 → 项目管理 (Ctrl+Shift+O) / 新建项目 (Ctrl+Shift+N) / 保存项目 (Ctrl+Alt+S)，项目后缀 .bgp\n"
                "  F5 开始模拟；F6 暂停；F7 单步；F8 重置。仿真运行时禁止编辑，重置后恢复原始场景。\n"
                "  保存只保存原始场景；相机移动和选择不进入撤销栈。"));
    });
}
void MainWindow::setupSceneTabs() {
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 2, 0);
    header->setSpacing(0);
    sceneTabs = new QTabBar;
    sceneTabs->setObjectName("sceneTabs");
    sceneTabs->setDocumentMode(true);
    sceneTabs->setExpanding(false);
    sceneTabs->setMovable(true);
    sceneTabs->setTabsClosable(true);
    sceneTabs->setUsesScrollButtons(true);
    sceneTabs->setElideMode(Qt::ElideRight);
    header->addWidget(sceneTabs, 1);
    auto *list = new QToolButton;
    list->setObjectName("sceneListButton");
    list->setAutoRaise(true);
    list->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    list->setToolTip(QStringLiteral("已打开的场景"));
    list->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(list);
    connect(menu, &QMenu::aboutToShow, this, [this, menu] {
        menu->clear();
        for (int i = 0; i < sceneTabs->count(); ++i) {
            auto *doc = documentAt(i);
            auto *a = menu->addAction(sceneTabs->tabText(i));
            a->setCheckable(true);
            a->setChecked(doc == activeDocument());
            a->setToolTip(doc->filePath);
            connect(a, &QAction::triggered, this, [this, doc] { sceneTabs->setCurrentIndex(indexOf(doc)); });
        }
    });
    list->setMenu(menu);
    header->addWidget(list);
    auto *add = new QToolButton;
    add->setObjectName("newSceneTabButton");
    add->setText("+");
    add->setAutoRaise(true);
    add->setFixedSize(24, 24);
    add->setToolTip(QStringLiteral("新建场景 (Ctrl+N)"));
    connect(add, &QToolButton::clicked, this, [this] { newScene(); });
    header->addWidget(add);
    layout->addLayout(header);
    layout->addWidget(viewport, 1);
    setCentralWidget(central);
    connect(sceneTabs, &QTabBar::currentChanged, this, &MainWindow::activateDocument);
    connect(sceneTabs, &QTabBar::tabCloseRequested, this, [this](int index) { closeScene(index); });
}
MainWindow::Document *MainWindow::documentAt(int index) const {
    if (index < 0 || index >= sceneTabs->count())
        return nullptr;
    const int id = sceneTabs->tabData(index).toInt();
    for (const auto &doc : documents)
        if (doc->id == id)
            return doc.get();
    return nullptr;
}
MainWindow::Document *MainWindow::activeDocument() const {
    for (const auto &doc : documents)
        if (doc->model.get() == scene)
            return doc.get();
    return nullptr;
}
int MainWindow::indexOf(Document *doc) const {
    if (!doc)
        return -1;
    for (int i = 0; i < sceneTabs->count(); ++i)
        if (sceneTabs->tabData(i).toInt() == doc->id)
            return i;
    return -1;
}
void MainWindow::newScene(bool example) {
    if (simulationStarting || simulationStepWaiting)
        return;
    auto model = std::make_unique<Scene>();
    model->reset(example);
    addDocument(std::move(model), {},
                example ? QStringLiteral("示例场景") : QStringLiteral("未命名场景 %1").arg(nextUntitled++));
    if (console)
        log(QStringLiteral("已新建场景：") + activeDocument()->title);
}
void MainWindow::addDocument(std::unique_ptr<Scene> model, const QString &path, const QString &title) {
    auto doc = std::make_unique<Document>();
    doc->id = nextDocumentId++;
    doc->model = std::move(model);
    doc->filePath = path;
    doc->title = title;
    // A document always owns at least one (initially empty) take, so the list is never empty.
    doc->recordings.push_back(std::make_shared<Recording>());
    auto *modelPtr = doc->model.get();
    auto *session = &doc->simulation;
    auto *runtime = &session->preview;
    auto *replay = &doc->replayPreview;
    connect(replay, &Scene::selectionChanged, this, [this, modelPtr] {
        if (scene == modelPtr) {
            syncSelection();
            syncInspector();
        }
    });
    connect(runtime, &Scene::selectionChanged, this, [modelPtr, runtime] { modelPtr->select(runtime->selected); });
    connect(modelPtr, &Scene::selectionChanged, this, [runtime, modelPtr] { runtime->select(modelPtr->selected); });
    connect(session, &SimulationSession::updated, this, [this, modelPtr] {
        if (scene == modelPtr)
            onSimulationFrame();
    });
    connect(session, &SimulationSession::failed, this, [this, modelPtr](const QString &error) {
        if (scene != modelPtr)
            return;
        log(QStringLiteral("仿真失败，请重置：") + error);
        statusBar()->showMessage(QStringLiteral("仿真失败：") + error);
        pauseSimulation();
    });
    undoGroup->addStack(&modelPtr->undo);
    connect(modelPtr, &Scene::changed, this, [this, modelPtr] {
        if (scene == modelPtr) {
            if (editingLocked())
                resetSimulation();
            syncTree();
            syncInspector();
            viewport->update();
        }
        updateTitle();
    });
    connect(modelPtr, &Scene::selectionChanged, this, [this, modelPtr] {
        if (scene == modelPtr) {
            syncSelection();
            syncInspector();
        }
    });
    connect(&modelPtr->undo, &QUndoStack::cleanChanged, this, [this] { updateTitle(); });
    const int id = doc->id;
    documents.push_back(std::move(doc));
    int index;
    {
        QSignalBlocker blocker(sceneTabs);
        index = sceneTabs->addTab(title);
        sceneTabs->setTabData(index, id);
        sceneTabs->setCurrentIndex(index);
    }
    activateDocument(index);
}
void MainWindow::activateDocument(int index) {
    if (simulationStarting || simulationStepWaiting)
        return;
    auto *doc = documentAt(index);
    if (!doc || doc->model.get() == scene)
        return;
    // The old model stays active until pending inspector text has been committed.
    finishInspectorEdit();
    viewport->cancelDrag();
    if (auto *old = activeDocument()) {
        old->replayPlaying = false;
        replayTimer.stop();
        old->simulation.pause();
        old->model->view = viewport->cameraJson();
        old->tool = viewport->tool;
    }
    pickTarget=0;viewport->anchorPicking=false;loadedLink={};linkComposerTouched=false;
    scene = doc->model.get();
    viewport->setScene(doc->replayActive ? &doc->replayPreview
                                        : doc->simulation.active() ? &doc->simulation.preview : scene);
    viewport->editingEnabled = !(doc->simulation.active() || doc->replayActive);
    viewport->restoreCamera(scene->view);
    viewport->setTool(doc->tool);
    undoGroup->setActiveStack(&scene->undo);
    {
        QSignalBlocker blocker(gridAction);
        gridAction->setChecked(viewport->showGrid);
    }
    syncTree();
    syncInspector();
    updateTitle();
    updateSimulationUi();
    updateReplayUi();
}
void MainWindow::updateTabTitles() {
    for (int i = 0; i < sceneTabs->count(); ++i)
        if (auto *doc = documentAt(i)) {
            const bool unsaved = std::any_of(doc->recordings.cbegin(), doc->recordings.cend(),
                                             [](const auto &r) { return r->dirty && r->frameCount() > 0; });
            sceneTabs->setTabText(i, doc->title + (doc->model->undo.isClean() ? QString{} : QStringLiteral(" *")) +
                                         (unsaved ? QStringLiteral(" ●") : QString{}));
            sceneTabs->setTabToolTip(i, doc->filePath.isEmpty() ? doc->title : QDir::toNativeSeparators(doc->filePath));
        }
}
bool MainWindow::closeScene(int index) {
    if (simulationStarting || simulationStepWaiting) {
        statusBar()->showMessage(QStringLiteral("仿真正在启动或单步计算，请稍候再关闭场景。"), 3000);
        return false;
    }
    auto *closing = documentAt(index);
    if (!closing)
        return false;
    auto *previous = activeDocument();
    sceneTabs->setCurrentIndex(index);
    if (!confirmDiscard()) {
        sceneTabs->setCurrentIndex(indexOf(previous));
        return false;
    }
    if (sceneTabs->count() == 1)
        newScene();
    else if (previous != closing)
        sceneTabs->setCurrentIndex(indexOf(previous));
    else
        sceneTabs->setCurrentIndex(index + 1 < sceneTabs->count() ? index + 1 : index - 1);
    {
        QSignalBlocker blocker(sceneTabs);
        sceneTabs->removeTab(indexOf(closing));
    }
    undoGroup->removeStack(&closing->model->undo);
    documents.erase(
        std::remove_if(documents.begin(), documents.end(), [closing](const auto &doc) { return doc.get() == closing; }),
        documents.end());
    updateTitle();
    return true;
}
void MainWindow::updateTitle() {
    updateTabTitles();
    const auto *doc = activeDocument();
    if (!doc)
        return;
    const QString document = doc->title + (scene->undo.isClean() ? QString{} : QStringLiteral(" *"));
    const QString title = projectName.isEmpty() ? document : projectName + QStringLiteral(" / ") + document;
    setWindowTitle(title + QStringLiteral(" — BaguaDT 场景编辑器"));
}
void MainWindow::log(const QString &text) {
    console->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + text);
}
} // namespace bagua
