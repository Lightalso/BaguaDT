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
class Viewport : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit Viewport(Scene *scene, QWidget *parent = nullptr);
    ~Viewport() override;
    Tool tool = Tool::Move;
    QVector3D target{0, .5f, 0};
    float yaw = 38, pitch = 27, distance = 11;
    bool showGrid = true;
    bool editingEnabled = true;
    void setTool(Tool t);
    void setScene(Scene *model);
    void stopCameraNavigation();
    void frameSelected();
    void home();
    QPointF project(QVector3D world, bool *visible = nullptr) const;
    QString pick(QPointF point) const;
    QPointF handlePoint(int axis) const;
    bool dragging() const { return activeAxis >= 0; }
    void cancelDrag();
    QJsonObject cameraJson() const;
    void restoreCamera(const QJsonObject &data);
signals:
    void toolChanged();
    void message(const QString &text);

protected:
    bool event(QEvent *) override;
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
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
    int activeAxis = -1, hoverAxis = -1;
    Object beforeDrag;
    bool ready = false;
    QVector3D eye() const;
    QMatrix4x4 vp() const;
    float gizmoLength() const;
    std::array<QPainterPath, 3> handlePaths() const;
    int hitHandle(QPointF point) const;
    void drawOverlay(QPainter &painter);
    void upload(Mesh &, const QVector<Vertex> &);
    void draw(const Mesh &, const QMatrix4x4 &, QColor, bool lit, GLenum mode = GL_TRIANGLES);
    void cleanup();
};
} // namespace bagua
