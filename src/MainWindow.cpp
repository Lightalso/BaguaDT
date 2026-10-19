#include "MainWindow.h"
#include "Backends.h"
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
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
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
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
QString normalizedScenePath(const QString &path) {
    const QFileInfo info(path);
    const auto canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}
bool sameScenePath(const QString &a, const QString &b) {
    if (a.isEmpty() || b.isEmpty())
        return false;
#ifdef Q_OS_WIN
    return normalizedScenePath(a).compare(normalizedScenePath(b), Qt::CaseInsensitive) == 0;
#else
    return normalizedScenePath(a) == normalizedScenePath(b);
#endif
}
QIcon transformIcon(int tool) {
    QPixmap pixmap(32, 32);
    pixmap.setDevicePixelRatio(2);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#305b83"), 1.3, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
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
MainWindow::MainWindow(QWidget *p) : QMainWindow(p) {
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
    statusBar()->showMessage(QStringLiteral("就绪 · 场景编辑器"));
    countLabel = new QLabel;
    statusBar()->addPermanentWidget(countLabel);
    connect(viewport, &Viewport::message, this, &MainWindow::log);
    connect(viewport, &Viewport::toolChanged, this, [this] { toolActions[int(viewport->tool)]->setChecked(true); });
    physicsTimer.setInterval(16);
    physicsTimer.setTimerType(Qt::PreciseTimer);
    connect(&physicsTimer, &QTimer::timeout, this, &MainWindow::advanceSimulation);
    replayTimer.setInterval(16);
    replayTimer.setTimerType(Qt::PreciseTimer);
    connect(&replayTimer, &QTimer::timeout, this, &MainWindow::advanceReplay);
    newScene(true);
    log(QStringLiteral("BaguaDT 0.3 · 编辑器已启动。单位：米；坐标系：右手系，Y 轴向上。"));
    log(QStringLiteral("右键 + WASD/QE 漫游；Shift 加速；右键 + 滚轮调速。松开右键后 W/E/R 切换编辑工具。"));
    QTimer::singleShot(250, this, [this] { validate(); });
}
void MainWindow::setupMenus() {
    auto *file = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    auto *edit = menuBar()->addMenu(QStringLiteral("编辑(&E)"));
    auto *create = menuBar()->addMenu(QStringLiteral("创建(&C)"));
    auto *view = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    auto *tools = menuBar()->addMenu(QStringLiteral("工具(&T)"));
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
                                              "BaguaDT (*.bagua.json *.bagua.project.json *.brec);;JSON (*.json)");
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
    action(file, QStringLiteral("导入网格资产…"), QKeySequence("Ctrl+I"), [this] {
        if (editingLocked())
            return;
        auto paths = QFileDialog::getOpenFileNames(this, QStringLiteral("导入网格"), {}, "Meshes (*.obj *.stl *.ply *.msh)");
        if (paths.isEmpty())
            return;
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("导入设置"));
        QFormLayout form(&dialog);
        QComboBox units, axis;
        units.addItems({QStringLiteral("米"), QStringLiteral("毫米"), QStringLiteral("厘米")});
        axis.addItems({QStringLiteral("Y 轴向上"), QStringLiteral("Z 轴向上")});
        form.addRow(QStringLiteral("源文件单位"), &units);
        form.addRow(QStringLiteral("源文件坐标系"), &axis);
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form.addRow(&buttons);
        connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        for (auto path : paths)
            importAsset(path, {units.currentIndex() == 1   ? .001
                               : units.currentIndex() == 2 ? .01
                                                           : 1.,
                               axis.currentIndex() == 1});
    });
    action(file, QStringLiteral("打开回放…"), {}, [this] {
        auto p = QFileDialog::getOpenFileName(this, QStringLiteral("打开记录"), {}, "BaguaDT recording (*.brec)");
        if (!p.isEmpty())
            openRecording(p);
    });
    exportRecordingAction = action(file, QStringLiteral("保存录制记录…"), QKeySequence("Ctrl+Shift+R"),
                                   [this] { exportRecordingDialog(); });
    action(file, QStringLiteral("保存完整项目…"), {}, [this] {
        auto p = QFileDialog::getSaveFileName(this, QStringLiteral("保存所有页签为便携项目"),
                                              "project.bagua.project.json", "BaguaDT project (*.bagua.project.json)");
        if (!p.isEmpty())
            saveProject(p);
    });
    action(file, QStringLiteral("打开 HSC 车辆回放项目"), {}, [this] {
        QString p = QDir(QCoreApplication::applicationDirPath()).filePath("examples/HSC/HSC.bagua.project.json");
        if (!QFileInfo::exists(p))
            p = QDir::current().filePath("examples/HSC/HSC.bagua.project.json");
        openProject(p);
    });
    file->addSeparator();
    action(file, QStringLiteral("退出"), QKeySequence::Quit, [this] { close(); });
    for (auto *a : {newAction, openAction, saveAction})
        toolbar->addAction(a);
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
    action(tools, QStringLiteral("验证 CUDA / Chrono 后端"), {}, [this] { validate(); });
    auto *help = menuBar()->addMenu(QStringLiteral("帮助"));
    action(help, QStringLiteral("操作说明"), QKeySequence("F1"), [this] {
        QMessageBox::information(
            this, QStringLiteral("操作说明"),
            QStringLiteral(
                "按住右键：鼠标原地转向，W/S 前后、A/D 左右、Q/E 下上\n"
                "右键漫游时：Shift 加速；滚轮调整移动速度\n"
                "Alt+左键拖动：环绕相机\n中键拖动：平移相机\n滚轮：缩放相机\n左键：选中物体 / 拖动三轴操作柄\nW "
                "/ E / R：移动 / 旋转 / "
                "缩放（视口获焦且未按右键时）\nShift：精细变换；Esc：取消拖动或停止漫游\nF：聚焦选中；Home：重置相机\nC"
                "trl+"
                "1/2/3/4：创建立方体 / 球 / 圆柱 / "
                "平面\nCtrl+D：复制；Delete：删除；F2：重命名\nCtrl+Z：撤销；Ctrl+Y：重做\nCtrl+"
                "S：保存；Ctrl+Shift+S：另存为\n\n尺寸表示局部包围尺寸（米），球 / 圆柱的 X、Z "
                "为直径。\n球体允许编辑为椭球，圆柱允许椭圆截面。\n变换顺序：局部缩放 → Qt 欧拉姿态 → "
                "平移。\nF5 开始模拟；F6 暂停模拟；F7 单步模拟；F8 重置模拟。\n"
                "仿真时禁止编辑，重置后恢复原始场景。保存仅保存原始场景。\n"
                "关闭碰撞的动态刚体仍受重力影响；平面始终为固定刚体。\n回放播放、暂停和逐帧控制位于底部记录栏。HSC "
                "开关位于工具 → 物理模拟设置。"));
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
    auto *runtime = &doc->physics.preview;
    auto *replay = &doc->replayPreview;
    connect(replay, &Scene::selectionChanged, this, [this, modelPtr] {
        if (scene == modelPtr) {
            syncSelection();
            syncInspector();
        }
    });
    connect(runtime, &Scene::selectionChanged, this, [modelPtr, runtime] { modelPtr->select(runtime->selected); });
    connect(modelPtr, &Scene::selectionChanged, this, [runtime, modelPtr] { runtime->select(modelPtr->selected); });
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
    auto *doc = documentAt(index);
    if (!doc || doc->model.get() == scene)
        return;
    // The old model stays active until pending inspector text has been committed.
    finishInspectorEdit();
    viewport->cancelDrag();
    if (auto *old = activeDocument()) {
        old->replayPlaying = false;
        replayTimer.stop();
        old->running = false;
        physicsTimer.stop();
        physicsAccumulator = 0;
        old->model->view = viewport->cameraJson();
        old->tool = viewport->tool;
    }
    scene = doc->model.get();
    viewport->setScene(doc->replayActive ? &doc->replayPreview : doc->physics.active() ? &doc->physics.preview : scene);
    viewport->editingEnabled = !(doc->physics.active() || doc->replayActive);
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
void MainWindow::setupDocks() {
    auto *sceneDock = new QDockWidget(QStringLiteral("场景"), this);
    sceneDock->setObjectName("sceneDock");
    sceneDock->setMinimumWidth(165);
    tree = new QTreeWidget;
    tree->setObjectName("sceneTree");
    tree->setHeaderLabels({QStringLiteral("对象"), QStringLiteral("类型")});
    tree->setRootIsDecorated(false);
    tree->setAlternatingRowColors(true);
    tree->setUniformRowHeights(true);
    tree->header()->setStretchLastSection(false);
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    sceneDock->setWidget(tree);
    addDockWidget(Qt::LeftDockWidgetArea, sceneDock);
    connect(tree, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (syncing)
            return;
        viewport->cancelDrag();
        auto items = tree->selectedItems();
        displayScene()->select(items.isEmpty() ? QString{} : items.first()->data(0, Qt::UserRole).toString());
    });
    connect(tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *, int column) {
        if (column != 0)
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
        QMenu menu;
        menu.addAction(duplicateAction);
        menu.addAction(deleteAction);
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
    auto *objectGroup = new QGroupBox(QStringLiteral("对象"));
    auto *objectForm = new QFormLayout(objectGroup);
    objectForm->setContentsMargins(8, 8, 8, 8);
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
    auto *transformGroup = new QGroupBox(QStringLiteral("变换"));
    auto *transformGrid = new QGridLayout(transformGroup);
    auto *geometryGroup = new QGroupBox(QStringLiteral("几何"));
    auto *geometryGrid = new QGridLayout(geometryGroup);
    for (auto *grid : {transformGrid, geometryGrid}) {
        grid->setContentsMargins(6, 8, 6, 8);
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
    }
    const QStringList rows = {QStringLiteral("位置 (m)"), QStringLiteral("旋转 (°)"), QStringLiteral("缩放"),
                              QStringLiteral("尺寸 (m)")};
    for (int row = 0; row < 4; ++row) {
        auto *grid = row < 3 ? transformGrid : geometryGrid;
        const int gridRow = row < 3 ? row + 1 : 1;
        grid->addWidget(new QLabel(rows[row]), gridRow, 0);
        for (int a = 0; a < 3; ++a) {
            auto *field = new QDoubleSpinBox;
            fields[row * 3 + a] = field;
            field->setObjectName(QString("field_%1_%2").arg(row).arg(a));
            field->setDecimals(3);
            field->setRange(row >= 2 ? .001 : -1e6, 1e6);
            field->setSingleStep(row == 1 ? 1 : .1);
            field->setKeyboardTracking(false);
            field->setMinimumWidth(0);
            field->setMinimumHeight(22);
            field->setAlignment(Qt::AlignRight);
            field->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            field->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
            field->setToolTip(rows[row] + " / " + QString("XYZ").mid(a, 1));
            grid->addWidget(field, gridRow, a + 1);
            connect(field, &QDoubleSpinBox::editingFinished, this, &MainWindow::commitFields);
        }
    }
    geometryGroup->setToolTip(
        QStringLiteral("球 / 圆柱：X、Z 为直径，Y 为高度。\n平面位于局部 XZ 面，Y 尺寸不参与显示。"));
    layout->addWidget(transformGroup);
    layout->addWidget(geometryGroup);
    auto *appearanceGroup = new QGroupBox(QStringLiteral("外观"));
    auto *appearanceForm = new QFormLayout(appearanceGroup);
    appearanceForm->setContentsMargins(8, 8, 8, 8);
    color = new QPushButton(QStringLiteral("选择颜色…"));
    color->setObjectName("objectColor");
    color->setMinimumHeight(22);
    appearanceForm->addRow(QStringLiteral("颜色"), color);
    layout->addWidget(appearanceGroup);
    auto *physicsGroup = new QGroupBox(QStringLiteral("刚体物理"));
    auto *physicsForm = new QFormLayout(physicsGroup);
    physicsForm->setContentsMargins(8, 8, 8, 8);
    physicsForm->setSpacing(4);
    collision = new QCheckBox(QStringLiteral("启用碰撞"));
    collision->setObjectName("objectCollision");
    collision->setToolTip(QStringLiteral("关闭后仍受重力影响，但不与其他物体发生接触。隐藏对象仍参与物理模拟。"));
    fixedBody = new QCheckBox(QStringLiteral("固定刚体"));
    fixedBody->setObjectName("objectFixed");
    fixedBody->setToolTip(
        QStringLiteral("固定网格使用原始三角面碰撞；动态网格使用凸包和包围盒近似惯量。平面始终固定。"));
    mass = new QDoubleSpinBox;
    mass->setObjectName("objectMass");
    mass->setDecimals(3);
    mass->setRange(.001, 1e9);
    mass->setKeyboardTracking(false);
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
        i->setFlags(i->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
        i->setCheckState(0, o.visible ? Qt::Checked : Qt::Unchecked);
        i->setToolTip(0, o.id);
        if (o.id == displayScene()->selected)
            tree->setCurrentItem(i);
    }
    tree->verticalScrollBar()->setValue(scroll);
    syncing = false;
    syncSelection();
}
void MainWindow::syncSelection() {
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
void MainWindow::updateTitle() {
    updateTabTitles();
    const auto *doc = activeDocument();
    if (!doc)
        return;
    setWindowTitle(QString("%1%2 — BaguaDT 场景编辑器").arg(doc->title, scene->undo.isClean() ? "" : " *"));
}
void MainWindow::log(const QString &text) {
    console->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + text);
}
bool MainWindow::savePath(const QString &path) {
    finishInspectorEdit();
    viewport->cancelDrag();
    auto *doc = activeDocument();
    if (!doc)
        return false;
    const QString resolved = normalizedScenePath(path);
    for (const auto &other : documents)
        if (other.get() != doc && sameScenePath(other->filePath, resolved)) {
            QMessageBox::warning(
                this, QStringLiteral("无法保存"),
                QStringLiteral("该文件已在页签“%1”中打开，请切换到该页签，或另选保存位置。").arg(other->title));
            return false;
        }
    scene->view = viewport->cameraJson();
    QString error;
    if (!scene->save(resolved, &error)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), error);
        log(QStringLiteral("保存失败：") + error);
        return false;
    }
    doc->filePath = resolved;
    doc->title = QFileInfo(resolved).fileName();
    updateTitle();
    log(QStringLiteral("已保存：") + path);
    return true;
}
bool MainWindow::openPath(const QString &path) {
    if (path.endsWith(".brec", Qt::CaseInsensitive))
        return openRecording(path);
    if (path.endsWith(".bagua.project.json", Qt::CaseInsensitive))
        return openProject(path);
    finishInspectorEdit();
    viewport->cancelDrag();
    const QString resolved = normalizedScenePath(path);
    for (const auto &doc : documents)
        if (sameScenePath(doc->filePath, resolved)) {
            sceneTabs->setCurrentIndex(indexOf(doc.get()));
            return true;
        }
    auto model = std::make_unique<Scene>();
    QString error;
    if (!model->load(resolved, &error)) {
        QMessageBox::warning(this, QStringLiteral("打开失败"), error);
        log(QStringLiteral("打开失败：") + error);
        return false;
    }
    addDocument(std::move(model), resolved, QFileInfo(resolved).fileName());
    log(QStringLiteral("已打开：") + path);
    return true;
}
bool MainWindow::save(bool as) {
    QString path = activeDocument()->filePath;
    if (as || path.isEmpty())
        path = QFileDialog::getSaveFileName(this, QStringLiteral("保存场景"),
                                            path.isEmpty() ? "untitled.bagua.json" : path, "BaguaDT (*.bagua.json)");
    if (path.isEmpty())
        return false;
    if (!path.endsWith(".json", Qt::CaseInsensitive))
        path += ".bagua.json";
    return savePath(path);
}
bool MainWindow::confirmDiscard() {
    finishInspectorEdit();
    endRecording();
    auto *doc = activeDocument();
    QVector<int> unsaved;
    if (doc)
        for (int i = 0; i < int(doc->recordings.size()); ++i)
            if (doc->recordings[size_t(i)]->dirty && doc->recordings[size_t(i)]->frameCount())
                unsaved.push_back(i);
    if (!unsaved.isEmpty()) {
        auto answer = QMessageBox::question(
            this, QStringLiteral("保存录制记录"),
            QStringLiteral("当前场景有 %1 条未保存的回放记录，是否逐条保存？").arg(unsaved.size()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (answer == QMessageBox::Cancel)
            return false;
        if (answer == QMessageBox::Save)
            for (int index : unsaved)
                if (!exportRecordingAt(index))
                    return false;
    }
    if (scene->undo.isClean())
        return true;
    auto answer =
        QMessageBox::question(this, QStringLiteral("保存场景"),
                              QStringLiteral("场景“%1”有未保存的修改，是否保存？").arg(activeDocument()->title),
                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save)
        return save();
    return true;
}
void MainWindow::closeEvent(QCloseEvent *e) {
    finishInspectorEdit();
    viewport->cancelDrag();
    auto *previous = activeDocument();
    for (int i = 0; i < sceneTabs->count(); ++i) {
        sceneTabs->setCurrentIndex(i);
        if (!confirmDiscard()) {
            sceneTabs->setCurrentIndex(indexOf(previous));
            e->ignore();
            return;
        }
    }
    e->accept();
}
void MainWindow::validate() {
    auto r = validateBackends(*scene);
    log(r.cuda);
    log(QStringLiteral("后端自检：") + r.chrono);
    if (simulationActive())
        log(activeDocument()->physics.solverDescription());
    log(activeDocument() && activeDocument()->replayActive
            ? QStringLiteral("当前为保存结果回放：请使用底部记录栏播放、暂停和逐帧控制；顶栏仅控制物理模拟。")
            : QStringLiteral("F5 启动 Chrono 刚体仿真；F6 暂停；F7 单步；F8 重置。"));
}
bool MainWindow::simulationActive() const {
    auto *d = activeDocument();
    return d && d->physics.active();
}
bool MainWindow::simulationRunning() const {
    auto *d = activeDocument();
    return d && d->running;
}
double MainWindow::simulationTime() const {
    auto *d = activeDocument();
    return d ? d->physics.time() : 0;
}
const Scene *MainWindow::simulationPreview() const {
    auto *d = activeDocument();
    return d && d->physics.active() ? &d->physics.preview : nullptr;
}
bool MainWindow::startSimulation() {
    if (auto *d = activeDocument(); d && d->replayActive) {
        log(QStringLiteral("请先通过底部退出回放，再开始物理模拟。"));
        return false;
    }
    finishInspectorEdit();
    viewport->cancelDrag();
    auto *d = activeDocument();
    if (!d)
        return false;
    if (!d->physics.active()) {
        QString error;
        if (!d->physics.start(*scene, &error, solverOptions)) {
            log(QStringLiteral("无法启动仿真：") + error);
            statusBar()->showMessage(QStringLiteral("仿真启动失败：") + error);
            return false;
        }
        viewport->setScene(&d->physics.preview);
        viewport->editingEnabled = false;
        log(d->physics.solverDescription() +
            QStringLiteral("；%1 个对象，重力 -9.81 m/s²，步长 1/240 s。").arg(scene->objects.size()));
    }
    d->running = true;
    physicsAccumulator = 0;
    physicsClock.restart();
    physicsTimer.start();
    updateSimulationUi();
    return true;
}
void MainWindow::pauseSimulation() {
    if (auto *d = activeDocument(); d && d->replayActive) {
        return;
    }
    if (auto *d = activeDocument())
        d->running = false;
    physicsTimer.stop();
    physicsAccumulator = 0;
    updateSimulationUi();
}
void MainWindow::resetSimulation() {
    auto *d = activeDocument();
    if (!d)
        return;
    if (d->replayActive) {
        return;
    }
    endRecording();
    pauseSimulation();
    // Detach the viewport before destroying its runtime objects.
    viewport->setScene(scene);
    viewport->editingEnabled = true;
    d->physics.reset();
    updateSimulationUi();
}
bool MainWindow::stepSimulation() {
    if (replayActive())
        return false;
    if (!simulationActive() && !startSimulation())
        return false;
    pauseSimulation();
    QString error;
    bool ok = activeDocument()->physics.step(1, &error);
    if (!ok)
        log(QStringLiteral("仿真失败，请重置：") + error);
    if (ok)
        captureFrame();
    updateSimulationUi();
    return ok;
}
void MainWindow::advanceSimulation() {
    auto *d = activeDocument();
    if (!d || !d->running)
        return;
    QString error;
    if (d->capturing && d->offlineCapture) {
        // Lock-step capture: no wall-clock catch-up. Each tick advances exactly one integration
        // step and records one frame, so a slow frame only makes the run take longer than real
        // time instead of skipping simulation time or thinning the samples.
        physicsAccumulator = 0;
        physicsClock.restart();
        if (!d->physics.step(1, &error)) {
            pauseSimulation();
            log(QStringLiteral("仿真失败，请重置：") + error);
        } else
            captureFrame();
    } else {
        physicsAccumulator += std::min(.1, physicsClock.nsecsElapsed() * 1e-9);
        physicsClock.restart();
        const int steps = int(physicsAccumulator / Physics::timeStep);
        if (steps > 0) {
            physicsAccumulator -= steps * Physics::timeStep;
            if (!d->physics.step(steps, &error)) {
                pauseSimulation();
                log(QStringLiteral("仿真失败，请重置：") + error);
            } else
                captureFrame();
        }
    }
    physicsStatus->setText(
        QStringLiteral(" %1 s · %2 接触 ").arg(d->physics.time(), 0, 'f', 3).arg(d->physics.contacts()));
    if (d->physics.hscActive()) {
        const auto stats = d->physics.hscStats();
        physicsStatus->setText(QStringLiteral(" %1 s · HSC 秩%2 · r=%3 ")
                                   .arg(d->physics.time(), 0, 'f', 3)
                                   .arg(stats.rank)
                                   .arg(stats.relativeResidual, 0, 'g', 2));
    }
    physicsStatus->setToolTip(d->physics.solverDescription());
}
void MainWindow::updateSimulationUi() {
    const bool active = editingLocked(), running = simulationRunning();
    const bool replay = activeDocument() && activeDocument()->replayActive;
    playAction->setEnabled(Physics::available() && !replay && !running);
    playAction->setText(simulationActive() ? QStringLiteral("继续模拟") : QStringLiteral("开始模拟"));
    pauseAction->setEnabled(running && !replay);
    resetAction->setEnabled(simulationActive() && !replay);
    stepAction->setEnabled(Physics::available() && !replay && !running);
    for (int i : {1, 2})
        menuBar()->actions()[i]->setEnabled(!active);
    for (auto *a : toolActions)
        a->setEnabled(!active);
    findChild<QToolButton *>("createObjectButton")->setEnabled(!active);
    findChild<QWidget *>("geometryAssets")->setEnabled(!active);
    // Keep tree selection available; editing is disabled during a runtime session.
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        auto *item = tree->topLevelItem(i);
        auto flags = item->flags();
        flags.setFlag(Qt::ItemIsEditable, !active);
        flags.setFlag(Qt::ItemIsUserCheckable, !active);
        item->setFlags(flags);
    }
    physicsStatus->setText(simulationActive() ? QStringLiteral(" 模拟 %1 s · %2 ")
                                                    .arg(simulationTime(), 0, 'f', 3)
                                                    .arg(running ? QStringLiteral("运行") : QStringLiteral("暂停"))
                                              : QStringLiteral(" 模拟未开始 "));
    if (simulationActive()) {
        const auto &physics = activeDocument()->physics;
        physicsStatus->setToolTip(physics.solverDescription());
        if (physics.hscActive()) {
            const auto stats = physics.hscStats();
            physicsStatus->setText(QStringLiteral(" %1 s · HSC 秩%2 · r=%3 ")
                                       .arg(physics.time(), 0, 'f', 3)
                                       .arg(stats.rank)
                                       .arg(stats.relativeResidual, 0, 'g', 2));
        }
    }
    statusBar()->showMessage(active ? (replay ? QStringLiteral("记录回放 · 当前显示保存的求解结果；可拖动时间轴")
                                              : simulationActive()
                                                    ? (activeDocument()->physics.hscActive()
                                                           ? QStringLiteral("原生 HSC 仿真预览 · F8 重置后可编辑；保存原始场景")
                                                           : QStringLiteral("Chrono 仿真预览 · F8 重置后可编辑；保存原始场景"))
                                                    : QStringLiteral("Chrono 仿真预览 · F8 重置后可编辑；保存原始场景"))
                                    : QStringLiteral("就绪 · 场景编辑器"));
    syncSelection();
    syncInspector();
    viewport->update();
    updateReplayUi();
}
} // namespace bagua
