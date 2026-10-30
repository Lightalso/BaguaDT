// Headless validation uses the editor's actual scene bridge, physics and recording code.
#include "Physics.h"
#include "Recording.h"
#include "Cache.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <limits>
using namespace bagua;
namespace {
QByteArray sha(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return h.result().toHex();
}
bool writeJson(QString path, QJsonObject json) {
    QSaveFile f(path);
    auto bytes = QJsonDocument(json).toJson();
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
}
struct Bounds {
    QVector3D lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    void add(QVector3D p) {
        for (int k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], p[k]);
            hi[k] = std::max(hi[k], p[k]);
        }
    }
};
Bounds bounds(const Object &o) {
    Bounds b;
    if (o.mesh)
        for (auto p : o.mesh->positions)
            b.add(o.matrix().map(p));
    return b;
}
double volume(QVector3D a, QVector3D b, QVector3D c, QVector3D d) {
    return double(QVector3D::dotProduct(b - a, QVector3D::crossProduct(c - a, d - a)));
}
} // namespace
int main(int argc, char **argv) {
    if (!qEnvironmentVariableIsSet("BAGUA_HSC_QUIET"))
        qputenv("BAGUA_HSC_QUIET", "1");
    QCoreApplication app(argc, argv);
    app.setApplicationName("BaguaDT");
    app.setOrganizationName("BaguaDT");
    cache::prepare();
    auto args = app.arguments();
    if (args.size() < 3) {
        qCritical("Usage: BaguaVehicleRunner SCENE OUTPUT_DIRECTORY [MAX_FRAMES=600]");
        return 2;
    }
    QString output = QDir(args[2]).absolutePath();
    if (QFile::exists(output + "/status.json")) {
        qCritical("Output already contains a run; use a new directory");
        return 2;
    }
    QDir().mkpath(output);
    const int maximum = args.size() > 3 ? args[3].toInt() : 600;
    if (maximum < 1 || maximum > 20000)
        return 2;
    Scene source;
    QString error;
    if (!source.load(args[1], &error)) {
        qCritical().noquote() << error;
        return 2;
    }
    QElapsedTimer elapsed;
    elapsed.start();
    Physics physics;
    HscOptions options;
    options.enabled = true;
    QJsonObject status{{"status", "INITIALIZING"},
                       {"source", QFileInfo(args[1]).absoluteFilePath()},
                       {"sceneSha256", QString(sha(args[1]))},
                       {"backendSha256", QString(sha(app.applicationDirPath() + "/uipc_backend_cuda.dll"))},
                       {"dt", source.timeStep},
                       {"simulation", source.simulation}};
    writeJson(output + "/status.json", status);
    if (!physics.start(source, &error, options)) {
        status["status"] = "FAILED";
        status["error"] = error;
        writeJson(output + "/status.json", status);
        return 3;
    }
    Recording recording;
    recording.begin(physics.preview, {{"producer", physics.solverDescription()},
                                       {"hscEnabled",true},
                                       {"simulation",source.simulation},
                                      {"sampling", "lock-step fixed timestep"},
                                      {"integrationStep", source.timeStep},
                                      {"sceneSha256", status["sceneSha256"]}});
    QFile metrics(output + "/metrics.jsonl");
    if (!metrics.open(QIODevice::WriteOnly))
        return 4;
    double initialObstacleMax = -1e30;
    for (int i = 0; i < 3; ++i) {
        for (QString suffix : {"_rim", "_tire"}) {
            auto *o = physics.preview.find("loose_" + QString::number(i) + suffix);
            if (!o) {
                qCritical("Missing full vehicle obstacle");
                return 2;
            }
            initialObstacleMax = std::max(initialObstacleMax, double(bounds(*o).hi.x()));
        }
    }
    double previous[4] = {-1e30, -1e30, -1e30, -1e30};
    bool crossed[4] = {}, contactSeen = false;
    int clearFrames = 0;
    double peakBead = 0, peakGuide = 0, peakHinge = 0, peakTravel = 0, peakDrive = 0, minRatio = 1e30;
    bool passed = false, stopped = false;
    status["status"] = "RUNNING";
    status["initializationSeconds"] = elapsed.elapsed() / 1000.;
    writeJson(output + "/status.json", status);
    for (int frame = 1; frame <= maximum; ++frame) {
        if (QFile::exists(output + "/STOP")) {
            stopped = true;
            error = "Stopped at a frame boundary by request";
            break;
        }
        if (!physics.step(1, &error))
            break;
        if (!recording.append(physics.time(), physics.preview, &error))
            break;
        Bounds obstacles[3];
        QVector3D centers[3];
        double obstacleMax = -1e30, vehicleMin = 1e30;
        for (const auto &o : physics.preview.objects) {
            if (o.id == "ground")
                continue;
            auto b = bounds(o);
            if (o.id.startsWith("loose_")) {
                int i = o.id.mid(6, 1).toInt();
                obstacles[i].add(b.lo);
                obstacles[i].add(b.hi);
                if (o.id.endsWith("_rim"))
                    centers[i] = o.anchorMatrix().map(QVector3D());
                obstacleMax = std::max(obstacleMax, double(b.hi.x()));
            } else
                vehicleMin = std::min(vehicleMin, double(b.lo.x()));
        }
        contactSeen |= physics.contacts() > 0;
        QJsonArray crossings;
        for (int i = 0; i < 4; ++i) {
            auto *rim = physics.preview.find("wheel_" + QString::number(i) + "_rim");
            if (!rim) {
                error = "Missing mounted wheel";
                break;
            }
            auto center = rim->anchorMatrix().map(QVector3D());
            int lane = (i == 0 || i == 2) ? 0 : 2;
            double relative = center.x() - centers[lane].x();
            if (previous[i] < 0 && relative >= 0 && center.z() >= obstacles[lane].lo.z() &&
                center.z() <= obstacles[lane].hi.z() && center.y() > obstacles[lane].hi.y())
                crossed[i] = true;
            previous[i] = relative;
            if (crossed[i])
                crossings.append(i);
        }
        for (const auto &s : physics.linkStats()) {
            if (s.id.endsWith("_bead"))
                peakBead = std::max(peakBead, s.error);
            if (s.type == "prismatic") {
                peakGuide = std::max(peakGuide, s.error);
                peakTravel = std::max(peakTravel, std::abs(s.distance));
            }
            if (s.type == "revolute") {
                peakHinge = std::max(peakHinge, s.error);
                peakDrive = std::max(peakDrive, std::abs(std::remainder(s.angle - s.aim, 2 * 3.141592653589793)));
            }
        }
        for (const auto &o : physics.preview.objects)
            if (o.deformable && o.mesh) {
                auto *rest = source.find(o.id);
                auto &t = o.mesh->tetrahedra;
                for (int j = 0; j < t.size(); j += 4) {
                    auto a = o.matrix().map(o.mesh->positions[t[j]]), b = o.matrix().map(o.mesh->positions[t[j + 1]]),
                         c = o.matrix().map(o.mesh->positions[t[j + 2]]),
                         d = o.matrix().map(o.mesh->positions[t[j + 3]]);
                    auto m = rest->matrix();
                    double v0 = volume(m.map(rest->mesh->positions[t[j]]), m.map(rest->mesh->positions[t[j + 1]]),
                                       m.map(rest->mesh->positions[t[j + 2]]), m.map(rest->mesh->positions[t[j + 3]]));
                    minRatio = std::min(minRatio, volume(a, b, c, d) / v0);
                }
            }
        double clearance = vehicleMin - std::max(initialObstacleMax, obstacleMax);
        clearFrames = (clearance >= .15 && crossings.size() == 4 && contactSeen) ? clearFrames + 1 : 0;
        passed = clearFrames >= 5;
        auto s = physics.hscStats();
        QJsonObject sample{{"frame", frame},
                           {"time", physics.time()},
                           {"wallSeconds", elapsed.elapsed() / 1000.},
                           {"clearance", clearance},
                           {"crossedWheels", crossings},
                           {"contactSeen", contactSeen},
                           {"peakBeadError", peakBead},
                           {"peakGuideError", peakGuide},
                           {"peakHingeError", peakHinge},
                           {"peakTravel", peakTravel},
                           {"peakDriveError", peakDrive},
                           {"minTetVolumeRatio", minRatio},
                           {"rank", s.rank},
                           {"residual", s.relativeResidual},
                           {"newtonIterations", s.newtonIterations},
                           {"lastLinearIterations", s.iterations},
                           {"lastLinearMilliseconds", s.milliseconds},
                           {"nonzeros", s.nonzeros},
                           {"factorNonzeros", s.factorNonzeros},
                           {"converged", s.converged},
                           {"inverseFallbacks", s.inverseFallbacks}};
        metrics.write(QJsonDocument(sample).toJson(QJsonDocument::Compact) + '\n');
        metrics.flush();
        status["latest"] = sample;
        status["status"] = passed ? "COMPLETED_FULL_PASS" : "RUNNING";
        writeJson(output + "/status.json", status);
        qInfo().noquote() << "Frame" << frame << "time" << physics.time() << "wall" << elapsed.elapsed() / 1000.
                          << "clearance" << clearance << "crossed" << crossings.size();
        if (minRatio <= 0) {
            error = "Tetrahedron inversion detected";
            passed = false;
            break;
        }
        if (passed)
            break;
    }
    if (!passed) {
        status["status"] = stopped ? "STOPPED" : error.isEmpty() ? "INCOMPLETE" : "FAILED";
        status["error"] = error;
    }
    const bool constraintsPassed =
        peakBead < 1e-4 && peakGuide < 1e-3 && peakHinge < 1e-3 && peakTravel < .151 && peakDrive < .02 && minRatio > 0;
    if (passed && !constraintsPassed) {
        status["status"] = "FAILED_VALIDATION";
        status["error"] = "Passage completed, but physical constraint thresholds were exceeded";
    }
    status["recordingFrames"] = recording.frameCount();
    status["elapsedSeconds"] = elapsed.elapsed() / 1000.;
    status["constraintsPassed"] = constraintsPassed;
    if (!recording.save(output + "/vehicle.brec", &error)) {
        status["recordingError"] = error;
        writeJson(output + "/status.json", status);
        return 4;
    }
    Recording reopened;
    bool verified=reopened.load(output+"/vehicle.brec",&error) && reopened.frameCount()==recording.frameCount();
    QVector<Object> decoded;
    for(int i=0;verified && i<reopened.frameCount();++i)verified=reopened.frame(i,decoded,&error) && decoded.size()==physics.preview.objects.size();
    status["recordingVerified"]=verified;
    status["recordingSha256"]=QString(sha(output+"/vehicle.brec"));
    if(!verified){status["status"]="FAILED_RECORDING";status["recordingError"]=error;}
    writeJson(output+"/vehicle.bagua.project.json",{{"format","baguadt.project"},{"version",1},{"name","HSC vehicle / "+status["status"].toString()},{"scenes",QJsonArray{QJsonObject{{"name","HSC vehicle"},{"recording","vehicle.brec"},{"mode","replay"}}}}});
    writeJson(output + "/status.json", status);
    return passed && constraintsPassed && verified ? 0 : 3;
}
