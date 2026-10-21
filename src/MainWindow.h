#pragma once
#include "Assets.h"
#include "Physics.h"
#include "Recording.h"
#include "Scene.h"
#include "Viewport.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabBar>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QUndoGroup>
#include <array>
#include <memory>
#include <vector>
namespace bagua {
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    Scene *scene = nullptr;
    Viewport *viewport = nullptr;
    void newScene(bool example = false);
    bool closeScene(int index);
    bool openPath(const QString &path);
    bool savePath(const QString &path);
    void validate();
    void log(const QString &text);
    bool startSimulation();
    void pauseSimulation();
    void resetSimulation();
    bool stepSimulation();
    bool simulationActive() const;
    bool simulationRunning() const;
    double simulationTime() const;
    const Scene *simulationPreview() const;
    bool replayActive() const;
    bool replayRunning() const;
    double replayTime() const;
    const Scene *replayPreview() const;
    void setPhysicsOptions(HscOptions options);
    HscOptions physicsOptions() const { return solverOptions; }
    bool importAsset(const QString &path, ImportOptions options = {});
    void importHscVehicle();
    bool openRecording(const QString &path);
    bool saveRecording(const QString &path);
    bool saveRecordingAt(int index, const QString &path);
    int recordingCount() const;
    bool selectRecordingAt(int index);
    void deleteRecordingAt(int index);
    bool openProject(const QString &path);
    bool saveProject(const QString &path);
    bool beginRecording();
    void endRecording();
    void setOfflineCapture(bool on);
    bool seekReplay(double time);
    void playReplay();
    void pauseReplay();
    void stopReplay();
    int recordedFrames() const;

protected:
    void closeEvent(QCloseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;

private:
    QTreeWidget *tree = nullptr;
    QWidget *inspector = nullptr;
    QLineEdit *name = nullptr;
    QCheckBox *visible = nullptr;
    QCheckBox *collision = nullptr, *fixedBody = nullptr;
    QDoubleSpinBox *mass = nullptr;
    QCheckBox *femEnabled = nullptr, *femBottom = nullptr;
    QDoubleSpinBox *femYoung = nullptr, *femPoisson = nullptr, *femStiffness = nullptr;
    QSpinBox *femResolution = nullptr;
    QComboBox *femAttachment = nullptr;
    std::array<QDoubleSpinBox *,6> velocityFields{};
    QWidget *makeVelocityInspector();
    void syncVelocityInspector();
    QWidget *makeFemInspector();
    void syncFemInspector();
    QLabel *physicsStatus = nullptr;
    QAction *playAction = nullptr, *pauseAction = nullptr, *resetAction = nullptr, *stepAction = nullptr;
    QTimer physicsTimer;
    QElapsedTimer physicsClock;
    double physicsAccumulator = 0;
    QPushButton *color = nullptr;
    QLabel *typeLabel = nullptr, *countLabel = nullptr;
    QPlainTextEdit *console = nullptr;
    QTreeWidget *assetTree = nullptr;
    QSlider *timeline = nullptr;
    QDoubleSpinBox *replayTimeField = nullptr;
    QComboBox *replaySpeed = nullptr;
    QCheckBox *replayLoop = nullptr, *replayInterpolate = nullptr;
    QLabel *replayInfo = nullptr;
    QAction *recordAction = nullptr, *replayPlayAction = nullptr, *replayPauseAction = nullptr,
            *replayStopAction = nullptr, *exportRecordingAction = nullptr;
    QAction *recordWithSimulationAction = nullptr;
    QCheckBox *recordOffline = nullptr;
    QListWidget *recordingList = nullptr;
    QAction *deleteRecordingAction = nullptr;
    // Assembly (links / anchors / contact exclusions) editor.
    QWidget *assemblyPanel = nullptr;
    QTreeWidget *linkTree = nullptr;
    QComboBox *linkType = nullptr, *linkBodyA = nullptr, *linkBodyB = nullptr;
    QPushButton *linkAdd = nullptr, *linkUpdate = nullptr, *linkRemove = nullptr;
    std::array<QDoubleSpinBox *, 3> linkAnchorA{}, linkAnchorB{}, linkAxis{};
    QPushButton *linkPickA = nullptr, *linkPickB = nullptr;
    QDoubleSpinBox *linkStrength = nullptr, *rotationAngle=nullptr, *rotationSpeed=nullptr;
    std::array<QDoubleSpinBox *,3> anchorEdit{};
    QCheckBox *limitEnabled = nullptr;
    QDoubleSpinBox *limitLower = nullptr, *limitUpper = nullptr, *limitStrength = nullptr;
    QTableWidget *stitchPairs = nullptr;
    QCheckBox *stitchAnchorMode = nullptr;
    QDoubleSpinBox *stitchStiffness = nullptr, *stitchRest = nullptr;
    QPushButton *pairsAdd = nullptr, *pairsRemove = nullptr, *pairsAuto = nullptr;
    QListWidget *anchorList = nullptr;
    QPushButton *anchorAdd = nullptr, *anchorRemove = nullptr, *anchorPick = nullptr;
    QComboBox *exclusionA = nullptr, *exclusionB = nullptr;
    QPushButton *exclusionAdd = nullptr, *exclusionRemove = nullptr;
    QListWidget *exclusionList = nullptr;
    QLabel *linkDiagnostics = nullptr;
    int pickTarget = 0; // 1 = anchor A, 2 = anchor B, 3 = a new anchor on the clicked body
    bool assemblySyncing = false;
    QJsonObject loadedLink;
    QCheckBox *driveEnabled=nullptr,*damperEnabled=nullptr;
    QDoubleSpinBox *driveSpeed=nullptr,*driveRamp=nullptr,*driveStrength=nullptr,*damperCoefficient=nullptr;
    QWidget *driveRow=nullptr,*damperRow=nullptr;
    QWidget *linkAnchorRow=nullptr,*linkLimitRow=nullptr,*linkRotationRow=nullptr,*linkStitchRow=nullptr;
    void setupAssemblyDock();
    void syncAssemblyPanel();
    void syncLinkForm(const Link &link);
    bool readLinkForm(Link &link, QString *error) const;
    bool commitLinks(const QVector<Link> &links, const QVector<QPair<QString, QString>> &exclusions,
                     const QString &label);
    int selectedLinkIndex() const;
    void deleteSelectedLink();
    void updateLinkDiagnostics();
    void handleViewportPick(const QVector3D &world, const QString &objectId);
    QTimer replayTimer;
    QElapsedTimer replayClock;
    std::array<QDoubleSpinBox *, 12> fields{};
    std::array<QAction *, 3> toolActions{};
    QAction *deleteAction = nullptr, *duplicateAction = nullptr, *gridAction = nullptr;
    struct Document {
        int id = 0;
        std::unique_ptr<Scene> model;
        QString filePath, title;
        Tool tool = Tool::Move;
        Physics physics;
        bool running = false;
        // Every take is kept: each one can be replayed, saved or deleted on its own.
        std::vector<std::shared_ptr<Recording>> recordings;
        int activeRecording = 0;
        Recording &recording() { return *recordings[size_t(activeRecording)]; }
        Scene replayPreview;
        bool capturing = false, replayActive = false, replayPlaying = false;
        // Lock-step recording: one fixed physics step per captured frame instead of chasing
        // wall-clock time, so playback is evenly sampled even when a step is slow.
        bool offlineCapture = false;
        double replayTime = 0;
        double captureOrigin = 0;
    };
    std::vector<std::unique_ptr<Document>> documents;
    QTabBar *sceneTabs = nullptr;
    QUndoGroup *undoGroup = nullptr;
    int nextDocumentId = 1, nextUntitled = 1;
    bool syncing = false;
    HscOptions solverOptions;
    void loadPhysicsSettings();
    void showPhysicsSettings();
    bool editingLocked() const;
    void setupMenus();
    void setupReplayDock();
    void updateReplayUi();
    void syncRecordingList();
    bool exportRecordingAt(int index);
    void advanceReplay();
    void captureFrame();
    bool exportRecordingDialog();
    void syncAssets();
    Scene *displayScene() const;
    void updateSimulationUi();
    void advanceSimulation();
    void setupSceneTabs();
    void addDocument(std::unique_ptr<Scene> model, const QString &path, const QString &title);
    void activateDocument(int index);
    Document *documentAt(int index) const;
    Document *activeDocument() const;
    int indexOf(Document *document) const;
    void updateTabTitles();
    void setupDocks();
    void syncTree();
    void syncInspector();
    void syncSelection();
    void updateTitle();
    void commitFields();
    void finishInspectorEdit();
    bool confirmDiscard();
    bool save(bool as = false);
};
} // namespace bagua
