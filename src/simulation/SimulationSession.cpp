#include "SimulationSession.h"
#include "Physics.h"
#include <QEventLoop>
#include <QMetaObject>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <exception>

namespace bagua {
SimulationWorker::SimulationWorker() {
    timer = new QTimer(this);
    timer->setTimerType(Qt::PreciseTimer);
    timer->setInterval(4);
    connect(timer, &QTimer::timeout, this, &SimulationWorker::advance);
}
SimulationWorker::~SimulationWorker() {
    if (timer)
        timer->stop();
}
void SimulationWorker::start(const SceneSetup &source, const HscOptions &options, quint64 gen, bool autorun) {
    if (timer)
        timer->stop();
    running = false;
    accumulator = 0;
    generation = gen;
    nativeThreadId.store(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    physics.reset();
    try {
        Scene scene;
        scene.objects = source.objects;
        scene.links = source.links;
        scene.exclusions = source.exclusions;
        scene.timeStep = source.timeStep;
        scene.simulation = source.simulation;
        scene.selected = source.selected;
        QString error;
        auto next = std::make_unique<Physics>();
        if (!next->start(scene, &error, options)) {
            emit started(gen, false, error, SimulationFrame{});
            return;
        }
        physics = std::move(next);
        running = autorun;
        clock.restart();
        frameClock.restart();
        if (autorun && timer)
            timer->start();
        emit started(gen, true, {}, makeFrame());
    } catch (const std::exception &e) {
        emit started(gen, false, QString::fromUtf8(e.what()), SimulationFrame{});
    } catch (...) {
        emit started(gen, false, QStringLiteral("模拟启动失败"), SimulationFrame{});
    }
}
void SimulationWorker::pause() {
    running = false;
    if (timer)
        timer->stop();
}
void SimulationWorker::resume() {
    if (!physics || running)
        return;
    running = true;
    accumulator = 0;
    clock.restart();
    frameClock.restart();
    if (timer)
        timer->start();
}
void SimulationWorker::step(int count, quint64 gen) {
    if (timer)
        timer->stop();
    running = false;
    if (!physics) {
        emit stepFinished(gen, false, QStringLiteral("仿真尚未启动"));
        return;
    }
    QString error;
    if (!physics->step(count, &error)) {
        emit failed(gen, error);
        emit stepFinished(gen, false, error);
        return;
    }
    emitFrame(true);
    emit stepFinished(gen, true, {});
}
void SimulationWorker::reset() {
    running = false;
    if (timer)
        timer->stop();
    physics.reset();
    accumulator = 0;
}
void SimulationWorker::shutdown() { reset(); }
void SimulationWorker::setLockStep(bool on) {
    lockStep = on;
    accumulator = 0;
    clock.restart();
}
SimulationFrame SimulationWorker::makeFrame() const {
    SimulationFrame frame;
    if (!physics)
        return frame;
    frame.objects = physics->preview.objects;
    frame.links = physics->preview.links;
    frame.exclusions = physics->preview.exclusions;
    frame.selected = physics->preview.selected;
    frame.timeStep = physics->preview.timeStep;
    frame.time = physics->time();
    frame.contacts = physics->contacts();
    frame.hscActive = physics->hscActive();
    frame.hscStats = physics->hscStats();
    frame.linkStats = physics->linkStats();
    frame.solverDescription = physics->solverDescription();
    frame.stepSize = physics->stepSize();
    return frame;
}
void SimulationWorker::emitFrame(bool stepFrame) {
    emit frameReady(generation, makeFrame(), stepFrame);
    frameClock.restart();
}
void SimulationWorker::advance() {
    if (!running || !physics)
        return;
    const double dt = std::max(1e-6, physics->stepSize());
    int count = 0;
    if (lockStep) {
        // High quality capture: exactly one fixed step per timer event.
        count = 1;
    } else {
        accumulator += std::min(.1, double(clock.restart()) * 1e-3);
        count = int(accumulator / dt);
        if (count <= 0)
            return;
        count = std::min(count, 240);
        accumulator -= count * dt;
    }
    QString error;
    if (!physics->step(count, &error)) {
        running = false;
        if (timer)
            timer->stop();
        emit failed(generation, error);
        return;
    }
    // Normal playback repaints at roughly 60 Hz; recording still receives each
    // emitted frame, while the solver can run at its native fixed step.
    if (lockStep || !frameClock.isValid() || frameClock.elapsed() >= 16)
        emitFrame(false);
}

SimulationSession::SimulationSession(QObject *parent) : QObject(parent) {
    qRegisterMetaType<SimulationFrame>("bagua::SimulationFrame");
    worker = new SimulationWorker;
    worker->moveToThread(&thread);
    thread.setObjectName(QStringLiteral("BaguaSimulationWorker"));
    connect(worker, &SimulationWorker::started, this, &SimulationSession::handleStarted);
    connect(worker, &SimulationWorker::frameReady, this, &SimulationSession::handleFrame);
    connect(worker, &SimulationWorker::stepFinished, this, &SimulationSession::handleStepFinished);
    connect(worker, &SimulationWorker::failed, this, &SimulationSession::handleFailed);
    thread.start();
}
SimulationSession::~SimulationSession() {
    cancelWaiters();
    if (worker && thread.isRunning()) {
        QMetaObject::invokeMethod(worker, [worker = worker] { worker->shutdown(); }, Qt::BlockingQueuedConnection);
        thread.quit();
        thread.wait();
    }
    delete worker;
    worker = nullptr;
}
void SimulationSession::cancelWaiters() {
    if (startWaiting) {
        startWaiting = false;
        startOk = false;
        startError = QStringLiteral("已取消");
        emit startFinished(false, startError);
    }
    if (stepWaiting) {
        stepWaiting = false;
        stepOk = false;
        stepError = QStringLiteral("已取消");
        emit stepFinished(false, stepError);
    }
    starting = false;
    stepping = false;
}
bool SimulationSession::start(const Scene &source, QString *error, HscOptions options, bool autorun) {
    if (runtimeActive) {
        if (autorun)
            resume();
        else
            pause();
        return true;
    }
    if (starting || stepping) {
        if (error)
            *error = QStringLiteral("模拟正在启动或步进");
        return false;
    }
    // Copy only value data here; JSON serialization and backend construction
    // stay in the worker, so a large vehicle asset never blocks the GUI thread.
    SceneSetup setup;
    setup.objects = source.objects;
    setup.links = source.links;
    setup.exclusions = source.exclusions;
    setup.timeStep = source.timeStep;
    setup.simulation = source.simulation;
    setup.selected = source.selected;
    generation++;
    const quint64 gen = generation;
    starting = true;
    runtimeActive = false;
    isRunning = false;
    startWaiting = true;
    startOk = false;
    startError.clear();
    const bool autorunFlag = autorun;
    startAutorun = autorun;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(300000);
    bool timedOut = false;
    connect(&timeout, &QTimer::timeout, &loop, [&] {
        timedOut = true;
        loop.quit();
    });
    connect(this, &SimulationSession::startFinished, &loop, [&] { loop.quit(); });
    timeout.start();
    QMetaObject::invokeMethod(
        worker,
        [worker = worker, setup, options, gen, autorunFlag] { worker->start(setup, options, gen, autorunFlag); },
        Qt::QueuedConnection);
    loop.exec();
    startWaiting = false;
    starting = false;
    if (timedOut) {
        startOk = false;
        startError = QStringLiteral("模拟启动超时");
        generation++;
        QMetaObject::invokeMethod(worker, [worker = worker] { worker->reset(); }, Qt::QueuedConnection);
    }
    if (error)
        *error = startError;
    return startOk;
}
bool SimulationSession::step(int count, QString *error) {
    if (!runtimeActive) {
        if (error)
            *error = QStringLiteral("仿真尚未启动");
        return false;
    }
    if (isRunning) {
        if (error)
            *error = QStringLiteral("请先暂停仿真再单步");
        return false;
    }
    if (stepping) {
        if (error)
            *error = QStringLiteral("上一步仍在计算");
        return false;
    }
    stepping = true;
    stepWaiting = true;
    stepOk = false;
    stepError.clear();
    const quint64 gen = generation;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(300000);
    bool timedOut = false;
    connect(&timeout, &QTimer::timeout, &loop, [&] {
        timedOut = true;
        loop.quit();
    });
    connect(this, &SimulationSession::stepFinished, &loop, [&] { loop.quit(); });
    timeout.start();
    QMetaObject::invokeMethod(worker, [worker = worker, count, gen] { worker->step(count, gen); },
                              Qt::QueuedConnection);
    loop.exec();
    stepWaiting = false;
    stepping = false;
    if (timedOut) {
        stepOk = false;
        stepError = QStringLiteral("单步计算超时");
    }
    if (error)
        *error = stepError;
    return stepOk;
}
void SimulationSession::pause() {
    isRunning = false;
    QMetaObject::invokeMethod(worker, [worker = worker] { worker->pause(); }, Qt::QueuedConnection);
}
void SimulationSession::resume() {
    if (!runtimeActive)
        return;
    isRunning = true;
    QMetaObject::invokeMethod(worker, [worker = worker] { worker->resume(); }, Qt::QueuedConnection);
}
void SimulationSession::reset() {
    generation++;
    runtimeActive = false;
    isRunning = false;
    starting = false;
    stepping = false;
    cancelWaiters();
    preview.objects.clear();
    preview.links.clear();
    preview.exclusions.clear();
    preview.selected.clear();
    lastTime = 0;
    lastContacts = 0;
    lastHscActive = false;
    lastHscStats = {};
    lastLinkStats.clear();
    lastDescription = QStringLiteral("模拟未开始");
    lastStepSize = 1.0 / 240.0;
    preview.notifyChanged();
    QMetaObject::invokeMethod(worker, [worker = worker] { worker->reset(); }, Qt::QueuedConnection);
}
void SimulationSession::setLockStep(bool on) {
    QMetaObject::invokeMethod(worker, [worker = worker, on] { worker->setLockStep(on); }, Qt::QueuedConnection);
}
void SimulationSession::applyFrame(const SimulationFrame &frame) {
    preview.objects = frame.objects;
    preview.links = frame.links;
    preview.exclusions = frame.exclusions;
    // Selection is owned by the editor, not by the solver loop. Adopt the
    // worker's initial selection only when the GUI snapshot has none; later
    // user selections must survive the next frame.
    if (!frame.selected.isEmpty() && !preview.find(preview.selected))
        preview.selected = frame.selected;
    preview.timeStep = frame.timeStep;
    if (!preview.find(preview.selected))
        preview.selected.clear();
    preview.notifyChanged();
    lastTime = frame.time;
    lastContacts = frame.contacts;
    lastHscActive = frame.hscActive;
    lastHscStats = frame.hscStats;
    lastLinkStats = frame.linkStats;
    lastDescription = frame.solverDescription;
    lastStepSize = frame.stepSize > 0 ? frame.stepSize : 1.0 / 240.0;
}
void SimulationSession::handleStarted(quint64 gen, bool ok, const QString &error, const SimulationFrame &frame) {
    if (gen != generation)
        return;
    starting = false;
    if (ok) {
        runtimeActive = true;
        isRunning = startAutorun;
        applyFrame(frame);
        emit updated();
    } else {
        runtimeActive = false;
        isRunning = false;
        startError = error;
    }
    startOk = ok;
    if (!ok)
        startError = error;
    startWaiting = false;
    emit startFinished(ok, error);
}
void SimulationSession::handleFrame(quint64 gen, const SimulationFrame &frame, bool stepFrame) {
    if (gen != generation)
        return;
    if (!stepFrame && !isRunning)
        return;
    if (!stepFrame && frame.time < lastTime)
        return;
    applyFrame(frame);
    emit updated();
}
void SimulationSession::handleStepFinished(quint64 gen, bool ok, const QString &error) {
    if (gen != generation)
        return;
    if (!stepWaiting && !stepping)
        return;
    stepping = false;
    stepWaiting = false;
    stepOk = ok;
    stepError = error;
    emit stepFinished(ok, error);
}
void SimulationSession::handleFailed(quint64 gen, const QString &error) {
    if (gen != generation)
        return;
    isRunning = false;
    if (stepWaiting) {
        stepOk = false;
        stepError = error;
        stepWaiting = false;
        stepping = false;
        emit stepFinished(false, error);
    }
    if (startWaiting) {
        startOk = false;
        startError = error;
        startWaiting = false;
        starting = false;
        emit startFinished(false, error);
    }
    emit failed(error);
}
} // namespace bagua
