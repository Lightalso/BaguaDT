#include "MainWindow.h"
#include "Backends.h"
#include <QMenuBar>
#include <QStatusBar>
#include <QToolButton>

namespace bagua {
void MainWindow::validate() {
    if (!scene)
        return;
    auto r = validateBackends(*scene);
    log(r.cuda);
    if(!physicsOptions().enabled)log(QStringLiteral("Chrono 自检：") + r.chrono);
    else log(QStringLiteral("当前选择 HSC 原生物理链（ABD / FEM / IPC / CCD）。开始模拟时检查原生场景有效性。"));
    if (simulationActive())
        log(activeDocument()->simulation.solverDescription());
    log(activeDocument() && activeDocument()->replayActive
            ? QStringLiteral("当前为保存结果回放：请使用底部记录栏播放、暂停和逐帧控制；顶栏仅控制物理模拟。")
            : QStringLiteral("F5 启动%1物理模拟；F6 暂停；F7 单步；F8 重置。").arg(physicsOptions().enabled?QStringLiteral(" HSC "):QStringLiteral(" Chrono ")));
}
bool MainWindow::simulationActive() const {
    auto *d = activeDocument();
    return d && d->simulation.active();
}
bool MainWindow::simulationRunning() const {
    auto *d = activeDocument();
    return d && d->simulation.running();
}
double MainWindow::simulationTime() const {
    auto *d = activeDocument();
    return d ? d->simulation.time() : 0;
}
const Scene *MainWindow::simulationPreview() const {
    auto *d = activeDocument();
    return d && d->simulation.active() ? &d->simulation.preview : nullptr;
}
bool MainWindow::startSimulation(bool autorun) {
    if (simulationStarting || simulationStepWaiting)
        return false;
    if (auto *d = activeDocument(); d && d->replayActive) {
        log(QStringLiteral("请先通过底部退出回放，再开始物理模拟。"));
        return false;
    }
    finishInspectorEdit();
    viewport->cancelDrag();
    auto *d = activeDocument();
    if (!d)
        return false;
    if (!d->simulation.active()) {
        QString error;
        simulationStarting = true;
        viewport->editingEnabled = false;
        // The session starts the solver on its worker thread and waits for the
        // backend handshake. The viewport keeps processing events while the
        // expensive HSC/Chrono world is built.
        const bool started = d->simulation.start(*scene, &error, solverOptions, autorun);
        simulationStarting = false;
        if (!started) {
            viewport->editingEnabled = true;
            log(QStringLiteral("无法启动仿真：") + error);
            statusBar()->showMessage(QStringLiteral("仿真启动失败：") + error);
            updateSimulationUi();
            return false;
        }
        viewport->setScene(&d->simulation.preview);
        viewport->editingEnabled = false;
        log(d->simulation.solverDescription() +
            QStringLiteral("；%1 个对象，重力 -9.81 m/s²，步长 %2 s（仿真工作线程）。")
                .arg(scene->objects.size())
                .arg(d->simulation.stepSize(), 0, 'g', 6));
    } else if (autorun) {
        d->simulation.resume();
    }
    updateSimulationUi();
    return true;
}
void MainWindow::pauseSimulation() {
    if (auto *d = activeDocument(); d && d->replayActive)
        return;
    if (auto *d = activeDocument())
        d->simulation.pause();
    updateSimulationUi();
}
void MainWindow::resetSimulation() {
    if (simulationStarting || simulationStepWaiting)
        return;
    auto *d = activeDocument();
    if (!d || d->replayActive)
        return;
    endRecording();
    // Detach the viewport before clearing the GUI-side simulation snapshot.
    viewport->setScene(scene);
    viewport->editingEnabled = true;
    d->simulation.reset();
    updateSimulationUi();
}
bool MainWindow::stepSimulation() {
    if (replayActive() || simulationStarting || simulationStepWaiting)
        return false;
    auto *d = activeDocument();
    if (!d)
        return false;
    if (!d->simulation.active() && !startSimulation(false))
        return false;
    pauseSimulation();
    QString error;
    // The solver runs on the worker; this call waits in a local event loop so
    // the requested one-step result is available while the UI keeps painting.
    simulationStepWaiting = true;
    const bool ok = d->simulation.step(1, &error);
    simulationStepWaiting = false;
    if (!ok && !error.isEmpty())
        log(QStringLiteral("单步失败：") + error);
    updateSimulationUi();
    return ok;
}
void MainWindow::updateSimulationStatus() {
    auto *d = activeDocument();
    if (!d)
        return;
    const auto &simulation = d->simulation;
    if (!simulation.active()) {
        physicsStatus->setText(QStringLiteral(" 模拟未开始 "));
        return;
    }
    physicsStatus->setText(
        QStringLiteral(" %1 s · %2 接触 ").arg(simulation.time(), 0, 'f', 3).arg(simulation.contacts()));
    if (simulation.hscActive()) {
        const auto stats = simulation.hscStats();
        physicsStatus->setText(QStringLiteral(" %1 s · HSC 秩%2 · r=%3 ")
                                   .arg(simulation.time(), 0, 'f', 3)
                                   .arg(stats.rank)
                                   .arg(stats.relativeResidual, 0, 'g', 2));
    }
    physicsStatus->setToolTip(simulation.solverDescription());
}
void MainWindow::onSimulationFrame() {
    auto *d = activeDocument();
    if (!d)
        return;
    updateSimulationStatus();
    updateLinkDiagnostics();
    if (d->capturing)
        captureFrame();
    viewport->update();
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
    if (simulationActive()) {
        updateSimulationStatus();
    } else {
        physicsStatus->setText(QStringLiteral(" 模拟未开始 "));
        physicsStatus->setToolTip(QStringLiteral("模拟未开始"));
    }
    statusBar()->showMessage(active ? (replay ? QStringLiteral("记录回放 · 当前显示保存的求解结果；可拖动时间轴")
                                              : simulationActive()
                                                    ? (activeDocument()->simulation.hscActive()
                                                           ? QStringLiteral("原生 HSC 仿真预览 · 工作线程求解；F8 重置后可编辑")
                                                           : QStringLiteral("Chrono 仿真预览 · 工作线程求解；F8 重置后可编辑"))
                                                    : QStringLiteral("Chrono 仿真预览 · F8 重置后可编辑"))
                                    : QStringLiteral("就绪 · 场景编辑器"));
    syncSelection();
    syncInspector();
    viewport->update();
    updateReplayUi();
}
} // namespace bagua
