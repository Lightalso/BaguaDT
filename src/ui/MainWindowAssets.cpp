#include "MainWindow.h"
#include "ProjectFile.h"
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolButton>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>
namespace bagua {
Scene *MainWindow::displayScene() const {
    auto *d = activeDocument();
    return d && d->replayActive ? &d->replayPreview : scene;
}
void MainWindow::dragEnterEvent(QDragEnterEvent *e) {
    if (!e->mimeData()->hasUrls())
        return;
    for (auto url : e->mimeData()->urls()) {
        const auto ext = QFileInfo(url.toLocalFile()).suffix().toLower();
        if (!url.isLocalFile() || !QStringList{"obj", "stl", "ply", "msh", "json", "brec"}.contains(ext))
            return;
    }
    e->acceptProposedAction();
}
void MainWindow::dropEvent(QDropEvent *e) {
    for (auto url : e->mimeData()->urls()) {
        auto path = url.toLocalFile();
        auto ext = QFileInfo(path).suffix().toLower();
        if (ext == "obj" || ext == "stl" || ext == "ply" || ext == "msh")
            importAsset(path);
        else
            openPath(path);
    }
    e->acceptProposedAction();
}
void MainWindow::setupReplayDock() {
    auto *bottom = findChild<QDockWidget *>("workspaceDock");
    auto *tabs = qobject_cast<QTabWidget *>(bottom->widget());
    assetTree = new QTreeWidget;
    assetTree->setObjectName("assetLibrary");
    assetTree->setHeaderLabels({QStringLiteral("资产 / 对象"), QStringLiteral("顶点"), QStringLiteral("三角形"),
                                QStringLiteral("来源与指纹")});
    assetTree->setRootIsDecorated(false);
    assetTree->setAlternatingRowColors(true);
    assetTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    assetTree->header()->setStretchLastSection(true);
    auto *assetPage = new QWidget;
    auto *assetLayout = new QVBoxLayout(assetPage);
    assetLayout->setContentsMargins(0, 0, 0, 0);
    assetLayout->setSpacing(3);
    auto *assetButtons = new QHBoxLayout;
    assetButtons->setContentsMargins(3, 2, 3, 0);
    assetButtons->setSpacing(4);
    auto assetButton = [&](const QString &text, const QString &name, const std::function<void()> &fn) {
        auto *button = new QToolButton;
        button->setText(text);
        button->setObjectName(name);
        button->setAutoRaise(true);
        assetButtons->addWidget(button);
        connect(button, &QToolButton::clicked, this, fn);
        return button;
    };
    assetButton(QStringLiteral("导入网格…"), QStringLiteral("assetImportMesh"),
                [this] { importMeshDialog(); });
    assetButton(QStringLiteral("导入组件…"), QStringLiteral("assetImportComponent"), [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("导入组件"), {},
            QStringLiteral("BaguaDT 组件 (*.bagua.json);;所有文件 (*)"));
        if (!path.isEmpty())
            importComponent(path);
    });
    assetButton(QStringLiteral("拆分全部…"), QStringLiteral("assetExportComponents"),
                [this] { exportComponents(false); });
    assetButton(QStringLiteral("导出当前…"), QStringLiteral("assetExportCurrent"),
                [this] { exportComponents(true); });
    assetButtons->addStretch();
    assetLayout->addLayout(assetButtons);
    assetLayout->addWidget(assetTree);
    tabs->addTab(assetPage, QStringLiteral("资产记录"));
    connect(assetTree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *i, int) {
        if (scene)
            displayScene()->select(i->data(0, Qt::UserRole).toString());
    });
    connect(assetTree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *, int) { viewport->frameSelected(); });
    auto *dock = new QDockWidget(QStringLiteral("记录与回放"), this);
    dock->setObjectName("replayDock");
    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(3);
    recordingList = new QListWidget;
    recordingList->setObjectName("recordingList");
    recordingList->setSelectionMode(QAbstractItemView::SingleSelection);
    recordingList->setAlternatingRowColors(true);
    recordingList->setUniformItemSizes(true);
    recordingList->setMinimumHeight(46);
    recordingList->setMaximumHeight(78);
    recordingList->setToolTip(QStringLiteral("当前场景的所有录制结果。选中一条即可回放、保存或删除。"));
    layout->addWidget(recordingList);
    auto *row = new QHBoxLayout;
    row->setSpacing(4);
    auto button = [&](QString text, QString objectName, std::function<void()> fn) {
        auto *b = new QToolButton;
        b->setText(text);
        b->setObjectName(objectName);
        b->setAutoRaise(true);
        row->addWidget(b);
        connect(b, &QToolButton::clicked, this, fn);
        return b;
    };
    auto actionButton = [&](QString text, QString objectName, std::function<void()> fn) {
        auto *a = new QAction(text, this);
        a->setObjectName(objectName);
        addAction(a);
        auto *b = new QToolButton;
        b->setDefaultAction(a);
        b->setAutoRaise(true);
        row->addWidget(b);
        connect(a, &QAction::triggered, this, fn);
        return a;
    };
    recordAction = actionButton(QStringLiteral("● 录制"), "recordSimulation", [this] {
        if (activeDocument()->capturing)
            endRecording();
        else
            beginRecording();
    });
    recordWithSimulationAction = actionButton(QStringLiteral("▶● 模拟并录制"), "recordWithSimulation", [this] {
        auto *d = activeDocument();
        if (!d || d->replayActive || d->capturing)
            return;
        if (!startSimulation())
            return;
        beginRecording();
    });
    recordWithSimulationAction->setToolTip(
        QStringLiteral("启动物理模拟并立即开始录制；结束后用「■ 停止录制」再保存记录。"));
    recordOffline = new QCheckBox(QStringLiteral("离线高质量"));
    recordOffline->setObjectName("recordOffline");
    recordOffline->setToolTip(QStringLiteral("录制时锁定固定步长：每个节拍只推进一个物理步并记一帧，不追实时。\n"
                                             "单帧耗时较高时仿真会明显变慢，但回放帧间距等于场景 dt，不会因卡顿而跳帧；此开关不改变物理精度或渲染分辨率。"));
    row->addWidget(recordOffline);
    connect(recordOffline, &QCheckBox::toggled, this, [this](bool on) {
        auto *d = activeDocument();
        if (!d)
            return;
        d->offlineCapture = on;
        d->simulation.setLockStep(on);
        log(on ? QStringLiteral("录制采样：离线高质量（锁步，一帧一个物理步，不追实时）。")
               : QStringLiteral("录制采样：实时（按实际时间推进，帧间距随负载变化）。"));
        if (d->capturing) {
            auto changes=d->recording().metadata["samplingChanges"].toArray();
            changes.append(QJsonObject{{"fromFrame",d->recording().frameCount()},{"time",d->simulation.time()-d->captureOrigin},{"mode",on?"lock-step fixed timestep":"render-tick physical timestamps"}});
            d->recording().metadata["samplingChanges"]=changes;d->recording().metadata["sampling"]="mixed";
        }
    });
    exportRecordingAction->setEnabled(false);
    button(QStringLiteral("打开记录…"), "openRecordingButton", [this] {
        auto p = QFileDialog::getOpenFileName(this, QStringLiteral("打开记录"), {}, "BaguaDT (*.brec)");
        if (!p.isEmpty())
            openRecording(p);
    });
    auto *saveButton = new QToolButton;
    saveButton->setDefaultAction(exportRecordingAction);
    saveButton->setAutoRaise(true);
    row->addWidget(saveButton);
    deleteRecordingAction = actionButton(QStringLiteral("删除所选"), "deleteSelectedRecording", [this] {
        auto *item = recordingList ? recordingList->currentItem() : nullptr;
        deleteRecordingAt(item ? item->data(Qt::UserRole).toInt() : -1);
    });
    deleteRecordingAction->setToolTip(QStringLiteral("删除列表中选中的录制结果；已保存到磁盘的文件不受影响。"));
    connect(recordingList, &QListWidget::currentRowChanged, this, [this](int) {
        if (syncing)
            return;
        auto *item = recordingList->currentItem();
        selectRecordingAt(item ? item->data(Qt::UserRole).toInt() : -1);
    });
    connect(recordingList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { playReplay(); });
    recordingList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(recordingList, &QListWidget::customContextMenuRequested, this, [this](const QPoint &p) {
        auto *item = recordingList->itemAt(p);
        if (!item)
            return;
        recordingList->setCurrentItem(item);
        QMenu menu(this);
        menu.addAction(exportRecordingAction);
        menu.addAction(deleteRecordingAction);
        menu.exec(recordingList->viewport()->mapToGlobal(p));
    });
    replayPlayAction = actionButton(QStringLiteral("▶ 开始回放"), "playReplay", [this] { playReplay(); });
    replayPauseAction = actionButton(QStringLiteral("暂停回放"), "pauseReplay", [this] { pauseReplay(); });
    button(QStringLiteral("|◀"), "firstReplayFrame", [this] {
        pauseReplay();
        seekReplay(0);
    });
    button(QStringLiteral("◀"), "previousReplayFrame", [this] {
        pauseReplay();
        auto *d = activeDocument();
        if (!d || d->recording().times.isEmpty())
            return;
        auto it = std::lower_bound(d->recording().times.cbegin(), d->recording().times.cend(), d->replayTime - 1e-8);
        seekReplay(it == d->recording().times.cbegin() ? 0 : *(it - 1));
    });
    button(QStringLiteral("▶|"), "nextReplayFrame", [this] {
        pauseReplay();
        auto *d = activeDocument();
        if (!d || d->recording().times.isEmpty())
            return;
        auto it = std::upper_bound(d->recording().times.cbegin(), d->recording().times.cend(), d->replayTime + 1e-8);
        seekReplay(it == d->recording().times.cend() ? d->recording().duration() : *it);
    });
    button(QStringLiteral("▶| 末帧"), "lastReplayFrame", [this] {
        pauseReplay();
        seekReplay(activeDocument()->recording().duration());
    });
    replayStopAction = actionButton(QStringLiteral("退出回放"), "stopReplay", [this] { stopReplay(); });
    replaySpeed = new QComboBox;
    replaySpeed->setObjectName("replaySpeed");
    for (double rate : {.25, .5, 1., 2., 4.})
        replaySpeed->addItem(QString::number(rate) + "×", rate);
    replaySpeed->setCurrentIndex(2);
    row->addWidget(replaySpeed);
    replayLoop = new QCheckBox(QStringLiteral("循环"));
    replayLoop->setObjectName("replayLoop");
    row->addWidget(replayLoop);
    replayInterpolate = new QCheckBox(QStringLiteral("插值"));
    replayInterpolate->setObjectName("replayInterpolate");
    replayInterpolate->setChecked(true);
    row->addWidget(replayInterpolate);
    row->addStretch();
    layout->addLayout(row);
    auto *timeRow = new QHBoxLayout;
    timeline = new QSlider(Qt::Horizontal);
    timeline->setObjectName("replayTimeline");
    timeline->setRange(0, 100000);
    timeRow->addWidget(timeline, 1);
    replayTimeField = new QDoubleSpinBox;
    replayTimeField->setObjectName("replayTime");
    replayTimeField->setDecimals(4);
    replayTimeField->setSuffix(" s");
    replayTimeField->setKeyboardTracking(false);
    timeRow->addWidget(replayTimeField);
    replayInfo = new QLabel(QStringLiteral("尚无记录"));
    replayInfo->setObjectName("replayInfo");
    timeRow->addWidget(replayInfo);
    layout->addLayout(timeRow);
    connect(timeline, &QSlider::sliderPressed, this, &MainWindow::pauseReplay);
    connect(timeline, &QSlider::valueChanged, this, [this](int value) {
        if (auto *d = activeDocument(); d && !syncing && d->recording().frameCount()) {
            pauseReplay();
            seekReplay(d->recording().duration() * value / 100000.);
        }
    });
    connect(replayTimeField, &QDoubleSpinBox::editingFinished, this, [this] {
        pauseReplay();
        seekReplay(replayTimeField->value());
    });
    dock->setWidget(panel);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
    splitDockWidget(dock, bottom, Qt::Vertical);
    resizeDocks({dock, bottom}, {124, 96}, Qt::Vertical);
}
void MainWindow::syncAssets() {
    if (!assetTree || !scene)
        return;
    assetTree->clear();
    for (const auto &o : displayScene()->objects)
        if (o.mesh) {
            auto *i = new QTreeWidgetItem(assetTree, {o.name, QString::number(o.mesh->positions.size()),
                                                      QString::number(o.mesh->indices.size() / 3), o.mesh->source});
            i->setData(0, Qt::UserRole, o.id);
            i->setToolTip(3, o.mesh->source + "\nSHA-256: " + o.mesh->sha256 + "\nAsset ID: " + o.mesh->id);
        }
}
bool MainWindow::importAsset(const QString &path, ImportOptions options) {
    if (editingLocked()) {
        log(QStringLiteral("请先退出仿真或回放再导入"));
        return false;
    }
    finishInspectorEdit();
    viewport->cancelDrag();
    QVector<Object> imported;
    QString error;
    if (!Assets::importFile(path, imported, &error, options)) {
        log(QStringLiteral("资产导入失败：") + path + " · " + error);
        return false;
    }
    if (scene->objects.size() + imported.size() > 10000) {
        log(QStringLiteral("导入后对象数量超出限制"));
        return false;
    }
    auto next = scene->objects;
    next += imported;
    QSet<QString> assets;
    qsizetype vertices = 0, indices = 0;
    for (const auto &o : next)
        if (o.mesh && !assets.contains(o.mesh->id)) {
            assets.insert(o.mesh->id);
            vertices += o.mesh->positions.size();
            indices += o.mesh->indices.size();
        }
    if (vertices > 2000000 || indices > 12000000) {
        log(QStringLiteral("导入后网格数据超出场景限制"));
        return false;
    }
    scene->replace(next, imported.first().id, QStringLiteral("导入网格资产"));
    viewport->frameSelected();
    log(QStringLiteral("已导入 %1 个网格：%2；默认固定刚体，可在属性中启用动态凸包碰撞。")
            .arg(imported.size())
            .arg(path));
    return true;
}
int MainWindow::recordedFrames() const {
    auto *d = activeDocument();
    return d ? d->recording().frameCount() : 0;
}
int MainWindow::recordingCount() const {
    auto *d = activeDocument();
    if (!d)
        return 0;
    return int(std::count_if(d->recordings.cbegin(), d->recordings.cend(),
                             [](const auto &r) { return r->frameCount() > 0; }));
}
bool MainWindow::beginRecording() {
    auto *d = activeDocument();
    if (!d || d->replayActive || d->capturing)
        return false;
    if (!d->simulation.active()) {
        log(QStringLiteral("请先通过顶栏开始模拟，再开启录制。"));
        return false;
    }
    // A new take never overwrites an earlier one: only the empty working entry is reused.
    if (d->recording().frameCount() > 0) {
        d->recordings.push_back(std::make_shared<Recording>());
        d->activeRecording = int(d->recordings.size()) - 1;
    }
    d->captureOrigin = d->simulation.time();
    d->recording().begin(d->simulation.preview, {{"producer", d->simulation.solverDescription().prepend("BaguaDT ")},
                                            {"hscEnabled", d->simulation.hscActive()},{"simulation",scene->simulation},
                                            {"created", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                                            {"integrationStep", d->simulation.stepSize()},
                                            {"sampling", d->offlineCapture ? "lock-step fixed timestep"
                                                                           : "render-tick physical timestamps"}});
    d->recording().initial.view = viewport->cameraJson();
    d->capturing = true;
    log(d->offlineCapture ? QStringLiteral("开始录制：逐步采样，每个物理步记一帧，帧间距 %1 s。").arg(d->simulation.stepSize(),0,'g',8)
                          : QStringLiteral("开始录制：记录实际物理时间与对象状态。"));
    updateReplayUi();
    return true;
}
void MainWindow::setOfflineCapture(bool on) {
    auto *d = activeDocument();
    if (!d)
        return;
    d->offlineCapture = on;
    d->simulation.setLockStep(on);
    if (recordOffline) {
        QSignalBlocker block(recordOffline);
        recordOffline->setChecked(on);
    }
}
void MainWindow::captureFrame() {
    auto *d = activeDocument();
    if (!d || !d->capturing)
        return;
    QString error;
    if (!d->recording().append(d->simulation.time() - d->captureOrigin, d->simulation.preview, &error)) {
        d->capturing = false;
        log(QStringLiteral("录制停止：") + error);
    }
    updateReplayUi();
}
void MainWindow::endRecording() {
    if (auto *d = activeDocument(); d && d->capturing) {
        d->capturing = false;
        const int frames = d->recording().frameCount();
        log(QStringLiteral("录制完成：%1 帧，%2 s，平均帧间距 %3 ms；可保存为 .brec。")
                .arg(frames)
                .arg(d->recording().duration())
                .arg(frames > 1 ? d->recording().duration() / (frames - 1) * 1000. : 0., 0, 'f', 2));
    }
    updateReplayUi();
}
bool MainWindow::saveRecording(const QString &path) {
    auto *d = activeDocument();
    return d ? saveRecordingAt(d->activeRecording, path) : false;
}
bool MainWindow::saveRecordingAt(int index, const QString &path) {
    auto *d = activeDocument();
    if (!d || index < 0 || index >= int(d->recordings.size()))
        return false;
    if (d->capturing && index == d->activeRecording)
        endRecording();
    auto &target = *d->recordings[size_t(index)];
    if (!target.frameCount()) {
        log(QStringLiteral("没有可保存的录制帧。"));
        return false;
    }
    QString error;
    if (!target.save(path, &error)) {
        log(QStringLiteral("保存记录失败：") + error);
        return false;
    }
    log(QStringLiteral("记录已保存：") + path);
    updateReplayUi();
    return true;
}
bool MainWindow::exportRecordingAt(int index) {
    auto *d = activeDocument();
    if (!d || index < 0 || index >= int(d->recordings.size()) || !d->recordings[size_t(index)]->frameCount())
        return false;
    const auto &target = *d->recordings[size_t(index)];
    const QString suggested = target.sourcePath().isEmpty() ? QStringLiteral("simulation.brec") : target.sourcePath();
    auto p = QFileDialog::getSaveFileName(this, QStringLiteral("保存记录"), suggested, "BaguaDT recording (*.brec)");
    if (p.isEmpty())
        return false;
    if (!p.endsWith(".brec", Qt::CaseInsensitive))
        p += ".brec";
    return saveRecordingAt(index, p);
}
bool MainWindow::exportRecordingDialog() {
    auto *d = activeDocument();
    return d ? exportRecordingAt(d->activeRecording) : false;
}
bool MainWindow::selectRecordingAt(int index) {
    auto *d = activeDocument();
    if (!d || index < 0 || index >= int(d->recordings.size()) || index == d->activeRecording)
        return false;
    if (d->capturing) {
        log(QStringLiteral("录制中不能切换记录；请先停止录制。"));
        return false;
    }
    const bool replaying = d->replayActive;
    pauseReplay();
    d->activeRecording = index;
    d->replayTime = 0;
    if (replaying) {
        if (d->recording().frameCount())
            seekReplay(0);
        else
            stopReplay();
    }
    log(QStringLiteral("已选择记录 %1：%2 帧，%3 s。")
            .arg(index + 1)
            .arg(d->recording().frameCount())
            .arg(d->recording().duration(), 0, 'f', 3));
    updateReplayUi();
    return true;
}
void MainWindow::deleteRecordingAt(int index) {
    auto *d = activeDocument();
    if (!d || index < 0 || index >= int(d->recordings.size()))
        return;
    if (!d->recordings[size_t(index)]->frameCount()) {
        log(QStringLiteral("没有可删除的记录。"));
        return;
    }
    if (d->capturing && index == d->activeRecording) {
        log(QStringLiteral("录制中不能删除当前记录；请先停止录制。"));
        return;
    }
    if (d->recordings[size_t(index)]->dirty &&
        QMessageBox::question(this, QStringLiteral("删除录制"),
                              QStringLiteral("该记录尚未保存到磁盘，仍要删除？\n已经保存的 .brec 文件不受影响。")) !=
            QMessageBox::Yes)
        return;
    const bool wasActive = index == d->activeRecording;
    const bool wasReplay = d->replayActive;
    if (wasActive)
        pauseReplay();
    d->recordings.erase(d->recordings.begin() + index);
    if (d->recordings.empty())
        d->recordings.push_back(std::make_shared<Recording>());
    if (index < d->activeRecording)
        --d->activeRecording;
    d->activeRecording = std::clamp(d->activeRecording, 0, int(d->recordings.size()) - 1);
    d->replayTime = 0;
    // Stay in replay mode on whatever take is selected now; leave it if nothing is left.
    if (wasActive && wasReplay) {
        if (d->recording().frameCount())
            seekReplay(0);
        else
            stopReplay();
    }
    log(QStringLiteral("已删除记录 %1；当前场景保留 %2 条。").arg(index + 1).arg(recordingCount()));
    updateReplayUi();
}
void MainWindow::syncRecordingList() {
    if (!recordingList)
        return;
    auto *d = activeDocument();
    QVector<int> indices;
    QStringList labels;
    if (d)
        for (int i = 0; i < int(d->recordings.size()); ++i) {
            const auto &take = *d->recordings[size_t(i)];
            if (!take.frameCount())
                continue;
            QString state = take.sourcePath().isEmpty() ? QStringLiteral("未保存")
                                                        : QFileInfo(take.sourcePath()).fileName();
            if (i == d->activeRecording && d->capturing)
                state = QStringLiteral("录制中");
            indices.push_back(i);
            labels.push_back(QStringLiteral("记录 %1 · %2 帧 · %3 s · %4")
                                 .arg(i + 1)
                                 .arg(take.frameCount())
                                 .arg(take.duration(), 0, 'f', 3)
                                 .arg(state));
        }
    if (indices.isEmpty()) {
        indices.push_back(-1);
        labels.push_back(QStringLiteral("尚无记录；用「● 录制」或「▶● 模拟并录制」开始"));
    }
    bool sameShape = recordingList->count() == indices.size();
    for (int row = 0; sameShape && row < indices.size(); ++row) {
        auto *item = recordingList->item(row);
        sameShape = item && item->data(Qt::UserRole).toInt() == indices[row];
    }
    QSignalBlocker block(recordingList);
    if (!sameShape) {
        recordingList->clear();
        for (int row = 0; row < indices.size(); ++row) {
            auto *item = new QListWidgetItem(labels[row], recordingList);
            item->setData(Qt::UserRole, indices[row]);
            if (indices[row] < 0) {
                item->setFlags(Qt::NoItemFlags);
                continue;
            }
            const auto &take = *d->recordings[size_t(indices[row])];
            QString tip = take.sourcePath().isEmpty() ? QStringLiteral("尚未保存到磁盘")
                                                      : QDir::toNativeSeparators(take.sourcePath());
            if (!take.metadata.isEmpty())
                tip += "\n" + QString::fromUtf8(QJsonDocument(take.metadata).toJson(QJsonDocument::Compact));
            item->setToolTip(tip);
        }
    } else {
        for (int row = 0; row < labels.size(); ++row)
            recordingList->item(row)->setText(labels[row]);
    }
    recordingList->setEnabled(d && !d->capturing);
    for (int row = 0; row < recordingList->count(); ++row)
        if (recordingList->item(row)->data(Qt::UserRole).toInt() == (d ? d->activeRecording : -1))
            recordingList->setCurrentRow(row);
    if (deleteRecordingAction) {
        auto *item = recordingList->currentItem();
        deleteRecordingAction->setEnabled(d && !d->capturing && item && item->data(Qt::UserRole).toInt() >= 0);
    }
}
bool MainWindow::openRecording(const QString &path) {
    Recording staged;
    QString error;
    if (!staged.load(path, &error)) {
        log(QStringLiteral("载入回放失败：") + error);
        return false;
    }
    auto model = std::make_unique<Scene>();
    model->loadJson(staged.initial.json());
    addDocument(std::move(model), {}, QFileInfo(path).completeBaseName());
    auto *d = activeDocument();
    if (!d->recording().load(path, &error)) {
        log(error);
        return false;
    }
    seekReplay(0);
    log(QStringLiteral("已载入记录：%1 帧 / %2 s · %3")
            .arg(d->recording().frameCount())
            .arg(d->recording().duration())
            .arg(path));
    return true;
}
bool MainWindow::seekReplay(double time) {
    if (simulationActive()) {
        log(QStringLiteral("请先通过顶栏重置模拟，再开始回放；已有记录会保留。"));
        return false;
    }
    auto *d = activeDocument();
    if (!d || !d->recording().frameCount() || !std::isfinite(time))
        return false;
    QVector<Object> state;
    QString error;
    time = std::clamp(time, 0., d->recording().duration());
    if (!d->recording().sample(time, state, &error, replayInterpolate->isChecked())) {
        pauseReplay();
        log(QStringLiteral("读取记录帧失败：") + error);
        return false;
    }
    d->replayPreview.links=d->recording().initial.links;d->replayPreview.exclusions=d->recording().initial.exclusions;d->replayPreview.timeStep=d->recording().initial.timeStep;
    bool refreshTree = !d->replayActive;
    if(d->replayActive && state.size()!=d->replayPreview.objects.size())refreshTree=true;
    if (d->replayActive && state.size()==d->replayPreview.objects.size())
        for (qsizetype i = 0; i < state.size(); ++i)
            if (state[i].visible != d->replayPreview.objects[i].visible)
                refreshTree = true;
    if (!d->replayActive) {
        endRecording();
        d->replayPreview.objects = std::move(state);
        d->replayPreview.selected = scene->selected;
        d->replayActive = true;
        viewport->setScene(&d->replayPreview);
        viewport->editingEnabled = false;
    } else
        d->replayPreview.objects = std::move(state);
    d->replayTime = time;
    emit d->replayPreview.changed();
    if (refreshTree)
        syncTree();
    updateSimulationUi();
    return true;
}
void MainWindow::playReplay() {
    auto *d = activeDocument();
    if (!d || !d->recording().frameCount())
        return;
    if (!d->replayActive || d->replayTime >= d->recording().duration()) {
        if (!seekReplay(0))
            return;
    }
    d->replayPlaying = true;
    replayClock.restart();
    replayTimer.start();
    updateSimulationUi();
}
void MainWindow::pauseReplay() {
    if (auto *d = activeDocument())
        d->replayPlaying = false;
    replayTimer.stop();
    if (replayInfo)
        updateSimulationUi();
}
void MainWindow::stopReplay() {
    auto *d = activeDocument();
    if (!d)
        return;
    pauseReplay();
    d->replayActive = false;
    d->replayTime = 0;
    viewport->setScene(scene);
    viewport->editingEnabled = true;
    syncTree();
    updateSimulationUi();
}
void MainWindow::advanceReplay() {
    auto *d = activeDocument();
    if (!d || !d->replayPlaying)
        return;
    double next = d->replayTime + replayClock.nsecsElapsed() * 1e-9 * replaySpeed->currentData().toDouble();
    replayClock.restart();
    if (next >= d->recording().duration()) {
        if (replayLoop->isChecked() && d->recording().duration() > 0)
            next = std::fmod(next, d->recording().duration());
        else {
            next = d->recording().duration();
            pauseReplay();
        }
    }
    seekReplay(next);
}
void MainWindow::updateReplayUi() {
    if (!replayInfo || !recordAction)
        return;
    auto *d = activeDocument();
    bool has = d && d->recording().frameCount();
    bool playing = d && d->replayPlaying, replay = d && d->replayActive;
    recordAction->setEnabled(d && !replay && d->simulation.active());
    recordAction->setText(d && d->capturing ? QStringLiteral("■ 停止录制") : QStringLiteral("● 录制"));
    if (recordWithSimulationAction)
        recordWithSimulationAction->setEnabled(d && !replay && !d->capturing);
    if (recordOffline) {
        QSignalBlocker block(recordOffline);
        recordOffline->setEnabled(d && !replay);
        recordOffline->setChecked(d && d->offlineCapture);
    }
    exportRecordingAction->setEnabled(has);
    replayPlayAction->setEnabled(has && !simulationActive() && !playing);
    replayPauseAction->setEnabled(playing);
    replayStopAction->setEnabled(replay);
    timeline->setEnabled(has && !simulationActive());
    replayTimeField->setEnabled(has && !simulationActive());
    {
        QSignalBlocker b(timeline);
        timeline->setValue(has && d->recording().duration() > 0 ? int(d->replayTime / d->recording().duration() * 100000)
                                                              : 0);
    }
    {
        QSignalBlocker b(replayTimeField);
        replayTimeField->setRange(0, has ? d->recording().duration() : 0);
        replayTimeField->setValue(d ? d->replayTime : 0);
    }
    int frame =
        has ? std::max(0, int(std::upper_bound(d->recording().times.cbegin(), d->recording().times.cend(), d->replayTime) -
                              d->recording().times.cbegin()) -
                              1)
            : 0;
    replayInfo->setText(has ? QStringLiteral("记录 %1/%2 · 帧 %3 / %4 · %5 s%6")
                                  .arg(d->activeRecording + 1)
                                  .arg(d->recordings.size())
                                  .arg(frame + 1)
                                  .arg(d->recording().frameCount())
                                  .arg(d->recording().duration(), 0, 'f', 3)
                                  .arg(d->recording().dirty ? QStringLiteral(" · 未保存") : QString{})
                            : QStringLiteral("尚无记录"));
    if (has)
        replayInfo->setToolTip(QString::fromUtf8(QJsonDocument(d->recording().metadata).toJson(QJsonDocument::Indented)));
    for (const auto &widgetName : {"firstReplayFrame", "previousReplayFrame", "nextReplayFrame", "lastReplayFrame"})
        findChild<QToolButton *>(widgetName)->setEnabled(has && !simulationActive());
    syncRecordingList();
    updateTitle();
}
bool MainWindow::openProject(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 4 * 1024 * 1024) {
        log(QStringLiteral("无法打开项目：") + path);
        return false;
    }
    QJsonParseError pe;
    auto project = QJsonDocument::fromJson(f.readAll(), &pe).object();
    auto scenes = project.value("scenes").toArray();
    if (pe.error != QJsonParseError::NoError || project.value("format") != "baguadt.project" ||
        project.value("version").toInt() != 1 || scenes.isEmpty() || scenes.size() > 2000) {
        log(QStringLiteral("项目格式无效"));
        return false;
    }
    struct Pending {
        QString scenePath, recordingPath, name;
        QVector<QString> recordings;
        bool replay;
        double time;
    };
    QVector<Pending> pending;
    QDir base(QFileInfo(path).absolutePath());
    QString error;
    for (auto entry : scenes) {
        auto j = entry.toObject();
        Pending p;
        p.name = j.value("name").toString();
        p.replay = j.value("mode").toString(j.contains("recording") ? "replay" : "edit") == "replay";
        p.time = j.value("time").toDouble(0);
        if (!std::isfinite(p.time) || p.time < 0) {
            log(QStringLiteral("项目回放时间无效"));
            return false;
        }
        // Newer projects list every take; older ones carry a single "recording".
        QStringList takeKeys;
        if (j.contains("recordings")) {
            if(!j["recordings"].isArray()){log(QStringLiteral("recordings 必须为列表"));return false;}
            const auto list = j.value("recordings").toArray();
            if (list.size() > 256) {
                log(QStringLiteral("项目录制条数超限"));
                return false;
            }
            for (const auto &value : list)
                takeKeys.push_back(value.toString());
        } else if (j.contains("recording"))
            takeKeys.push_back(j.value("recording").toString());
        for (const auto &key : {QString("scene")}) {
            if (!j.contains(key))
                continue;
            QString relative = j.value(key).toString();
            auto resolved = QDir::cleanPath(base.absoluteFilePath(relative));
            if (relative.isEmpty() || QDir::isAbsolutePath(relative) ||
                !resolved.startsWith(base.absolutePath() + "/", Qt::CaseInsensitive)) {
                log(QStringLiteral("项目引用路径必须在项目目录内"));
                return false;
            }
            Scene check;
            if (!check.load(resolved, &error)) {
                log(error);
                return false;
            }
            p.scenePath = resolved;
        }
        for (const auto &relative : takeKeys) {
            auto resolved = QDir::cleanPath(base.absoluteFilePath(relative));
            if (relative.isEmpty() || QDir::isAbsolutePath(relative) ||
                !resolved.startsWith(base.absolutePath() + "/", Qt::CaseInsensitive)) {
                log(QStringLiteral("项目引用路径必须在项目目录内"));
                return false;
            }
            Recording check;
            if (!check.load(resolved, &error)) {
                log(error);
                return false;
            }
            p.recordings.push_back(resolved);
        }
        // The active take is the one named by "recording"; fall back to the first take.
        p.recordingPath.clear();
        if (j.contains("recording")) {
            const auto named = QDir::cleanPath(base.absoluteFilePath(j.value("recording").toString()));
            if (p.recordings.contains(named))
                p.recordingPath = named;
        }
        if (p.recordingPath.isEmpty() && !p.recordings.isEmpty())
            p.recordingPath = p.recordings.first();
        if (p.scenePath.isEmpty() && p.recordings.isEmpty()) {
            log(QStringLiteral("项目页签缺少场景或记录"));
            return false;
        }
        pending.push_back(p);
    }
    // Stage all documents and takes before changing the UI. Reopening a project creates
    // independent tabs; it must never replace recordings belonging to an existing document.
    std::vector<std::unique_ptr<Scene>> staged;
    std::vector<std::vector<std::shared_ptr<Recording>>> takes;
    for(const auto &p:pending){
        auto model=std::make_unique<Scene>();std::vector<std::shared_ptr<Recording>> recordings;
        for(const auto &takePath:p.recordings){auto take=std::make_shared<Recording>();if(!take->load(takePath,&error)){log(error);return false;}recordings.push_back(take);}
        if(!p.scenePath.isEmpty()){if(!model->load(p.scenePath,&error)){log(error);return false;}}
        else if(!model->loadJson(recordings.front()->initial.json(),&error)){log(error);return false;}
        if(p.replay && !recordings.empty()){int selected=0;for(int i=0;i<p.recordings.size();++i)if(p.recordings[i]==p.recordingPath)selected=i;QVector<Object> frame;if(!recordings[size_t(selected)]->sample(p.time,frame,&error)){log(error);return false;}}
        staged.push_back(std::move(model));takes.push_back(std::move(recordings));
    }
    for(int i=0;i<pending.size();++i){
        const auto &p=pending[i];addDocument(std::move(staged[size_t(i)]),{},p.name.isEmpty()?QStringLiteral("项目场景"):p.name);
        auto *doc=activeDocument();
        if(!takes[size_t(i)].empty()){doc->recordings=std::move(takes[size_t(i)]);doc->activeRecording=0;for(int k=0;k<p.recordings.size();++k)if(p.recordings[k]==p.recordingPath)doc->activeRecording=k;}
        if(p.replay && !p.recordings.isEmpty() && !seekReplay(p.time))return false;
    }
    updateReplayUi();
    projectPath = QFileInfo(path).absoluteFilePath();
    projectName = project.value("name").toString().trimmed();
    if (projectName.isEmpty())
        projectName = QFileInfo(path).completeBaseName();
    rememberProject(projectPath);
    updateTitle();
    log(QStringLiteral("项目已打开：") + projectName + QStringLiteral(" — ") + QDir::toNativeSeparators(projectPath));
    return true;
}
bool MainWindow::saveProject(const QString &filename, QString *error) {
    finishInspectorEdit();
    endRecording();
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        log(message);
        return false;
    };
    if (filename.trimmed().isEmpty())
        return fail(QStringLiteral("项目路径为空"));
    if (scene)
        scene->view = viewport->cameraJson();
    const QString path = withProjectSuffix(filename);
    const QFileInfo targetInfo(path);
    if (targetInfo.exists() && targetInfo.isDir())
        return fail(QStringLiteral("项目路径是文件夹：") + QDir::toNativeSeparators(path));
    QDir dir(targetInfo.absolutePath());
    if (!dir.exists() && !QDir().mkpath(dir.absolutePath()))
        return fail(QStringLiteral("无法创建项目目录：") + QDir::toNativeSeparators(dir.absolutePath()));
    QString folder = targetInfo.completeBaseName() + ".data-" + QUuid::createUuid().toString(QUuid::Id128);
    if (!dir.mkpath(folder))
        return fail(QStringLiteral("无法创建项目资源目录：") + QDir::toNativeSeparators(dir.filePath(folder)));
    QString manifestName = targetInfo.completeBaseName();
    const ProjectInfo existing = readProjectInfo(path);
    if (existing.valid && !existing.name.trimmed().isEmpty())
        manifestName = existing.name;
    else if (!projectPath.isEmpty() &&
             QFileInfo(projectPath).absoluteFilePath().compare(QFileInfo(path).absoluteFilePath(),
                                                               Qt::CaseInsensitive) == 0 &&
             !projectName.isEmpty())
        manifestName = projectName;
    QJsonArray entries;
    QString saveError;
    for (int i = 0; i < sceneTabs->count(); ++i) {
        auto *d = documentAt(i);
        d->capturing = false;
        QString relative = folder + QString("/scene_%1.bagua.json").arg(i);
        // Commit a complete new resource directory before publishing its project manifest.
        QSaveFile sf(dir.filePath(relative));
        auto bytes = QJsonDocument(d->model->json()).toJson();
        if (!sf.open(QIODevice::WriteOnly) || sf.write(bytes) != bytes.size() || !sf.commit())
            return fail(QStringLiteral("保存项目场景失败：%1（%2）")
                            .arg(QDir::toNativeSeparators(sf.fileName()), sf.errorString()));
        QJsonObject entry{{"name", d->title},
                          {"scene", relative},
                          {"mode", d->replayActive ? "replay" : "edit"},
                          {"time", d->replayTime}};
        QJsonArray takes;
        for (int k = 0; k < int(d->recordings.size()); ++k) {
            auto &take = *d->recordings[size_t(k)];
            if (!take.frameCount())
                continue;
            QString recording = folder + QString("/recording_%1_%2.brec").arg(i).arg(k);
            bool wasDirty = take.dirty;
            if (!take.save(dir.filePath(recording), &saveError))
                return fail(QStringLiteral("保存项目录制失败：%1（%2）")
                                .arg(QDir::toNativeSeparators(dir.filePath(recording)),
                                     saveError.isEmpty() ? QStringLiteral("未知错误") : saveError));
            take.dirty = wasDirty;
            takes.append(recording);
            if (k == d->activeRecording)
                entry.insert("recording", recording);
        }
        if (!takes.isEmpty())
            entry.insert("recordings", takes);
        entries.append(entry);
    }
    QSaveFile file(path);
    auto projectBytes = QJsonDocument(QJsonObject{{"format", "baguadt.project"},
                                          {"version", 1},
                                          {"name", manifestName},
                                          {"scenes", entries}})
                    .toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(projectBytes) != projectBytes.size() || !file.commit())
        return fail(QStringLiteral("保存项目清单失败：%1（%2）")
                        .arg(QDir::toNativeSeparators(path), file.errorString()));
    for (int i = 0; i < sceneTabs->count(); ++i) {
        auto *d = documentAt(i);
        d->filePath = dir.absoluteFilePath(folder + QString("/scene_%1.bagua.json").arg(i));
        d->model->undo.setClean();
        for (auto &take : d->recordings)
            take->dirty = false;
    }
    projectPath = QFileInfo(path).absoluteFilePath();
    projectName = manifestName;
    rememberProject(projectPath);
    updateReplayUi();
    updateTitle();
    log(QStringLiteral("项目已保存：") + QDir::toNativeSeparators(projectPath));
    return true;
}
} // namespace bagua