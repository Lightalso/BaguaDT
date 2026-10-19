#include "Viewport.h"
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
static const std::array<QColor, 3> axisColors = {QColor("#c73f46"), QColor("#168343"), QColor("#246bc9")};
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
void Viewport::upload(Mesh &m, const QVector<Vertex> &data) {
    if (!m.vao)
        glGenVertexArrays(1, &m.vao);
    glBindVertexArray(m.vao);
    if (!m.vbo)
        glGenBuffers(1, &m.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(Vertex), data.constData(), GL_STATIC_DRAW);
    m.count = int(data.size());
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
                if (o.shape == Shape::Mesh && o.mesh) {
                    auto &cached = importedMeshes[o.id];
                    if (cached.source != o.mesh) {
                        upload(cached.gpu, bagua::geometry(o));
                        cached.source = o.mesh;
                    }
                    draw(cached.gpu, o.matrix(), c, true);
                } else
                    draw(meshes[int(o.shape)], o.matrix(), c, true);
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
QString Viewport::pick(QPointF p) const {
    auto inv = vp().inverted();
    float x = 2 * float(p.x()) / width() - 1, y = 1 - 2 * float(p.y()) / height();
    auto near = inv * QVector4D(x, y, -1, 1), far = inv * QVector4D(x, y, 1, 1);
    auto origin = near.toVector3DAffine(), dir = (far.toVector3DAffine() - origin).normalized();
    float closest = std::numeric_limits<float>::infinity();
    QString id;
    for (const auto &o : scene->objects)
        if (o.visible) {
            float d = intersect(o, origin, dir);
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
std::array<QPainterPath, 3> Viewport::handlePaths() const {
    std::array<QPainterPath, 3> paths;
    auto *o = scene->current();
    if (!o || !o->visible || !editingEnabled)
        return paths;
    bool visible;
    auto c = project(o->position, &visible);
    if (!visible)
        return paths;
    float length = gizmoLength();
    for (int a = 0; a < 3; ++a) {
        if (tool != Tool::Rotate) {
            paths[a].moveTo(c);
            const auto axis =
                tool == Tool::Scale ? QQuaternion::fromEulerAngles(o->rotation).rotatedVector(axes[a]) : axes[a];
            paths[a].lineTo(project(o->position + axis * length));
        } else {
            for (int i = 0; i <= 96; ++i) {
                float t = 2 * pi * i / 96;
                auto w = o->position + length * (axes[(a + 1) % 3] * cosf(t) + axes[(a + 2) % 3] * sinf(t));
                auto point = project(w);
                if (i == 0)
                    paths[a].moveTo(point);
                else
                    paths[a].lineTo(point);
            }
        }
    }
    return paths;
}
QPointF Viewport::handlePoint(int a) const {
    const auto paths = handlePaths();
    return paths[a].pointAtPercent(tool == Tool::Rotate ? .125 : .78);
}
int Viewport::hitHandle(QPointF p) const {
    auto paths = handlePaths();
    int best = -1;
    double distance = 1e20;
    for (int a = 0; a < 3; ++a) {
        QPainterPathStroker stroke;
        stroke.setWidth(15);
        if (!stroke.createStroke(paths[a]).contains(p))
            continue;
        double d = 1e20;
        for (int i = 0; i <= 96; ++i) {
            auto q = paths[a].pointAtPercent(double(i) / 96);
            d = std::min(d, QLineF(q, p).length());
        }
        if (d < distance) {
            distance = d;
            best = a;
        }
    }
    return best;
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
    if (auto *o = scene->current(); o && o->visible) {
        std::array<QVector3D, 8> corners;
        const auto bounds = localBounds(*o);
        for (int i = 0; i < 8; ++i)
            corners[i] = o->matrix().map({(i & 1) ? bounds.second.x() : bounds.first.x(),
                                          (i & 2) ? bounds.second.y() : bounds.first.y(),
                                          (i & 4) ? bounds.second.z() : bounds.first.z()});
        p.setPen(QPen(QColor("#0078d4"), 1.2, Qt::DashLine));
        for (int i = 0; i < 8; ++i)
            for (int bit : {1, 2, 4})
                if (!(i & bit)) {
                    bool a, b;
                    auto x = project(corners[i], &a), y = project(corners[i | bit], &b);
                    if (a && b)
                        p.drawLine(x, y);
                }
        auto paths = handlePaths();
        auto center = project(o->position);
        for (int a = 0; a < 3; ++a) {
            QColor c = (a == activeAxis || a == hoverAxis) ? QColor("#b77900") : axisColors[a];
            p.setPen(QPen(QColor("#ffffff"), 4.5, Qt::SolidLine, Qt::RoundCap));
            p.drawPath(paths[a]);
            p.setPen(QPen(c, 2, Qt::SolidLine, Qt::RoundCap));
            p.drawPath(paths[a]);
            if (paths[a].isEmpty())
                continue;
            if (tool != Tool::Rotate) {
                auto end = paths[a].pointAtPercent(1);
                p.setBrush(c);
                if (tool == Tool::Scale)
                    p.drawRect(QRectF(end - QPointF(4, 4), QSizeF(8, 8)));
                else {
                    QLineF l(center, end);
                    if (l.length() > 1) {
                        auto unit = (end - center) / l.length(), side = QPointF(-unit.y(), unit.x());
                        p.drawPolygon(
                            QPolygonF{end + unit * 3, end - unit * 10 + side * 4, end - unit * 10 - side * 4});
                    }
                }
                p.drawText(end + QPointF(8, -7), QString("XYZ").mid(a, 1));
            }
        }
    }
    p.setPen(QColor("#465262"));
    p.drawText(10, 19, QStringLiteral("透视 · 米 · Y 轴向上"));
    p.drawText(QRect(width() - 110, 5, 100, 20), Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("%1 个对象").arg(scene->objects.size()));
    p.setPen(QColor("#66717e"));
    p.setFont(QFont("Microsoft YaHei UI", 8));
    p.drawText(
        10, height() - 10,
        cameraButton == Qt::RightButton
            ? QStringLiteral("漫游：WASD 移动 · Q/E 下/上 · Shift 加速 · 滚轮调速  %1 m/s").arg(flySpeed, 0, 'f', 1)
            : QStringLiteral("右键 + WASD/QE 漫游 · Alt+左键环绕 · 中键平移 · F 聚焦"));
    // Camera orientation indicator.
    auto view = (target - eye()).normalized();
    auto right = QVector3D::crossProduct(view, {0, 1, 0}).normalized(), up = QVector3D::crossProduct(right, view);
    QPointF c(width() - 40, height() - 42);
    for (int a = 0; a < 3; ++a) {
        QPointF e = c + QPointF(QVector3D::dotProduct(axes[a], right) * 20, -QVector3D::dotProduct(axes[a], up) * 20);
        p.setPen(QPen(axisColors[a], 2));
        p.drawLine(c, e);
        p.drawText(e + QPointF(4, 0), QString("XYZ").mid(a, 1));
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
    emit toolChanged();
    update();
}
void Viewport::frameSelected() {
    if (auto *o = scene->current()) {
        const auto bounds = localBounds(*o);
        target = o->matrix().map((bounds.first + bounds.second) * .5f);
        distance = std::clamp(((bounds.second - bounds.first) * o->dimensions * o->scale).length() * 1.7f, 1.f, 500.f);
    }
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
        activeAxis = hitHandle(e->position());
        if (activeAxis >= 0 && scene->current()) {
            beforeDrag = *scene->current();
            grabMouse();
        } else
            scene->select(pick(e->position()));
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
        Object o = beforeDrag;
        auto total = e->position() - startMouse;
        float fine = (e->modifiers() & Qt::ShiftModifier) ? .15f : 1.f;
        if (tool == Tool::Rotate) {
            auto c = project(beforeDrag.position);
            auto u = startMouse - c, v = e->position() - c;
            float angle = float((atan2(v.y(), v.x()) - atan2(u.y(), u.x())) * 180 / pi);
            auto facing = QVector3D::dotProduct(axes[activeAxis], eye() - beforeDrag.position);
            if (facing > 0)
                angle = -angle;
            if (QLineF(c, startMouse).length() < 10)
                angle = float(total.x() - total.y());
            // Intersect the mouse rays with the actual rotation-ring plane.
            auto ringVector = [&](QPointF point, bool &valid) {
                const auto inv = vp().inverted();
                auto far =
                    inv * QVector4D(2 * float(point.x()) / width() - 1, 1 - 2 * float(point.y()) / height(), 1, 1);
                const auto ray = (far.toVector3DAffine() - eye()).normalized();
                const float denominator = QVector3D::dotProduct(ray, axes[activeAxis]);
                valid = std::abs(denominator) > .03f;
                if (!valid)
                    return QVector3D{};
                const float t = QVector3D::dotProduct(beforeDrag.position - eye(), axes[activeAxis]) / denominator;
                auto vector = eye() + ray * t - beforeDrag.position;
                valid = t > 0 && vector.lengthSquared() > 1e-8f;
                return vector.normalized();
            };
            bool firstValid, secondValid;
            auto first = ringVector(startMouse, firstValid), second = ringVector(e->position(), secondValid);
            if (firstValid && secondValid)
                angle = atan2f(QVector3D::dotProduct(axes[activeAxis], QVector3D::crossProduct(first, second)),
                               QVector3D::dotProduct(first, second)) *
                        180 / pi;
            o.rotation = (QQuaternion::fromAxisAndAngle(axes[activeAxis], angle * fine) *
                          QQuaternion::fromEulerAngles(beforeDrag.rotation))
                             .toEulerAngles();
        } else {
            const auto axis = tool == Tool::Scale
                                  ? QQuaternion::fromEulerAngles(beforeDrag.rotation).rotatedVector(axes[activeAxis])
                                  : axes[activeAxis];
            const float worldLength = std::max(.1f, (eye() - beforeDrag.position).length() * 2 *
                                                        tanf(22.5f * pi / 180) * 92 / std::max(1, height()));
            auto c = project(beforeDrag.position), end = project(beforeDrag.position + axis * worldLength);
            auto d = end - c;
            double length = std::hypot(d.x(), d.y());
            float amount =
                length > 8 ? float((total.x() * d.x() + total.y() * d.y()) / length) : float(total.x() - total.y());
            amount *= fine;
            if (tool == Tool::Move)
                o.position[activeAxis] += amount * worldLength / float(std::max(8., length));
            else
                o.scale[activeAxis] = std::clamp(beforeDrag.scale[activeAxis] * expf(amount / 90), .001f, 10000.f);
        }
        scene->preview(o);
        return;
    }
    hoverAxis = hitHandle(e->position());
    setCursor(hoverAxis >= 0 ? Qt::SizeAllCursor : Qt::ArrowCursor);
    update();
}
void Viewport::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() == cameraButton) {
        stopCameraNavigation();
    }
    if (e->button() == Qt::LeftButton && dragging()) {
        activeAxis = -1;
        releaseMouse();
        scene->finishPreview(beforeDrag, tool == Tool::Move     ? QStringLiteral("移动对象")
                                         : tool == Tool::Rotate ? QStringLiteral("旋转对象")
                                                                : QStringLiteral("缩放对象"));
        update();
    }
}
void Viewport::cancelDrag() {
    if (!dragging())
        return;
    activeAxis = -1;
    releaseMouse();
    scene->preview(beforeDrag);
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
