#include "Backends.h"
#include "Geometry.h"
#include "Physics.h"
#include <QQuaternion>
#include <cmath>
#ifdef BAGUA_HAS_CHRONO
#include "chrono/ChVersion.h"
#include "chrono/physics/ChBody.h"
#include "chrono/physics/ChSystemNSC.h"
#endif
#ifdef BAGUA_HAS_CUDA
extern "C" int baguaCudaBounds(const float *matrices, int count, float *bounds, char *info, int infoSize);
#endif
namespace bagua {
#ifdef BAGUA_HAS_CUDA
static QMatrix4x4 boundsMatrix(const Object &o) {
    auto m = o.matrix();
    const auto bounds = localBounds(o);
    m.translate((bounds.first + bounds.second) * .5f);
    m.scale(bounds.second - bounds.first);
    return m;
}
#endif
BackendReport validateBackends(const Scene &scene) {
    BackendReport r;
#ifdef BAGUA_HAS_CUDA
    r.cudaCompiled = true;
    QVector<float> matrices, bounds;
    for (const auto &o : scene.objects) {
        auto m = boundsMatrix(o);
        for (int i = 0; i < 16; ++i)
            matrices.push_back(m.constData()[i]);
    }
    bounds.resize(scene.objects.size() * 6);
    char info[512] = {};
    int code = baguaCudaBounds(matrices.constData(), int(scene.objects.size()), bounds.data(), info, sizeof(info));
    r.cuda = QString::fromUtf8(info);
    r.cudaAvailable = code >= 0;
    r.cudaPassed = code == 0;
    if (code == 0) {
        for (int i = 0; i < scene.objects.size(); ++i) {
            QVector3D lo(1e20f, 1e20f, 1e20f), hi(-1e20f, -1e20f, -1e20f);
            auto m = boundsMatrix(scene.objects[i]);
            for (int c = 0; c < 8; ++c) {
                auto v = m.map({(c & 1) ? .5f : -.5f, (c & 2) ? .5f : -.5f, (c & 4) ? .5f : -.5f});
                for (int a = 0; a < 3; ++a) {
                    lo[a] = std::min(lo[a], v[a]);
                    hi[a] = std::max(hi[a], v[a]);
                }
            }
            for (int a = 0; a < 3; ++a)
                if (std::abs(bounds[i * 6 + a] - lo[a]) > 1e-4f * (1 + std::abs(lo[a])) ||
                    std::abs(bounds[i * 6 + a + 3] - hi[a]) > 1e-4f * (1 + std::abs(hi[a])))
                    r.cudaPassed = false;
        }
        r.cuda +=
            r.cudaPassed ? QStringLiteral("；GPU 包围盒计算与 CPU 对照通过") : QStringLiteral("；GPU/CPU 对照失败");
    }
#else
    r.cuda = QStringLiteral("未编译 CUDA 后端（仅编辑器模式）");
#endif
#ifdef BAGUA_HAS_CHRONO
    r.chronoCompiled = true;
    try {
        Physics physics;
        QString error;
        r.chronoPassed = physics.start(scene, &error) && physics.step(1, &error);
        r.bodies = int(physics.preview.objects.size());
        r.chrono =
            r.chronoPassed
                ? QString("Chrono %1：%2 个刚体，Bullet 碰撞与 NSC 动力学步进通过").arg(CHRONO_VERSION).arg(r.bodies)
                : QStringLiteral("Chrono 自检失败：") + error;
    } catch (const std::exception &e) {
        r.chrono = QString::fromUtf8(e.what());
    }
#else
    r.chrono = QStringLiteral("未编译 Chrono 后端（仅编辑器模式）");
#endif
    return r;
}
} // namespace bagua
