#pragma once
#include "Hsc.h"
#include "Scene.h"
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QVector>
#include <atomic>
#include <memory>

namespace bagua {
class Physics;
// A shallow scene copy handed to the worker. Meshes are immutable
// shared_ptr payloads, so this copy is small even for large vehicles.
struct SceneSetup {
    QVector<Object> objects;
    QVector<Link> links;
    QVector<QPair<QString, QString>> exclusions;
    double timeStep = 1.0 / 240.0;
    QJsonObject simulation;
    QString selected;
};

// A snapshot produced on the simulation worker. Objects and links are copied
// value data; imported/deforming meshes use immutable shared_ptr payloads, so
// the UI can keep drawing a snapshot while the worker advances the next one.
struct SimulationFrame {
    QVector<Object> objects;
    QVector<Link> links;
    QVector<QPair<QString, QString>> exclusions;
    QString selected;
    double timeStep = 1.0 / 240.0;
    double time = 0;
    int contacts = 0;
    bool hscActive = false;
    HscStats hscStats;
    QVector<LinkStat> linkStats;
    QString solverDescription;
    double stepSize = 1.0 / 240.0;
};

// Owns the Physics object and runs the solver loop away from the GUI thread.
class SimulationWorker : public QObject {
    Q_OBJECT
public:
    explicit SimulationWorker();
    ~SimulationWorker() override;
    // All of these are invoked from the worker's own event loop.
    void start(const SceneSetup &source, const HscOptions &options, quint64 generation, bool autorun);
    void pause();
    void resume();
    void step(int count, quint64 generation);
    void reset();
    void shutdown();
    void setLockStep(bool on);
    quintptr workerThreadId() const { return nativeThreadId.load(); }

signals:
    void started(quint64 generation, bool ok, const QString &error, const bagua::SimulationFrame &frame);
    void frameReady(quint64 generation, const bagua::SimulationFrame &frame, bool stepFrame);
    void stepFinished(quint64 generation, bool ok, const QString &error);
    void failed(quint64 generation, const QString &error);

private slots:
    void advance();

private:
    SimulationFrame makeFrame() const;
    void emitFrame(bool stepFrame);
    std::unique_ptr<Physics> physics;
    QTimer *timer = nullptr;
    QElapsedTimer clock;
    double accumulator = 0;
    quint64 generation = 0;
    bool running = false;
    bool lockStep = false;
    QElapsedTimer frameClock;
    std::atomic<quintptr> nativeThreadId{0};
};
} // namespace bagua

Q_DECLARE_METATYPE(bagua::SimulationFrame)

namespace bagua {
// GUI-side facade. It owns the worker thread and a normal Scene snapshot that
// the viewport and the rest of the editor can read without synchronization.
class SimulationSession : public QObject {
    Q_OBJECT
public:
    explicit SimulationSession(QObject *parent = nullptr);
    ~SimulationSession() override;

    Scene preview;
    bool start(const Scene &source, QString *error = nullptr, HscOptions options = {}, bool autorun = true);
    bool step(int count = 1, QString *error = nullptr);
    void pause();
    void resume();
    void reset();
    void setLockStep(bool on);

    bool active() const { return runtimeActive; }
    bool running() const { return isRunning; }
    double time() const { return lastTime; }
    int contacts() const { return lastContacts; }
    bool hscActive() const { return lastHscActive; }
    HscStats hscStats() const { return lastHscStats; }
    QVector<LinkStat> linkStats() const { return lastLinkStats; }
    QString solverDescription() const { return lastDescription; }
    double stepSize() const { return lastStepSize; }
    // Native id of the thread that owns Physics; tests use it to prove the
    // solver never runs on the GUI thread.
    quintptr workerThreadId() const { return worker ? worker->workerThreadId() : 0; }

signals:
    void updated();
    void failed(const QString &error);
    void startFinished(bool ok, const QString &error);
    void stepFinished(bool ok, const QString &error);

private:
    void applyFrame(const SimulationFrame &frame);
    void handleStarted(quint64 generation, bool ok, const QString &error, const SimulationFrame &frame);
    void handleFrame(quint64 generation, const SimulationFrame &frame, bool stepFrame);
    void handleStepFinished(quint64 generation, bool ok, const QString &error);
    void handleFailed(quint64 generation, const QString &error);
    void cancelWaiters();

    QThread thread;
    SimulationWorker *worker = nullptr;
    quint64 generation = 0;
    bool runtimeActive = false;
    bool isRunning = false;
    bool starting = false;
    bool stepping = false;
    bool startWaiting = false, stepWaiting = false;
    bool startOk = false, stepOk = false;
    bool startAutorun = true;
    QString startError, stepError;
    double lastTime = 0;
    int lastContacts = 0;
    bool lastHscActive = false;
    HscStats lastHscStats;
    QVector<LinkStat> lastLinkStats;
    QString lastDescription;
    double lastStepSize = 1.0 / 240.0;
};
} // namespace bagua
