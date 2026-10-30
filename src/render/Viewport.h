#pragma once
#include "Geometry.h"
#include "Scene.h"
#include <QElapsedTimer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPainterPath>
#include <QSet>
#include <QTimer>
#include <array>
#include <memory>

namespace bagua {
enum class Tool { Move, Rotate, Scale };
// Unreal's transform widget and Unity's handle orientation both expose a
// world/local switch. Scale stays local by definition (it edits the local
// scale components), while move/rotate follow this setting.
enum class TransformSpace { World, Local };

class Viewport : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit Viewport(Scene *scene, QWidget *parent = nullptr);
    ~Viewport() override;
    Tool tool = Tool::Move;
    TransformSpace transformSpace = TransformSpace::World;
    QVector3D target{0, .5f, 0};
    float yaw = 38, pitch = 27, distance = 11;
    bool showGrid = true;
    bool showLinks = true;
    bool showAnchors = true;
    bool showTransformHud = true;
    bool editingEnabled = true;
    bool anchorPicking = false;
    // Grid snapping. Ctrl forces snapping for one gesture even when the
    // persistent toggle is off; Shift selects the fine step.
    bool snapEnabled = false;
    float snapTranslate = .1f, snapRotate = 15.f, snapScale = .1f;
    void setTool(Tool t);
    void setTransformSpace(TransformSpace space);
    void setSnapEnabled(bool enabled);
    void setSnapSteps(float translate, float rotate, float scale);
    void setScene(Scene *model);
    void stopCameraNavigation();
    void frameSelected();
    void frameAll();
    void home();
    QPointF project(QVector3D world, bool *visible = nullptr) const;
    QString pick(QPointF point) const;
    // Surface point under the cursor, for anchor picking.
    QVector3D pickPoint(QPointF point, QString *objectId = nullptr) const;
    void setAnchorHighlight(const QString &objectId, int anchor);
    void setLinkHighlight(const QString &linkId);
    void clearLinkHighlight();
    QPointF handlePoint(int axis) const;
    bool dragging() const { return activeAxis >= 0; }
    void cancelDrag();
    QJsonObject cameraJson() const;
    void restoreCamera(const QJsonObject &data);
signals:
    void toolChanged();
    void message(const QString &text);
    void transformSpaceChanged();
    void snapChanged();
    // Left click in the viewport: world surface point (or the origin if nothing was hit).
    void clicked(const QVector3D &world, const QString &objectId);
    // Escape pressed in the viewport: also cancels an anchor-picking mode.
    void cancelled();
    // A constraint marker was activated in the viewport.
    void linkPicked(const QString &linkId);
    // Human-readable transform delta during a handle drag, mirrored to the HUD.
    void dragDelta(const QString &text);
    void hoverChanged(const QString &objectId);

protected:
    bool event(QEvent *) override;
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;

private:
    struct Mesh {
        GLuint vao = 0, vbo = 0;
        int count = 0;
    };
    Scene *scene;
    std::unique_ptr<QOpenGLShaderProgram> shader;
    std::unique_ptr<QOpenGLShaderProgram> overlayShader;
    std::array<Mesh, 4> meshes{};
    struct CachedMesh {
        Mesh gpu;
        std::shared_ptr<const MeshData> source;
    };
    QHash<QString, CachedMesh> importedMeshes;
    Mesh grid;
    Mesh overlayQuad;
    GLuint overlayTexture = 0;
    QPointF lastMouse, startMouse;
    Qt::MouseButton cameraButton = Qt::NoButton;
    QSet<int> flyKeys;
    QTimer flyTimer;
    QElapsedTimer flyClock;
    float flySpeed = 3.f;
    void advanceFlyCamera();
    // 0..2 axis, 3..5 plane handles, 6 center handle, 7 screen-space rotation ring.
    int activeAxis = -1, hoverAxis = -1;
    Object beforeDrag;
    bool ready = false;
    QString highlightObject;
    int highlightAnchor = -1;
    QString highlightLink;
    QString hoveredObject;
    QString dragFeedback;
    QVector3D dragStartPlanePoint, dragPlaneNormal;
    QVector3D eye() const;
    QMatrix4x4 vp() const;
    float gizmoLength() const;
    std::array<QVector3D, 3> transformAxes(const Object &object) const;
    std::array<QPainterPath, 3> handlePaths() const;
    // Three move-plane quads followed by the square center handle.
    std::array<QPainterPath, 4> planeHandlePaths() const;
    QPainterPath viewRingPath() const;
    int hitHandle(QPointF point) const;
    QString hitLinkMarker(QPointF point) const;
    bool mouseRay(QPointF point, QVector3D &origin, QVector3D &direction) const;
    bool rayPlane(QPointF point, const QVector3D &planePoint, const QVector3D &planeNormal,
                  QVector3D &intersection) const;
    void paintObject(const Object &object, const QMatrix4x4 &model, QColor color, bool lit);
    void drawSelectionOutline(const Object &object);
    void drawOverlay(QPainter &painter);
    void setDragFeedback(const QString &text);
    void nudgeSelection(int axis, float direction, bool large);
    void upload(Mesh &, const QVector<Vertex> &);
    void draw(const Mesh &, const QMatrix4x4 &, QColor, bool lit, GLenum mode = GL_TRIANGLES);
    void cleanup();
};
} // namespace bagua
