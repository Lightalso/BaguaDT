#include "Viewport.h"
#include <QApplication>
#include <QJsonArray>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPathStroker>
#include <QQuaternion>
#include <QResizeEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>

namespace bagua {
static constexpr float pi = 3.14159265358979323846f;
static const std::array<QVector3D, 3> axes = {QVector3D(1, 0, 0), QVector3D(0, 1, 0), QVector3D(0, 0, 1)};
static const std::array<QColor, 3> axisColors = {QColor("#d0444b"), QColor("#1b9a50"), QColor("#2f7ad6")};
static const std::array<QColor, 3> axisLightColors = {QColor("#ff8b8f"), QColor("#69d78f"), QColor("#7db8ff")};

static QString toolLabel(Tool tool) {
    return tool == Tool::Move ? QStringLiteral("移动") : tool == Tool::Rotate ? QStringLiteral("旋转")
                                                                              : QStringLiteral("缩放");
}
static QString spaceLabel(TransformSpace space) {
    return space == TransformSpace::World ? QStringLiteral("世界") : QStringLiteral("局部");
}
static QString linkTypeLabel(Link::Type type) {
    switch (type) {
    case Link::Type::Spherical:
        return QStringLiteral("球铰");
    case Link::Type::Revolute:
        return QStringLiteral("转动副");
    case Link::Type::Prismatic:
        return QStringLiteral("移动副");
    case Link::Type::Fixed:
        return QStringLiteral("固定副");
    case Link::Type::Rotation:
        return QStringLiteral("转动约束");
    case Link::Type::Stitch:
        return QStringLiteral("缝合 / 弹簧");
    }
    return QStringLiteral("链接");
}
static QColor linkColor(Link::Type type) {
    switch (type) {
    case Link::Type::Spherical:
        return QColor("#8e5bd8");
    case Link::Type::Revolute:
        return QColor("#e08a2e");
    case Link::Type::Prismatic:
        return QColor("#1f9fc4");
    case Link::Type::Fixed:
        return QColor("#d04545");
    case Link::Type::Rotation:
        return QColor("#2b7de0");
    case Link::Type::Stitch:
        return QColor("#7a5cff");
    }
    return QColor("#d05a3a");
}
static QString axisLetter(int a) {
    return QString(QChar("XYZ"[std::clamp(a, 0, 2)]));
}
static bool rayAabb(QVector3D origin, QVector3D direction, QVector3D low, QVector3D high, float &entry) {
    float t0 = -std::numeric_limits<float>::infinity(), t1 = std::numeric_limits<float>::infinity();
    for (int a = 0; a < 3; ++a) {
        if (std::abs(direction[a]) < 1e-9f) {
            if (origin[a] < low[a] || origin[a] > high[a])
                return false;
            continue;
        }
        const float inverse = 1.f / direction[a];
        float near = (low[a] - origin[a]) * inverse, far = (high[a] - origin[a]) * inverse;
        if (near > far)
            std::swap(near, far);
        t0 = std::max(t0, near);
        t1 = std::min(t1, far);
        if (t0 > t1)
            return false;
    }
    entry = std::max(t0, 0.f);
    return t1 >= entry;
}
static bool objectBounds(const Object &object, QVector3D &low, QVector3D &high) {
    const auto bounds = localBounds(object);
    if (object.scale.x() <= 0 || object.scale.y() <= 0 || object.scale.z() <= 0)
        return false;
    low = QVector3D(1e30f, 1e30f, 1e30f);
    high = QVector3D(-1e30f, -1e30f, -1e30f);
    const auto matrix = object.matrix();
    for (int i = 0; i < 8; ++i) {
        const QVector3D point = matrix.map({(i & 1) ? bounds.second.x() : bounds.first.x(),
                                            (i & 2) ? bounds.second.y() : bounds.first.y(),
                                            (i & 4) ? bounds.second.z() : bounds.first.z()});
        for (int a = 0; a < 3; ++a) {
            low[a] = std::min(low[a], point[a]);
            high[a] = std::max(high[a], point[a]);
        }
    }
    return true;
}

Viewport::Viewport(Scene *s, QWidget *p) : QOpenGLWidget(p), scene(s) {
    setObjectName("viewport");
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setContextMenuPolicy(Qt::PreventContextMenu);
    setMinimumSize(400, 320);
    flyTimer.setInterval(16);
    flyTimer.setTimerType(Qt::PreciseTimer);
    connect(&flyTimer, &QTimer::timeout, this, &Viewport::advanceFlyCamera);
    if (s) {
        connect(s, &Scene::changed, this, QOverload<>::of(&Viewport::update));
        connect(s, &Scene::selectionChanged, this, QOverload<>::of(&Viewport::update));
    }
}
void Viewport::setScene(Scene *model) {
    stopCameraNavigation();
    cancelDrag();
    if (scene)
        disconnect(scene, nullptr, this, nullptr);
    scene = model;
    hoveredObject.clear();
    highlightLink.clear();
    connect(scene, &Scene::changed, this, QOverload<>::of(&Viewport::update));
    connect(scene, &Scene::selectionChanged, this, QOverload<>::of(&Viewport::update));
    update();
}
Viewport::~Viewport() {
    flyTimer.stop();
    if (context())
        disconnect(context(), nullptr, this, nullptr);
    cleanup();
}
bool Viewport::event(QEvent *e) {
    if (e->type() == QEvent::FocusOut || e->type() == QEvent::WindowDeactivate || e->type() == QEvent::Hide)
        stopCameraNavigation();
    return QOpenGLWidget::event(e);
}
void Viewport::stopCameraNavigation() {
    const bool wasNavigating = cameraButton != Qt::NoButton;
    cameraButton = Qt::NoButton;
    flyKeys.clear();
    flyTimer.stop();
    flyClock.invalidate();
    unsetCursor();
    if (wasNavigating)
        update();
}
void Viewport::advanceFlyCamera() {
    if (cameraButton != Qt::RightButton || !flyClock.isValid())
        return;
    const float dt = std::min(.05f, float(flyClock.restart()) * .001f);
    const auto forward = (target - eye()).normalized();
    const auto right = QVector3D::crossProduct(forward, {0, 1, 0}).normalized();
    auto direction = forward * float(int(flyKeys.contains(Qt::Key_W)) - int(flyKeys.contains(Qt::Key_S))) +
                     right * float(int(flyKeys.contains(Qt::Key_D)) - int(flyKeys.contains(Qt::Key_A))) +
                     QVector3D(0, 1, 0) * float(int(flyKeys.contains(Qt::Key_E)) - int(flyKeys.contains(Qt::Key_Q)));
    if (direction.lengthSquared() < 1e-8f)
        return;
    const float speed = flySpeed * (flyKeys.contains(Qt::Key_Shift) ? 4.f : 1.f);
    // Translate the eye and look target together, with frame-rate-independent speed.
    target += direction.normalized() * speed * dt;
    update();
}
void Viewport::cleanup() {
    if (!context())
        return;
    makeCurrent();
    for (auto &m : meshes) {
        if (m.vbo)
            glDeleteBuffers(1, &m.vbo);
        if (m.vao)
            glDeleteVertexArrays(1, &m.vao);
        m = {};
    }
    for (auto &c : importedMeshes) {
        if (c.gpu.vbo)
            glDeleteBuffers(1, &c.gpu.vbo);
        if (c.gpu.vao)
            glDeleteVertexArrays(1, &c.gpu.vao);
    }
    importedMeshes.clear();
    for (auto *m : {&grid, &overlayQuad}) {
        if (m->vbo)
            glDeleteBuffers(1, &m->vbo);
        if (m->vao)
            glDeleteVertexArrays(1, &m->vao);
        *m = {};
    }
    if (overlayTexture)
        glDeleteTextures(1, &overlayTexture);
    overlayTexture = 0;
    shader.reset();
    overlayShader.reset();
    ready = false;
    doneCurrent();
}
void Viewport::initializeGL() {
    initializeOpenGLFunctions();
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &Viewport::cleanup, Qt::DirectConnection);
    shader = std::make_unique<QOpenGLShaderProgram>();
    bool ok = shader->addShaderFromSourceCode(QOpenGLShader::Vertex, R"(
        #version 330 core
        layout(location=0) in vec3 pos; layout(location=1) in vec3 normal;
        uniform mat4 mvp; uniform mat3 normalMatrix; out vec3 n;
        void main(){gl_Position=mvp*vec4(pos,1.0); n=normalMatrix*normal;}
    )");
    ok = shader->addShaderFromSourceCode(QOpenGLShader::Fragment, R"(
        #version 330 core
        in vec3 n; uniform vec4 color; uniform bool lit; out vec4 frag;
        void main(){float light=lit?(0.38+0.62*max(dot(normalize(n),normalize(vec3(0.4,0.85,0.55))),0.0)):1.0;frag=vec4(color.rgb*light,color.a);}
    )") &&
         ok;
    ready = ok && shader->link();
    if (!ready) {
        emit message(QStringLiteral("OpenGL 着色器错误：") + shader->log());
        return;
    }
    overlayShader = std::make_unique<QOpenGLShaderProgram>();
    overlayShader->addShaderFromSourceCode(QOpenGLShader::Vertex,
                                           "#version 330 core\nlayout(location=0) in vec3 pos; layout(location=1) in "
                                           "vec3 tex; out vec2 uv; void main(){uv=tex.xy;gl_Position=vec4(pos,1.0);}");
    overlayShader->addShaderFromSourceCode(
        QOpenGLShader::Fragment,
        "#version 330 core\nin vec2 uv;uniform sampler2D image;out vec4 frag;void main(){frag=texture(image,uv);}");
    ready = overlayShader->link();
    if (!ready) {
        emit message(overlayShader->log());
        return;
    }
    upload(overlayQuad, {{{-1, -1, 0}, {0, 1, 0}},
                         {{1, -1, 0}, {1, 1, 0}},
                         {{1, 1, 0}, {1, 0, 0}},
                         {{-1, -1, 0}, {0, 1, 0}},
                         {{1, 1, 0}, {1, 0, 0}},
                         {{-1, 1, 0}, {0, 0, 0}}});
    glGenTextures(1, &overlayTexture);
    glBindTexture(GL_TEXTURE_2D, overlayTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    for (int i = 0; i < 4; ++i)
        upload(meshes[i], bagua::geometry(Shape(i)));
    QVector<Vertex> lines;
    auto line = [&](QVector3D a, QVector3D b) { lines << Vertex{a, {0, 1, 0}} << Vertex{b, {0, 1, 0}}; };
    for (int i = -20; i <= 20; ++i) {
        line({float(i), .006f, -20}, {float(i), .006f, 20});
        line({-20, .006f, float(i)}, {20, .006f, float(i)});
    }
    upload(grid, lines);
}
void Viewport::upload(Mesh &m, const QVector<Vertex> &vertices) {
    if (!m.vao)
        glGenVertexArrays(1, &m.vao);
    glBindVertexArray(m.vao);
    if (!m.vbo)
        glGenBuffers(1, &m.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.constData(), GL_STATIC_DRAW);
    m.count = int(vertices.size());
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void *>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, normal)));
    glBindVertexArray(0);
}
QVector3D Viewport::eye() const {
    float y = yaw * pi / 180, p = pitch * pi / 180;
    return target + distance * QVector3D(sinf(y) * cosf(p), sinf(p), cosf(y) * cosf(p));
}
QMatrix4x4 Viewport::vp() const {
    QMatrix4x4 p, v;
    p.perspective(45, float(width()) / std::max(1, height()), .02f, std::max(2000.f, distance * 10));
    v.lookAt(eye(), target, {0, 1, 0});
    return p * v;
}
void Viewport::draw(const Mesh &mesh, const QMatrix4x4 &model, QColor c, bool lit, GLenum mode) {
    if (!shader || mesh.vao == 0)
        return;
    shader->bind();
    shader->setUniformValue("mvp", vp() * model);
    shader->setUniformValue("normalMatrix", model.normalMatrix());
    shader->setUniformValue("color", QVector4D(c.redF(), c.greenF(), c.blueF(), 1));
    shader->setUniformValue("lit", lit);
    glBindVertexArray(mesh.vao);
    glDrawArrays(mode, 0, mesh.count);
    glBindVertexArray(0);
    shader->release();
}
void Viewport::paintObject(const Object &o, const QMatrix4x4 &model, QColor color, bool lit) {
    if (o.shape == Shape::Mesh && o.mesh) {
        auto &cached = importedMeshes[o.id];
        if (cached.source != o.mesh) {
            upload(cached.gpu, bagua::geometry(o));
            cached.source = o.mesh;
        }
        draw(cached.gpu, model, color, lit);
    } else
        draw(meshes[int(o.shape)], model, color, lit);
}
void Viewport::drawSelectionOutline(const Object &o) {
    if (!ready)
        return;
    glStencilMask(0xFF);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_FALSE);
    paintObject(o, o.matrix(), Qt::white, false);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    QMatrix4x4 outline = o.matrix();
    outline.scale(1.025f);
    // Depth testing stays on so an occluded selection outline does not leak
    // through walls; depth writes stay off so the outline only fills the ring.
    paintObject(o, outline, QColor("#0b74d1"), false);
    glDepthMask(GL_TRUE);
    glDisable(GL_STENCIL_TEST);
}
void Viewport::paintGL() {
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glClearColor(.925f, .937f, .953f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    if (ready) {
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1, 1);
        for (const auto &o : scene->objects)
            if (o.visible) {
                QColor c = o.color;
                if (o.id == scene->selected)
                    c = c.lighter(114);
                else if (o.id == hoveredObject)
                    c = c.lighter(106);
                paintObject(o, o.matrix(), c, true);
            }
        glDisable(GL_POLYGON_OFFSET_FILL);
        for (auto it = importedMeshes.begin(); it != importedMeshes.end();) {
            const auto *object = scene->find(it.key());
            if (!object || object->shape != Shape::Mesh) {
                glDeleteBuffers(1, &it->gpu.vbo);
                glDeleteVertexArrays(1, &it->gpu.vao);
                it = importedMeshes.erase(it);
            } else
                ++it;
        }
        if (const auto *selected = scene->current(); selected && selected->visible && editingEnabled)
            drawSelectionOutline(*selected);
        if (showGrid) {
            QMatrix4x4 identity;
            draw(grid, identity, QColor("#b9c2cd"), false, GL_LINES);
        }
        glDisable(GL_DEPTH_TEST);
        // Paint the UI overlay in a raster image, then composite explicitly.
        // This isolates Qt's painter state from the OpenGL scene renderer.
        const qreal dpr = devicePixelRatioF();
        QImage image(QSize(qRound(width() * dpr), qRound(height() * dpr)), QImage::Format_RGBA8888_Premultiplied);
        image.setDevicePixelRatio(dpr);
        image.fill(Qt::transparent);
        {
            QPainter p(&image);
            drawOverlay(p);
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, overlayTexture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width(), image.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     image.constBits());
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        overlayShader->bind();
        overlayShader->setUniformValue("image", 0);
        glBindVertexArray(overlayQuad.vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        overlayShader->release();
        glDisable(GL_BLEND);
    }
}
QPointF Viewport::project(QVector3D world, bool *visible) const {
    auto p = vp() * QVector4D(world, 1);
    bool valid = p.w() > .001f && p.z() > -p.w() && p.z() < p.w();
    if (visible)
        *visible = valid;
    if (std::abs(p.w()) < 1e-8f)
        return {-10000, -10000};
    return {(p.x() / p.w() + 1) * width() / 2, (1 - p.y() / p.w()) * height() / 2};
}
bool Viewport::mouseRay(QPointF p, QVector3D &origin, QVector3D &direction) const {
    const auto inverse = vp().inverted();
    const float x = 2 * float(p.x()) / std::max(1, width()) - 1, y = 1 - 2 * float(p.y()) / std::max(1, height());
    const auto nearPoint = inverse * QVector4D(x, y, -1, 1), farPoint = inverse * QVector4D(x, y, 1, 1);
    origin = nearPoint.toVector3DAffine();
    const auto far = farPoint.toVector3DAffine();
    const auto delta = far - origin;
    if (delta.lengthSquared() < 1e-12f)
        return false;
    direction = delta.normalized();
    return true;
}
bool Viewport::rayPlane(QPointF p, const QVector3D &planePoint, const QVector3D &planeNormal,
                        QVector3D &intersection) const {
    QVector3D origin, direction;
    if (!mouseRay(p, origin, direction))
        return false;
    const float denominator = QVector3D::dotProduct(direction, planeNormal);
    if (std::abs(denominator) < 1e-5f)
        return false;
    const float t = QVector3D::dotProduct(planePoint - origin, planeNormal) / denominator;
    if (t <= 0)
        return false;
    intersection = origin + direction * t;
    return true;
}
QVector3D Viewport::pickPoint(QPointF p, QString *objectId) const {
    QVector3D origin, direction;
    if (!mouseRay(p, origin, direction)) {
        if (objectId)
            objectId->clear();
        return target;
    }
    float closest = std::numeric_limits<float>::infinity();
    QString id;
    for (const auto &o : scene->objects)
        if (o.visible) {
            QVector3D low, high;
            float entry = 0;
            if (!objectBounds(o, low, high) || !rayAabb(origin, direction, low, high, entry) || entry > closest)
                continue;
            float d = intersect(o, origin, direction);
            if (d < closest) {
                closest = d;
                id = o.id;
            }
        }
    if (objectId)
        *objectId = id;
    return std::isfinite(closest) ? origin + direction * closest : origin + direction * distance;
}
void Viewport::setAnchorHighlight(const QString &objectId, int anchor) {
    highlightObject = objectId;
    highlightAnchor = anchor;
    update();
}
void Viewport::setLinkHighlight(const QString &linkId) {
    if (highlightLink == linkId)
        return;
    highlightLink = linkId;
    update();
}
void Viewport::clearLinkHighlight() {
    setLinkHighlight({});
}
QString Viewport::pick(QPointF p) const {
    QVector3D origin, direction;
    if (!mouseRay(p, origin, direction))
        return {};
    float closest = std::numeric_limits<float>::infinity();
    QString id;
    for (const auto &o : scene->objects)
        if (o.visible) {
            QVector3D low, high;
            float entry = 0;
            if (!objectBounds(o, low, high) || !rayAabb(origin, direction, low, high, entry) || entry > closest)
                continue;
            float d = intersect(o, origin, direction);
            if (d < closest) {
                closest = d;
                id = o.id;
            }
        }
    return id;
}
float Viewport::gizmoLength() const {
    auto *o = scene->current();
    return o ? std::max(.1f, (eye() - o->position).length() * 2 * tanf(22.5f * pi / 180) * 92 / std::max(1, height()))
             : 1;
}
std::array<QVector3D, 3> Viewport::transformAxes(const Object &object) const {
    std::array<QVector3D, 3> result = axes;
    if (tool == Tool::Scale || transformSpace == TransformSpace::Local) {
        const auto rotation = QQuaternion::fromEulerAngles(object.rotation);
        for (int a = 0; a < 3; ++a)
            result[a] = rotation.rotatedVector(axes[a]).normalized();
    }
    return result;
}
std::array<QPainterPath, 3> Viewport::handlePaths() const {
    std::array<QPainterPath, 3> paths;
    const auto *o = scene->current();
    if (!o || !o->visible || !editingEnabled)
        return paths;
    bool visible;
    project(o->position, &visible);
    if (!visible)
        return paths;
    const auto directions = transformAxes(*o);
    const float length = gizmoLength();
    for (int a = 0; a < 3; ++a) {
        if (tool != Tool::Rotate) {
            paths[a].moveTo(project(o->position));
            paths[a].lineTo(project(o->position + directions[a] * length));
        } else {
            for (int i = 0; i <= 96; ++i) {
                const float t = 2 * pi * i / 96;
                const auto w = o->position + length * (directions[(a + 1) % 3] * cosf(t) + directions[(a + 2) % 3] * sinf(t));
                const auto point = project(w);
                if (i == 0)
                    paths[a].moveTo(point);
                else
                    paths[a].lineTo(point);
            }
        }
    }
    return paths;
}
std::array<QPainterPath, 4> Viewport::planeHandlePaths() const {
    std::array<QPainterPath, 4> paths;
    const auto *o = scene->current();
    if (!o || !o->visible || !editingEnabled || tool == Tool::Rotate)
        return paths;
    const auto directions = transformAxes(*o);
    const float length = gizmoLength();
    if (tool == Tool::Move) {
        for (int a = 0; a < 3; ++a) {
            const int i = (a + 1) % 3, j = (a + 2) % 3;
            const auto middle = o->position + (directions[i] + directions[j]) * length * .42f;
            const float half = length * .085f;
            const std::array<QVector3D, 4> corners = {middle - directions[i] * half - directions[j] * half,
                                                      middle + directions[i] * half - directions[j] * half,
                                                      middle + directions[i] * half + directions[j] * half,
                                                      middle - directions[i] * half + directions[j] * half};
            QPolygonF polygon;
            bool valid = true;
            for (const auto &corner : corners) {
                bool visible;
                const auto point = project(corner, &visible);
                valid = valid && visible;
                polygon << point;
            }
            if (valid) {
                paths[a].addPolygon(polygon);
                paths[a].closeSubpath();
            }
        }
    }
    const auto center = project(o->position);
    paths[3].addRect(QRectF(center - QPointF(6, 6), QSizeF(12, 12)));
    return paths;
}
QPainterPath Viewport::viewRingPath() const {
    QPainterPath path;
    const auto *o = scene->current();
    if (!o || !o->visible || !editingEnabled || tool != Tool::Rotate)
        return path;
    const auto center = project(o->position);
    const auto directions = transformAxes(*o);
    bool visible;
    const auto edge = project(o->position + directions[0] * gizmoLength(), &visible);
    if (!visible)
        return path;
    const double radius = std::max(12.0, QLineF(center, edge).length() * 1.12);
    path.addEllipse(center, radius, radius);
    return path;
}
QPointF Viewport::handlePoint(int a) const {
    if (a >= 3 && a <= 6) {
        const auto paths = planeHandlePaths();
        return paths[size_t(std::clamp(a - 3, 0, 3))].boundingRect().center();
    }
    if (a == 7) {
        const auto path = viewRingPath();
        return path.isEmpty() ? QPointF{} : path.pointAtPercent(.125);
    }
    const auto paths = handlePaths();
    if (a < 0 || a > 2 || paths[size_t(a)].isEmpty())
        return {};
    return paths[size_t(a)].pointAtPercent(tool == Tool::Rotate ? .125 : .78);
}
int Viewport::hitHandle(QPointF p) const {
    const auto paths = handlePaths();
    const auto *o = scene->current();
    if (!o)
        return -1;
    if (tool != Tool::Rotate) {
        const auto planes = planeHandlePaths();
        // The center square sits at the pivot and wins over the axis strokes,
        // because axis strokes also start there.
        if (!planes[3].isEmpty() && planes[3].contains(p))
            return 6;
        if (tool == Tool::Move)
            for (int a = 0; a < 3; ++a)
                if (!planes[size_t(a)].isEmpty() && planes[size_t(a)].contains(p))
                    return 3 + a;
    }
    int best = -1;
    double nearestDistance = 12.0;
    for (int a = 0; a < 3; ++a) {
        if (paths[size_t(a)].isEmpty())
            continue;
        QPainterPathStroker stroke;
        stroke.setWidth(14);
        if (!stroke.createStroke(paths[size_t(a)]).contains(p))
            continue;
        double nearest = 1e20;
        for (int i = 0; i <= 96; ++i) {
            const auto q = paths[size_t(a)].pointAtPercent(double(i) / 96);
            nearest = std::min(nearest, QLineF(q, p).length());
        }
        if (nearest < nearestDistance) {
            nearestDistance = nearest;
            best = a;
        }
    }
    if (best >= 0)
        return best;
    if (tool == Tool::Rotate) {
        const auto ring = viewRingPath();
        if (!ring.isEmpty()) {
            QPainterPathStroker stroke;
            stroke.setWidth(9);
            if (stroke.createStroke(ring).contains(p))
                return 7;
        }
    }
    return -1;
}
QString Viewport::hitLinkMarker(QPointF point) const {
    QString result;
    double best = 9.0;
    for (auto it = scene->links.crbegin(); it != scene->links.crend(); ++it) {
        const auto *a = scene->find(it->a);
        const auto *b = scene->find(it->b);
        if (!a || !b)
            continue;
        for (const auto &world : {a->anchorMatrix().map(it->anchorA), b->anchorMatrix().map(it->anchorB)}) {
            bool visible;
            const auto screen = project(world, &visible);
            if (!visible)
                continue;
            const double markerDistance = QLineF(screen, point).length();
            if (markerDistance < best) {
                best = markerDistance;
                result = it->id;
            }
        }
    }
    return result;
}
void Viewport::setDragFeedback(const QString &text) {
    if (dragFeedback == text)
        return;
    dragFeedback = text;
    emit dragDelta(text);
    update();
}
void Viewport::drawOverlay(QPainter &p) {
    p.setRenderHint(QPainter::Antialiasing);
    p.setFont(QFont("Microsoft YaHei UI", 9));
    // Ground axes are projected from world coordinates, including during camera motion.
    for (int a : {0, 2}) {
        bool v1, v2;
        auto x = project(-axes[a] * 20, &v1), y = project(axes[a] * 20, &v2);
        if (v1 && v2) {
            QColor c = axisColors[a];
            c.setAlpha(90);
            p.setPen(QPen(c, 1));
            p.drawLine(x, y);
        }
    }
    if (showLinks) {
        // Selected link parts are outlined so the assembly relation is readable
        // even when the two bodies overlap in screen space.
        auto highlightPart = [&](const Object &object, const QColor &color, const QString &tag) {
            QVector3D low, high;
            if (!objectBounds(object, low, high))
                return;
            const std::array<QVector3D, 8> corners = {
                QVector3D(low.x(), low.y(), low.z()),     QVector3D(high.x(), low.y(), low.z()),
                QVector3D(low.x(), high.y(), low.z()),    QVector3D(high.x(), high.y(), low.z()),
                QVector3D(low.x(), low.y(), high.z()),    QVector3D(high.x(), low.y(), high.z()),
                QVector3D(low.x(), high.y(), high.z()),   QVector3D(high.x(), high.y(), high.z())};
            std::array<QPointF, 8> projected;
            for (int i = 0; i < 8; ++i) {
                bool visible = false;
                projected[size_t(i)] = project(corners[size_t(i)], &visible);
                if (!visible)
                    return;
            }
            static const int edges[12][2] = {{0, 1}, {0, 2}, {0, 4}, {1, 3}, {1, 5}, {2, 3},
                                             {2, 6}, {3, 7}, {4, 5}, {4, 6}, {5, 7}, {6, 7}};
            p.save();
            QColor outline = color;
            outline.setAlpha(210);
            p.setPen(QPen(outline, 1.5, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            for (const auto &edge : edges)
                p.drawLine(projected[size_t(edge[0])], projected[size_t(edge[1])]);
            const auto center = project((low + high) * .5f);
            p.setPen(QColor("#ffffff"));
            p.setBrush(outline);
            p.drawEllipse(center, 8, 8);
            p.setFont(QFont("Microsoft YaHei UI", 8, QFont::Bold));
            p.drawText(QRectF(center.x() - 8, center.y() - 8, 16, 16), Qt::AlignCenter, tag);
            p.restore();
        };
        // Links, anchors and constraints are projected from world coordinates, so
        // they follow the camera, the running preview and the edited pose.
        for (const auto &link : scene->links) {
            const Object *a = scene->find(link.a), *b = scene->find(link.b);
            if (!a || !b)
                continue;
            const bool active = link.id == highlightLink;
            const QColor color = linkColor(link.type);
            const auto wa = a->anchorMatrix().map(link.anchorA), wb = b->anchorMatrix().map(link.anchorB);
            bool v1, v2;
            const auto pa = project(wa, &v1), pb = project(wb, &v2);
            if (active) {
                highlightPart(*a, color, QStringLiteral("A"));
                highlightPart(*b, color, QStringLiteral("B"));
            }
            if (!link.kinematic()) {
                QColor stitch = linkColor(Link::Type::Stitch);
                stitch.setAlpha(active ? 235 : 80);
                p.setPen(QPen(stitch, active ? 2.2 : 0.9, active ? Qt::SolidLine : Qt::DotLine));
                QPointF labelPoint;
                bool hasLabelPoint = false;
                for (const auto &pair : link.pairs) {
                    if (pair.first < 0 || pair.first >= a->anchors.size())
                        continue;
                    QVector3D targetPoint;
                    if (link.anchorPairs) {
                        if (pair.second < 0 || pair.second >= b->anchors.size())
                            continue;
                        targetPoint = b->anchorMatrix().map(b->anchors[pair.second]);
                    } else {
                        if (!b->mesh || pair.second < 0 || pair.second >= b->mesh->positions.size())
                            continue;
                        targetPoint = b->matrix().map(b->mesh->positions[pair.second]);
                    }
                    bool va, vb;
                    auto from = project(a->anchorMatrix().map(a->anchors[pair.first]), &va), to = project(targetPoint, &vb);
                    if (!va || !vb)
                        continue;
                    p.drawLine(from, to);
                    if (!hasLabelPoint) {
                        labelPoint = (from + to) * .5;
                        hasLabelPoint = true;
                    }
                    if (active) {
                        p.setBrush(QColor(255, 255, 255, 235));
                        p.setPen(QPen(stitch, 1));
                        p.drawEllipse(from, 3.2, 3.2);
                        p.drawEllipse(to, 3.2, 3.2);
                    }
                }
                if (active) {
                    const QString text = QStringLiteral("缝合 · A %1 ↔ B %2 · %3 对")
                                             .arg(a->name.isEmpty() ? a->id : a->name,
                                                  b->name.isEmpty() ? b->id : b->name)
                                             .arg(link.pairs.size());
                    const QFontMetrics metrics(p.font());
                    const qreal plateWidth = metrics.horizontalAdvance(text) + 14;
                    QPointF at = hasLabelPoint ? labelPoint : (pa + pb) * .5;
                    at.setX(std::clamp(at.x() + 10, 8.0, std::max(8.0, width() - plateWidth - 8)));
                    at.setY(std::clamp(at.y() - 20, 8.0, std::max(8.0, height() - 28.0)));
                    const QRectF plate(at, QSizeF(plateWidth, 20));
                    p.setPen(QPen(stitch, 1));
                    p.setBrush(QColor(255, 255, 255, 242));
                    p.drawRoundedRect(plate, 3, 3);
                    p.setPen(stitch.darker(140));
                    p.drawText(plate.adjusted(6, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
                }
                continue;
            }
            if (v1 && v2) {
                QColor c = color;
                c.setAlpha(active ? 235 : 65);
                p.setPen(QPen(c, active ? 2.4 : 0.9, active ? Qt::SolidLine : Qt::DashLine));
                p.drawLine(pa, pb);
                p.setBrush(c);
                p.setPen(QPen(c.darker(130), 1));
                p.drawEllipse(pa, active ? 5.0 : 3.0, active ? 5.0 : 3.0);
                p.drawEllipse(pb, active ? 4.5 : 2.6, active ? 4.5 : 2.6);
                if (active) {
                    p.save();
                    p.setFont(QFont("Microsoft YaHei UI", 8, QFont::Bold));
                    for (const auto &[point, tag] : {std::pair{pa, QStringLiteral("A")}, std::pair{pb, QStringLiteral("B")}}) {
                        p.setPen(QPen(color, 1.2));
                        p.setBrush(QColor(255, 255, 255, 242));
                        p.drawEllipse(point, 8, 8);
                        p.setPen(color.darker(140));
                        p.drawText(QRectF(point.x() - 8, point.y() - 8, 16, 16), Qt::AlignCenter, tag);
                    }
                    p.restore();
                }
            }
            if (link.type != Link::Type::Spherical && link.type != Link::Type::Fixed) {
                auto direction = link.axis;
                if (a->mesh && a->mesh->frameVertices.size() == 4)
                    direction = a->anchorMatrix().mapVector(
                        QQuaternion::fromEulerAngles(a->rotation).conjugated().rotatedVector(direction) / a->scale);
                const auto dir = direction.normalized() * (active ? .34f : .22f);
                bool v3, v4;
                const auto a0 = project(wa - dir, &v3), a1 = project(wa + dir, &v4);
                if (v3 && v4) {
                    p.setPen(QPen(active ? color : QColor(color.red(), color.green(), color.blue(), 120),
                                  active ? 2.6 : 1.4));
                    p.drawLine(a0, a1);
                    if (active) {
                        p.setBrush(color);
                        p.setPen(Qt::NoPen);
                        const auto unit = (a1 - a0) / std::max(1.0, QLineF(a0, a1).length());
                        const auto side = QPointF(-unit.y(), unit.x());
                        p.drawPolygon(QPolygonF{a1 + unit * 4, a1 - unit * 8 + side * 4, a1 - unit * 8 - side * 4});
                    }
                }
            }
            if (active) {
                const auto midpoint = (pa + pb) / 2;
                const QString label = QStringLiteral("%1 · A %2 ↔ B %3")
                                          .arg(linkTypeLabel(link.type),
                                               a->name.isEmpty() ? a->id : a->name,
                                               b->name.isEmpty() ? b->id : b->name);
                const QFontMetrics metrics(p.font());
                const qreal plateWidth = metrics.horizontalAdvance(label) + 14;
                QPointF at(midpoint.x() + 10, midpoint.y() - 22);
                at.setX(std::clamp(at.x(), 8.0, std::max(8.0, width() - plateWidth - 8)));
                at.setY(std::clamp(at.y(), 8.0, std::max(8.0, height() - 28.0)));
                const QRectF plate(at, QSizeF(plateWidth, 20));
                p.setPen(QPen(color, 1));
                p.setBrush(QColor(255, 255, 255, 242));
                p.drawRoundedRect(plate, 3, 3);
                p.setPen(color.darker(140));
                p.drawText(plate.adjusted(6, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
            }
        }
    }
    if (showAnchors) {
        for (const auto &o : scene->objects) {
            if (!o.visible)
                continue;
            for (int i = 0; i < o.anchors.size(); ++i) {
                bool visible;
                const auto s = project(o.anchorMatrix().map(o.anchors[i]), &visible);
                if (!visible)
                    continue;
                const bool hot = o.id == highlightObject && i == highlightAnchor;
                p.setPen(QPen(QColor("#12503a"), 1.2));
                p.setBrush(hot ? QColor("#ffd166") : QColor("#4cd4a0"));
                const qreal r = hot ? 5.5 : 3.5;
                p.drawEllipse(s, r, r);
                if (o.id == scene->selected) {
                    p.setBrush(Qt::NoBrush);
                    p.setPen(QPen(QColor("#173042"), 1));
                    p.drawText(s + QPointF(6, -6), QString::number(i));
                }
            }
        }
        p.setBrush(Qt::NoBrush);
    }
    if (auto *o = scene->current(); o && o->visible && editingEnabled) {
        const auto center = project(o->position);
        const auto directions = transformAxes(*o);
        const auto planes = planeHandlePaths();
        if (tool == Tool::Move) {
            for (int a = 0; a < 3; ++a) {
                const bool hot = activeAxis == 3 + a || hoverAxis == 3 + a;
                QColor c = axisColors[(a + 1) % 3];
                c.setAlpha(hot ? 200 : 110);
                p.setPen(QPen(c, hot ? 1.8 : 1.1));
                QColor fill = axisColors[(a + 2) % 3];
                fill.setAlpha(hot ? 130 : 64);
                p.setBrush(fill);
                p.drawPath(planes[size_t(a)]);
            }
        }
        if (tool != Tool::Rotate) {
            const bool hot = activeAxis == 6 || hoverAxis == 6;
            p.setBrush(hot ? QColor("#ffcf55") : QColor(255, 255, 255, 238));
            p.setPen(QPen(hot ? QColor("#b77900") : QColor("#54606d"), 1.2));
            p.drawRect(planes[3].boundingRect());
            p.setBrush(Qt::NoBrush);
        }
        if (tool == Tool::Rotate) {
            const auto ring = viewRingPath();
            if (!ring.isEmpty()) {
                p.setPen(QPen(QColor(255, 255, 255, 220), 3.4));
                p.drawPath(ring);
                p.setPen(QPen(activeAxis == 7 || hoverAxis == 7 ? QColor("#b77900") : QColor("#76828f"), 1.2,
                              Qt::DashLine));
                p.drawPath(ring);
            }
        }
        for (int a = 0; a < 3; ++a) {
            QColor c = (a == activeAxis || a == hoverAxis) ? QColor("#f0a400") : axisColors[a];
            p.setPen(QPen(QColor("#ffffff"), 4.6, Qt::SolidLine, Qt::RoundCap));
            p.drawPath(handlePaths()[size_t(a)]);
            p.setPen(QPen(c, 2.1, Qt::SolidLine, Qt::RoundCap));
            p.drawPath(handlePaths()[size_t(a)]);
            if (tool != Tool::Rotate) {
                const auto end = project(o->position + directions[a] * gizmoLength());
                p.setBrush(c);
                if (tool == Tool::Scale)
                    p.drawRect(QRectF(end - QPointF(4, 4), QSizeF(8, 8)));
                else {
                    QLineF l(center, end);
                    if (l.length() > 1) {
                        const auto unit = (end - center) / l.length(), side = QPointF(-unit.y(), unit.x());
                        p.drawPolygon(
                            QPolygonF{end + unit * 3, end - unit * 10 + side * 4, end - unit * 10 - side * 4});
                    }
                }
                p.setPen(QColor("#35404c"));
                p.drawText(end + QPointF(8, -7), axisLetter(a));
            } else {
                const auto edge = project(o->position + directions[a] * gizmoLength() * .96f);
                p.setPen(c);
                p.drawText(edge + QPointF(4, -4), axisLetter(a));
            }
        }
    }
    p.setPen(QColor("#465262"));
    p.drawText(10, 19, QStringLiteral("透视 · 米 · Y 轴向上"));
    p.drawText(QRect(width() - 110, 5, 100, 20), Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("%1 个对象").arg(scene->objects.size()));
    if (showTransformHud) {
        // Keep this as a single, low-contrast status strip. The previous
        // multi-line card covered a large part of the viewport and duplicated
        // the inspector values.
        QString line = QStringLiteral("%1 · %2坐标系 · %3")
                           .arg(toolLabel(tool), spaceLabel(transformSpace),
                                snapEnabled ? QStringLiteral("吸附 %1/%2/%3")
                                                  .arg(snapTranslate, 0, 'g', 3)
                                                  .arg(snapRotate, 0, 'g', 3)
                                                  .arg(snapScale, 0, 'g', 3)
                                            : QStringLiteral("吸附关"));
        if (!dragFeedback.isEmpty())
            line += QStringLiteral(" · ") + dragFeedback;
        QFont small = p.font();
        small.setPointSize(8);
        p.setFont(small);
        const QFontMetrics metrics(small);
        const qreal pillWidth = std::min<qreal>(width() - 20, metrics.horizontalAdvance(line) + 16);
        const QRectF pill(10, 28, pillWidth, 18);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 185));
        p.drawRoundedRect(pill, 3, 3);
        p.setPen(dragFeedback.isEmpty() ? QColor("#5c6875") : QColor("#9a5b00"));
        p.drawText(pill.adjusted(7, 0, -7, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   metrics.elidedText(line, Qt::ElideRight, int(pillWidth - 14)));
    }
    p.setPen(QColor("#66717e"));
    p.setFont(QFont("Microsoft YaHei UI", 8));
    p.drawText(10, height() - 10,
               cameraButton == Qt::RightButton
                   ? QStringLiteral("漫游：WASD 移动 · Q/E 下/上 · Shift 加速 · 滚轮调速  %1 m/s")
                         .arg(flySpeed, 0, 'f', 1)
                   : QStringLiteral("左键选择 · 拖动轴 / 平面 / 中心柄 · W/E/R 工具 · L 世界/局部 · G 吸附 · F "
                                    "聚焦 · Shift 精细 · Ctrl 吸附 · Esc 取消"));
    // Camera orientation indicator.
    auto view = (target - eye()).normalized();
    auto right = QVector3D::crossProduct(view, {0, 1, 0}).normalized(), up = QVector3D::crossProduct(right, view);
    QPointF c(width() - 40, height() - 42);
    for (int a = 0; a < 3; ++a) {
        QPointF e = c + QPointF(QVector3D::dotProduct(axes[a], right) * 20, -QVector3D::dotProduct(axes[a], up) * 20);
        p.setPen(QPen(axisColors[a], 2));
        p.drawLine(c, e);
        p.drawText(e + QPointF(4, 0), axisLetter(a));
    }
    if (!ready) {
        p.setPen(QColor("#b42318"));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("无法初始化 OpenGL 3.3，请检查显卡驱动"));
    }
}
void Viewport::setTool(Tool t) {
    if (dragging())
        cancelDrag();
    tool = t;
    hoverAxis = -1;
    dragFeedback.clear();
    emit toolChanged();
    update();
}
void Viewport::setTransformSpace(TransformSpace space) {
    if (transformSpace == space)
        return;
    if (dragging())
        cancelDrag();
    transformSpace = space;
    emit transformSpaceChanged();
    update();
}
void Viewport::setSnapEnabled(bool enabled) {
    if (snapEnabled == enabled)
        return;
    snapEnabled = enabled;
    emit snapChanged();
    update();
}
void Viewport::setSnapSteps(float translate, float rotate, float scale) {
    snapTranslate = std::clamp(translate, .0001f, 100.f);
    snapRotate = std::clamp(rotate, .01f, 180.f);
    snapScale = std::clamp(scale, .0001f, 100.f);
    emit snapChanged();
    update();
}
void Viewport::frameSelected() {
    if (auto *o = scene->current()) {
        const auto bounds = localBounds(*o);
        target = o->matrix().map((bounds.first + bounds.second) * .5f);
        const QVector3D size = (bounds.second - bounds.first) * o->dimensions * o->scale;
        distance = std::clamp(size.length() * 1.7f, 1.f, 500.f);
    }
    update();
}
void Viewport::frameAll() {
    if (scene->objects.isEmpty()) {
        home();
        return;
    }
    QVector3D low(1e30f, 1e30f, 1e30f), high(-1e30f, -1e30f, -1e30f);
    for (const auto &o : scene->objects) {
        if (!o.visible)
            continue;
        QVector3D objectLow, objectHigh;
        if (!objectBounds(o, objectLow, objectHigh))
            continue;
        for (int a = 0; a < 3; ++a) {
            low[a] = std::min(low[a], objectLow[a]);
            high[a] = std::max(high[a], objectHigh[a]);
        }
    }
    if (low.x() > high.x()) {
        home();
        return;
    }
    target = (low + high) * .5f;
    distance = std::clamp((high - low).length() * 1.25f, 1.5f, 1000.f);
    update();
}
void Viewport::home() {
    stopCameraNavigation();
    target = {0, .5f, 0};
    yaw = 38;
    pitch = 27;
    distance = 11;
    update();
}
void Viewport::nudgeSelection(int axis, float direction, bool large) {
    if (!editingEnabled)
        return;
    auto *current = scene->current();
    if (!current)
        return;
    Object object = *current;
    float step = snapTranslate * (large ? 10.f : 1.f);
    object.position[axis] += direction * step;
    scene->edit(object, QStringLiteral("微调对象"));
    emit message(QStringLiteral("%1 %2%3 m")
                     .arg(axisLetter(axis))
                     .arg(direction >= 0 ? QStringLiteral("+") : QStringLiteral("-"))
                     .arg(step, 0, 'g', 4));
}
void Viewport::mousePressEvent(QMouseEvent *e) {
    setFocus();
    if (cameraButton == Qt::RightButton) {
        e->accept();
        return;
    }
    lastMouse = startMouse = e->position();
    if (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton ||
        (e->button() == Qt::LeftButton && (e->modifiers() & Qt::AltModifier))) {
        cancelDrag();
        stopCameraNavigation();
        cameraButton = e->button();
        if (cameraButton == Qt::RightButton) {
            if (e->modifiers() & Qt::ShiftModifier)
                flyKeys.insert(Qt::Key_Shift);
            flyClock.start();
            flyTimer.start();
            setCursor(Qt::BlankCursor);
        } else
            setCursor(Qt::ClosedHandCursor);
        update();
        e->accept();
        return;
    }
    if (e->button() == Qt::LeftButton) {
        if (anchorPicking) {
            activeAxis = -1;
            QString hit;
            auto world = pickPoint(e->position(), &hit);
            double best = 64.;
            for (const auto &o : scene->objects)
                if (o.visible)
                    for (auto a : o.anchors) {
                        bool visible;
                        const auto wp = o.anchorMatrix().map(a);
                        const auto screen = project(wp, &visible);
                        const auto delta = screen - e->position();
                        const double d = QPointF::dotProduct(delta, delta);
                        if (visible && d < best) {
                            best = d;
                            world = wp;
                            hit = o.id;
                        }
                    }
            scene->select(hit);
            emit clicked(world, hit);
            update();
            return;
        }
        activeAxis = hitHandle(e->position());
        if (activeAxis >= 0 && scene->current()) {
            beforeDrag = *scene->current();
            dragStartPlanePoint = beforeDrag.position;
            dragPlaneNormal = QVector3D(0, 1, 0);
            bool started = true;
            if (activeAxis >= 3 && activeAxis <= 5) {
                const auto directions = transformAxes(beforeDrag);
                dragPlaneNormal = directions[activeAxis - 3];
                started = rayPlane(e->position(), beforeDrag.position, dragPlaneNormal, dragStartPlanePoint);
            } else if (activeAxis == 6) {
                dragPlaneNormal = (eye() - beforeDrag.position).normalized();
                started = rayPlane(e->position(), beforeDrag.position, dragPlaneNormal, dragStartPlanePoint);
            }
            if (started) {
                dragFeedback.clear();
                grabMouse();
                update();
                e->accept();
                return;
            }
            activeAxis = -1;
        }
        QString hit;
        auto world = pickPoint(e->position(), &hit);
        scene->select(hit);
        emit clicked(world, hit);
        update();
    }
}
void Viewport::mouseMoveEvent(QMouseEvent *e) {
    auto delta = e->position() - lastMouse;
    lastMouse = e->position();
    if (cameraButton != Qt::NoButton) {
        if (cameraButton == Qt::MiddleButton) {
            auto view = (target - eye()).normalized(), right = QVector3D::crossProduct(view, {0, 1, 0}).normalized(),
                 up = QVector3D::crossProduct(right, view);
            float factor = distance * .00095f;
            target += right * float(-delta.x()) * factor + up * float(delta.y()) * factor;
        } else {
            const auto position = eye();
            yaw -= float(delta.x()) * .35f;
            pitch = std::clamp(pitch + float(delta.y()) * .35f, -85.f, 85.f);
            // Right-button look rotates in place; Alt+LMB keeps orbit behavior.
            if (cameraButton == Qt::RightButton)
                target += position - eye();
        }
        update();
        return;
    }
    if (dragging()) {
        if (!scene->current())
            return;
        Object o = beforeDrag;
        const auto directions = transformAxes(beforeDrag);
        const bool shift = e->modifiers() & Qt::ShiftModifier;
        const bool forceSnap = snapEnabled || (e->modifiers() & Qt::ControlModifier);
        const auto total = e->position() - startMouse;
        const float worldLength =
            std::max(.1f, (eye() - beforeDrag.position).length() * 2 * tanf(22.5f * pi / 180) * 92 /
                              std::max(1, height()));
        auto axisAmount = [&](int axis) {
            const auto c = project(beforeDrag.position);
            const auto end = project(beforeDrag.position + directions[axis] * worldLength);
            const auto d = end - c;
            const double length = std::hypot(d.x(), d.y());
            const float pixels = length > 8 ? float((total.x() * d.x() + total.y() * d.y()) / length)
                                            : float(total.x() - total.y());
            return pixels * float(worldLength / std::max(8.0, length));
        };
        if (activeAxis >= 0 && activeAxis <= 2) {
            const int axis = activeAxis;
            if (tool == Tool::Move) {
                const auto direction = directions[axis];
                float amount = axisAmount(axis);
                QVector3D next = beforeDrag.position + direction * amount;
                if (forceSnap) {
                    const float step = shift ? snapTranslate * .1f : snapTranslate;
                    if (transformSpace == TransformSpace::World && axis >= 0) {
                        float value = next[axis];
                        value = std::round(value / step) * step;
                        next = beforeDrag.position + direction * amount;
                        next[axis] = value;
                    } else
                        next = beforeDrag.position + direction * (std::round(amount / step) * step);
                } else if (shift)
                    next = beforeDrag.position + direction * (amount * .15f);
                o.position = next;
                const float signedDelta = QVector3D::dotProduct(next - beforeDrag.position, direction);
                setDragFeedback(QStringLiteral("%1 %2%3 m")
                                    .arg(axisLetter(axis))
                                    .arg(signedDelta >= 0 ? QStringLiteral("+") : QString())
                                    .arg(signedDelta, 0, 'f', 3));
            } else if (tool == Tool::Rotate) {
                const auto center = project(beforeDrag.position);
                const auto normal = directions[axis];
                auto ringVector = [&](QPointF point, bool &valid) {
                    QVector3D origin, ray;
                    if (!mouseRay(point, origin, ray)) {
                        valid = false;
                        return QVector3D{};
                    }
                    const float denominator = QVector3D::dotProduct(ray, normal);
                    valid = std::abs(denominator) > .03f;
                    if (!valid)
                        return QVector3D{};
                    const float t = QVector3D::dotProduct(beforeDrag.position - origin, normal) / denominator;
                    auto vector = origin + ray * t - beforeDrag.position;
                    valid = t > 0 && vector.lengthSquared() > 1e-8f;
                    return vector.normalized();
                };
                float angle = 0;
                bool firstValid, secondValid;
                const auto first = ringVector(startMouse, firstValid), second = ringVector(e->position(), secondValid);
                if (firstValid && secondValid)
                    angle = atan2f(QVector3D::dotProduct(normal, QVector3D::crossProduct(first, second)),
                                   QVector3D::dotProduct(first, second)) *
                            180 / pi;
                else {
                    const auto u = startMouse - center, v = e->position() - center;
                    angle = float((atan2(v.y(), v.x()) - atan2(u.y(), u.x())) * 180 / pi);
                    if (QLineF(center, startMouse).length() < 10)
                        angle = float(total.x() - total.y());
                }
                if (forceSnap) {
                    const float step = shift ? snapRotate * .1f : snapRotate;
                    angle = std::round(angle / step) * step;
                } else if (shift)
                    angle *= .15f;
                const auto rotation = QQuaternion::fromEulerAngles(beforeDrag.rotation);
                const auto localDelta = QQuaternion::fromAxisAndAngle(axes[axis], angle);
                const auto worldDelta = QQuaternion::fromAxisAndAngle(normal, angle);
                o.rotation = (transformSpace == TransformSpace::Local ? rotation * localDelta : worldDelta * rotation)
                                 .toEulerAngles();
                setDragFeedback(QStringLiteral("%1 %2°").arg(axisLetter(axis)).arg(angle, 0, 'f', 1));
            } else {
                float amount = axisAmount(axis);
                if (forceSnap) {
                    const float step = shift ? snapScale * .1f : snapScale;
                    o.scale[axis] = std::clamp(std::round((beforeDrag.scale[axis] * expf(amount / 90.f)) / step) * step,
                                               std::max(.001f, step), 10000.f);
                } else {
                    if (shift)
                        amount *= .15f;
                    o.scale[axis] = std::clamp(beforeDrag.scale[axis] * expf(amount / 90.f), .001f, 10000.f);
                }
                setDragFeedback(QStringLiteral("%1 × %2").arg(axisLetter(axis)).arg(o.scale[axis], 0, 'f', 3));
            }
        } else if (activeAxis >= 3 && activeAxis <= 5) {
            QVector3D hit;
            if (rayPlane(e->position(), beforeDrag.position, dragPlaneNormal, hit)) {
                const int normalAxis = activeAxis - 3;
                const int u = (normalAxis + 1) % 3, v = (normalAxis + 2) % 3;
                QVector3D movement = hit - dragStartPlanePoint;
                float du = QVector3D::dotProduct(movement, directions[u]);
                float dv = QVector3D::dotProduct(movement, directions[v]);
                if (forceSnap) {
                    const float step = shift ? snapTranslate * .1f : snapTranslate;
                    du = std::round(du / step) * step;
                    dv = std::round(dv / step) * step;
                } else if (shift) {
                    du *= .15f;
                    dv *= .15f;
                }
                o.position = beforeDrag.position + directions[u] * du + directions[v] * dv;
                setDragFeedback(QStringLiteral("%1%2  %3%4, %5%6 m")
                                    .arg(axisLetter(u), axisLetter(v))
                                    .arg(du >= 0 ? QStringLiteral("+") : QString())
                                    .arg(du, 0, 'f', 3)
                                    .arg(dv >= 0 ? QStringLiteral("+") : QString())
                                    .arg(dv, 0, 'f', 3));
            }
        } else if (activeAxis == 6) {
            if (tool == Tool::Scale) {
                float amount = float(total.x() - total.y());
                if (forceSnap) {
                    const float step = shift ? snapScale * .1f : snapScale;
                    const float factor = expf(amount / 120.f);
                    for (int a = 0; a < 3; ++a)
                        o.scale[a] = std::clamp(std::round((beforeDrag.scale[a] * factor) / step) * step,
                                                std::max(.001f, step), 10000.f);
                } else {
                    if (shift)
                        amount *= .15f;
                    const float factor = expf(amount / 120.f);
                    for (int a = 0; a < 3; ++a)
                        o.scale[a] = std::clamp(beforeDrag.scale[a] * factor, .001f, 10000.f);
                }
                setDragFeedback(QStringLiteral("均匀缩放 × %1").arg(o.scale.x(), 0, 'f', 3));
            } else {
                QVector3D hit;
                if (rayPlane(e->position(), beforeDrag.position, dragPlaneNormal, hit)) {
                    QVector3D movement = hit - dragStartPlanePoint;
                    if (forceSnap) {
                        const float step = shift ? snapTranslate * .1f : snapTranslate;
                        for (int a = 0; a < 3; ++a)
                            movement[a] = std::round(movement[a] / step) * step;
                    } else if (shift)
                        movement *= .15f;
                    o.position = beforeDrag.position + movement;
                    setDragFeedback(QStringLiteral("Δ %1, %2, %3 m")
                                        .arg(o.position.x() - beforeDrag.position.x(), 0, 'f', 3)
                                        .arg(o.position.y() - beforeDrag.position.y(), 0, 'f', 3)
                                        .arg(o.position.z() - beforeDrag.position.z(), 0, 'f', 3));
                }
            }
        } else if (activeAxis == 7) {
            const auto center = project(beforeDrag.position);
            const auto u = startMouse - center, v = e->position() - center;
            float angle = float((atan2(v.y(), v.x()) - atan2(u.y(), u.x())) * 180 / pi);
            if (forceSnap) {
                const float step = shift ? snapRotate * .1f : snapRotate;
                angle = std::round(angle / step) * step;
            } else if (shift)
                angle *= .15f;
            const auto normal = (eye() - beforeDrag.position).normalized();
            const auto rotation = QQuaternion::fromEulerAngles(beforeDrag.rotation);
            o.rotation = (QQuaternion::fromAxisAndAngle(normal, -angle) * rotation).toEulerAngles();
            setDragFeedback(QStringLiteral("视图轴 %1°").arg(angle, 0, 'f', 1));
        }
        scene->preview(o);
        return;
    }
    const int previousHover = hoverAxis;
    hoverAxis = hitHandle(e->position());
    // Hover picking is cheap for primitive scenes and skips heavy imported
    // meshes so large assets do not rebuild triangles on every mouse move.
    bool heavy = false;
    for (const auto &o : scene->objects)
        if (o.visible && o.shape == Shape::Mesh && o.mesh && o.mesh->indices.size() > 3000) {
            heavy = true;
            break;
        }
    QString hover;
    if (!heavy && !anchorPicking)
        hover = pick(e->position());
    if (hover != hoveredObject) {
        hoveredObject = hover;
        emit hoverChanged(hoveredObject);
        update();
    }
    if (previousHover != hoverAxis)
        update();
    if (hoverAxis >= 0)
        setCursor((hoverAxis == 0 || hoverAxis == 3) ? Qt::SizeHorCursor
                  : (hoverAxis == 1 || hoverAxis == 4) ? Qt::SizeVerCursor
                  : hoverAxis == 7                     ? Qt::CrossCursor
                                                       : Qt::SizeAllCursor);
    else if (!hover.isEmpty())
        setCursor(Qt::PointingHandCursor);
    else
        setCursor(Qt::ArrowCursor);
}
void Viewport::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() == cameraButton)
        stopCameraNavigation();
    if (e->button() == Qt::LeftButton && dragging()) {
        const int handle = activeAxis;
        activeAxis = -1;
        releaseMouse();
        const QString label = tool == Tool::Move     ? QStringLiteral("移动对象")
                              : tool == Tool::Rotate ? QStringLiteral("旋转对象")
                                                     : QStringLiteral("缩放对象");
        scene->finishPreview(beforeDrag, label);
        dragFeedback.clear();
        unsetCursor();
        update();
        Q_UNUSED(handle);
    }
}
void Viewport::mouseDoubleClickEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton || anchorPicking)
        return;
    const QString hit = pick(e->position());
    if (!hit.isEmpty()) {
        scene->select(hit);
        frameSelected();
    }
}
void Viewport::cancelDrag() {
    if (!dragging())
        return;
    activeAxis = -1;
    releaseMouse();
    scene->preview(beforeDrag);
    dragFeedback.clear();
    unsetCursor();
    update();
}
void Viewport::wheelEvent(QWheelEvent *e) {
    if (dragging())
        return;
    if (cameraButton == Qt::RightButton)
        flySpeed = std::clamp(flySpeed * expf(e->angleDelta().y() * .0015f), .05f, 1000.f);
    else
        distance = std::clamp(distance * expf(-e->angleDelta().y() * .001f), .15f, 1000.f);
    update();
    e->accept();
}
void Viewport::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) {
        stopCameraNavigation();
        cancelDrag();
        emit cancelled();
        return;
    }
    if (cameraButton == Qt::RightButton) {
        if (e->key() == Qt::Key_W || e->key() == Qt::Key_S || e->key() == Qt::Key_A || e->key() == Qt::Key_D ||
            e->key() == Qt::Key_Q || e->key() == Qt::Key_E || e->key() == Qt::Key_Shift) {
            if (!e->isAutoRepeat())
                flyKeys.insert(e->key());
            e->accept();
            return;
        }
        QOpenGLWidget::keyPressEvent(e);
        return;
    }
    if (e->key() == Qt::Key_F) {
        frameSelected();
        return;
    }
    if (e->key() == Qt::Key_Home) {
        if (e->modifiers() & Qt::ControlModifier)
            frameAll();
        else
            home();
        return;
    }
    if (e->key() == Qt::Key_W) {
        setTool(Tool::Move);
        return;
    }
    if (e->key() == Qt::Key_E) {
        setTool(Tool::Rotate);
        return;
    }
    if (e->key() == Qt::Key_R) {
        setTool(Tool::Scale);
        return;
    }
    if (e->key() == Qt::Key_L) {
        setTransformSpace(transformSpace == TransformSpace::World ? TransformSpace::Local : TransformSpace::World);
        emit message(QStringLiteral("变换坐标系：%1").arg(spaceLabel(transformSpace)));
        return;
    }
    if (e->key() == Qt::Key_G) {
        setSnapEnabled(!snapEnabled);
        emit message(snapEnabled ? QStringLiteral("网格吸附：开（%1 m / %2°）").arg(snapTranslate, 0, 'g', 3).arg(
                                      snapRotate, 0, 'g', 3)
                                : QStringLiteral("网格吸附：关"));
        return;
    }
    if (editingEnabled && scene->current() &&
        (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right || e->key() == Qt::Key_Up ||
         e->key() == Qt::Key_Down || e->key() == Qt::Key_PageUp || e->key() == Qt::Key_PageDown)) {
        const bool large = e->modifiers() & Qt::ShiftModifier;
        if (e->key() == Qt::Key_Left)
            nudgeSelection(0, -1, large);
        else if (e->key() == Qt::Key_Right)
            nudgeSelection(0, 1, large);
        else if (e->key() == Qt::Key_Up)
            nudgeSelection(2, -1, large);
        else if (e->key() == Qt::Key_Down)
            nudgeSelection(2, 1, large);
        else if (e->key() == Qt::Key_PageUp)
            nudgeSelection(1, 1, large);
        else if (e->key() == Qt::Key_PageDown)
            nudgeSelection(1, -1, large);
        return;
    }
    QOpenGLWidget::keyPressEvent(e);
}
void Viewport::keyReleaseEvent(QKeyEvent *e) {
    if (cameraButton == Qt::RightButton && flyKeys.contains(e->key())) {
        if (!e->isAutoRepeat())
            flyKeys.remove(e->key());
        e->accept();
        return;
    }
    QOpenGLWidget::keyReleaseEvent(e);
}
QJsonObject Viewport::cameraJson() const {
    return {{"target", QJsonArray{target.x(), target.y(), target.z()}},
            {"yaw", yaw},
            {"pitch", pitch},
            {"distance", distance},
            {"grid", showGrid}};
}
void Viewport::restoreCamera(const QJsonObject &j) {
    home();
    auto ar = j["target"].toArray();
    if (ar.size() == 3)
        target = {float(ar[0].toDouble()), float(ar[1].toDouble()), float(ar[2].toDouble())};
    yaw = float(j["yaw"].toDouble(38));
    pitch = std::clamp(float(j["pitch"].toDouble(27)), -85.f, 85.f);
    distance = std::clamp(float(j["distance"].toDouble(11)), .15f, 1000.f);
    showGrid = j["grid"].toBool(true);
    update();
}
} // namespace bagua
