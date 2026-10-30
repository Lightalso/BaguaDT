#pragma once
#include "Assets.h"
#include "EditorControls.h"
#include "Physics.h"
#include "Recording.h"
#include "Scene.h"
#include "SimulationSession.h"
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
#include <QStringList>
#include <QTabBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUndoGroup>
#include <array>
#include <memory>
#include <vector>
namespace bagua {
struct VehicleAssemblyRequest;
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr, bool initialScene = true);
    Scene *scene = nullptr;
    Viewport *viewport = nullptr;
    void newScene(bool example = false);
    // Startup launcher: opens the project manager before any scene is created.
    // Returns false when the user closes the manager without opening a project.
    bool showStartupProjectManager();
    bool closeScene(int index);
    bool openPath(const QString &path);
    bool savePath(const QString &path);
    void validate();
    void log(const QString &text);
    bool startSimulation(bool autorun = true);
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
    void importMeshDialog();
    bool importComponent(const QString &path);
    bool exportComponentsTo(const QString &directory, bool onlySelected);
    void exportComponents(bool onlySelected);
    void showVehicleAssemblyWizard();
    bool applyVehicleAssembly(const VehicleAssemblyRequest &request);
    void importHscVehicle();
    bool openRecording(const QString &path);
    bool saveRecording(const QString &path);
    bool saveRecordingAt(int index, const QString &path);
    int recordingCount() const;
    bool selectRecordingAt(int index);
    void deleteRecordingAt(int index);
    bool openProject(const QString &path);
    bool saveProject(const QString &path, QString *error = nullptr);
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
    QLineEdit *sceneFilter = nullptr;
    QWidget *inspector = nullptr;
    QLineEdit *name = nullptr;
    QCheckBox *visible = nullptr;
    QCheckBox *collision = nullptr, *fixedBody = nullptr;
    DragSpinBox *mass = nullptr;
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
    void updateSimulationStatus();
    void onSimulationFrame();
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
    QPushButton *linkAdd = nullptr, *linkUpdate = nullptr, *linkRemove = nullptr, *linkNew = nullptr;
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
    QPushButton *pairsPickA = nullptr, *pairsPickB = nullptr;
    QLabel *stitchPairInfo = nullptr;
    QCheckBox *showAssemblyViewport = nullptr;
    QListWidget *anchorList = nullptr;
    QPushButton *anchorAdd = nullptr, *anchorRemove = nullptr, *anchorPick = nullptr;
    QComboBox *exclusionA = nullptr, *exclusionB = nullptr;
    QPushButton *exclusionAdd = nullptr, *exclusionRemove = nullptr;
    QListWidget *exclusionList = nullptr;
    QLabel *linkDiagnostics = nullptr;
    // The composer view mirrors Unreal's constraint workflow: pick two bodies,
    // place one shared pivot, choose an axis, then create.
    QLabel *linkTypeHint = nullptr, *linkError = nullptr, *linkPivotHint = nullptr;
    std::array<QDoubleSpinBox *, 3> linkPivot{};
    QPushButton *linkPickBodyA = nullptr, *linkPickBodyB = nullptr;
    QToolButton *linkSwap = nullptr;
    QPushButton *linkPivotMid = nullptr, *linkPivotFromA = nullptr, *linkPivotFromB = nullptr;
    QPushButton *linkPivotPick = nullptr, *linkAxisFlip = nullptr;
    QComboBox *anchorSnap = nullptr;
    QCheckBox *linkAdvancedAnchors = nullptr;
    int pickTarget = 0; // 1/2 = link anchors, 3 = new anchor, 4/5 = body A/B, 6 = pivot,
                        // 7/8 = stitch pair A anchor / B point
    bool assemblySyncing = false;
    bool linkComposerTouched = false;
    QJsonObject loadedLink;
    QCheckBox *driveEnabled=nullptr,*damperEnabled=nullptr;
    QDoubleSpinBox *driveSpeed=nullptr,*driveRamp=nullptr,*driveStrength=nullptr,*damperCoefficient=nullptr;
    QWidget *driveRow=nullptr,*damperRow=nullptr;
    QWidget *linkAnchorRow=nullptr,*linkLimitRow=nullptr,*linkRotationRow=nullptr,*linkStitchRow=nullptr;
    CollapsibleGroupBox *linkBodiesGroup = nullptr, *linkPivotGroup = nullptr, *linkAxisGroup = nullptr,
                        *linkParamsGroup = nullptr, *linkDriveGroup = nullptr;
    void setupAssemblyDock();
    void syncAssemblyPanel();
    void syncLinkForm(const Link &link);
    bool readLinkForm(Link &link, QString *error) const;
    bool commitLinks(const QVector<Link> &links, const QVector<QPair<QString, QString>> &exclusions,
                     const QString &label);
    int selectedLinkIndex() const;
    void deleteSelectedLink();
    void updateLinkDiagnostics();
    void updateLinkComposer();
    void setStitchRow(int row, int anchor, int target);
    void refreshStitchRows();
    void updateStitchPairInfo();
    int stitchPairAnchor(int row) const;
    int stitchPairTarget(int row) const;
    void syncPivotFromAnchors();
    bool applyPivotToAnchors();
    void setLinkError(const QString &error);
    void pickBody(int target);
    void focusSelectedLink();
    void handleViewportPick(const QVector3D &world, const QString &objectId);
    QTimer replayTimer;
    QElapsedTimer replayClock;
    std::array<QDoubleSpinBox *, 12> fields{};
    std::array<QToolButton *, 4> resetRow{};
    Object inspectorScrubBefore;
    bool inspectorScrubbing = false;
    std::array<QAction *, 3> toolActions{};
    QAction *snapAction = nullptr, *spaceAction = nullptr, *showLinksAction = nullptr, *showAnchorsAction = nullptr,
            *showHudAction = nullptr;
    QComboBox *snapStep = nullptr;
    QAction *deleteAction = nullptr, *duplicateAction = nullptr, *gridAction = nullptr;
    struct Document {
        int id = 0;
        std::unique_ptr<Scene> model;
        QString filePath, title;
        Tool tool = Tool::Move;
        // All physics work happens on this session's worker thread; preview is a
        // GUI-thread snapshot safe to render and record.
        SimulationSession simulation;
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
    // .bgp project currently owning the tab set. Empty when only loose scenes
    // have been edited. Project management never replaces per-scene undo stacks.
    QString projectPath, projectName;
    void showProjectManager();
    bool createProjectFromTabs();
    bool saveCurrentProject();
    void rememberProject(const QString &path);
    void forgetProject(const QString &path);
    QStringList recentProjects() const;
    int nextDocumentId = 1, nextUntitled = 1;
    bool syncing = false;
    bool simulationStarting = false;
    bool simulationStepWaiting = false;
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
    void setupSceneTabs();
    void addDocument(std::unique_ptr<Scene> model, const QString &path, const QString &title);
    void activateDocument(int index);
    Document *documentAt(int index) const;
    Document *activeDocument() const;
    int indexOf(Document *document) const;
    void updateTabTitles();
    void setupDocks();
    void syncTree();
    void syncSceneFilter();
    void syncInspector();
    void resetInspectorRow(int row);
    void syncSelection();
    void updateTitle();
    void commitFields();
    void finishInspectorEdit();
    bool confirmDiscard();
    bool save(bool as = false);
};
} // namespace bagua
