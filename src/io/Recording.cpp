#include "Recording.h"
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QQuaternion>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace bagua {
namespace {
void require(bool v, const char *message) {
    if (!v)
        throw std::runtime_error(message);
}
void configure(QDataStream &s) {
    s.setByteOrder(QDataStream::LittleEndian);
    s.setFloatingPointPrecision(QDataStream::SinglePrecision);
}
void writeVector(QDataStream &s, QVector3D v) { s << v.x() << v.y() << v.z(); }
QVector3D readVector(QDataStream &s, bool positive = false) {
    float x, y, z;
    s >> x >> y >> z;
    QVector3D p(x, y, z);
    for (int a = 0; a < 3; ++a)
        require(std::isfinite(p[a]) && std::abs(p[a]) <= 1e9 && (!positive || p[a] >= .000001f),
                "Invalid frame coordinates");
    return p;
}
QByteArray encode(const QVector<Object> &objects) {
    QByteArray bytes;
    QDataStream s(&bytes, QIODevice::WriteOnly);
    configure(s);
    s << quint32(objects.size());
    for (const auto &o : objects) {
        auto id = o.id.toUtf8();
        s << quint32(id.size());
        s.writeRawData(id.constData(), id.size());
        for (auto v : {o.position, o.rotation, o.scale, o.dimensions})
            writeVector(s, v);
        s << quint8(o.visible ? 1 : 0);
        s << quint32(o.mesh ? o.mesh->positions.size() : 0);
        if (o.mesh)
            for (auto p : o.mesh->positions)
                writeVector(s, p);
    }
    return qCompress(bytes, 3);
}
QVector<Object> decode(QByteArray bytes, const Scene &initial) {
    require(bytes.size() >= 4, "Truncated compressed frame");
    const auto *p = reinterpret_cast<const unsigned char *>(bytes.constData());
    quint32 expected = (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | p[3];
    require(expected > 0 && expected <= 128 * 1024 * 1024, "Frame exceeds decoded size limit");
    bytes = qUncompress(bytes);
    require(bytes.size() == expected, "Corrupt compressed frame");
    QDataStream s(bytes);
    configure(s);
    quint32 count;
    s >> count;
    require(count == quint32(initial.objects.size()), "Frame object count mismatch");
    auto objects = initial.objects;
    for (auto &o : objects) {
        quint32 len;
        s >> len;
        require(len > 0 && len <= 1024, "Invalid object ID length");
        QByteArray id(int(len), Qt::Uninitialized);
        require(s.readRawData(id.data(), len) == int(len) && QString::fromUtf8(id) == o.id, "Frame object ID mismatch");
        o.position = readVector(s);
        o.rotation = readVector(s);
        o.scale = readVector(s, true);
        o.dimensions = readVector(s, true);
        quint8 visible;
        s >> visible;
        require(visible <= 1, "Invalid frame visibility");
        o.visible = visible;
        quint32 vertices;
        s >> vertices;
        require(vertices == quint32(o.mesh ? o.mesh->positions.size() : 0), "Deformation topology mismatch");
        if (vertices) {
            auto mesh = std::make_shared<MeshData>(*o.mesh);
            for (auto &v : mesh->positions)
                v = readVector(s);
            o.mesh = mesh;
        }
    }
    require(s.status() == QDataStream::Ok && s.atEnd(), "Truncated or oversized frame");
    return objects;
}
} // namespace
void Recording::begin(const Scene &scene, QJsonObject provenance) {
    initial.loadJson(scene.json());
    metadata = std::move(provenance);
    times.clear();
    entries.clear();
    captured.clear();
    path.clear();
    cache.clear();
    dirty = true;
    append(0, scene);
}
bool Recording::append(double t, const Scene &state, QString *error) {
    if (!path.isEmpty() || !std::isfinite(t) || t < 0 || (!times.isEmpty() && t <= times.last()) ||
        state.objects.size() != initial.objects.size() || times.size() >= 1000000) {
        if (error)
            *error = QStringLiteral("记录时间或对象拓扑不合法");
        return false;
    }
    for (qsizetype i = 0; i < state.objects.size(); ++i) {
        auto &a = state.objects[i];
        auto &b = initial.objects[i];
        if (a.id != b.id || bool(a.mesh) != bool(b.mesh) ||
            (a.mesh && a.mesh->positions.size() != b.mesh->positions.size())) {
            if (error)
                *error = QStringLiteral("录制期间对象拓扑发生变化");
            return false;
        }
    }
    times.push_back(t);
    captured.push_back(state.objects);
    dirty = true;
    return true;
}
bool Recording::frame(int index, QVector<Object> &state, QString *error) const {
    try {
        require(index >= 0 && index < times.size(), "Frame index out of range");
        if (!captured.isEmpty()) {
            state = captured[index];
            return true;
        }
        if (auto *value = cache.object(index)) {
            state = *value;
            return true;
        }
        QFile f(path);
        require(f.open(QIODevice::ReadOnly), "Cannot open recording data");
        const auto e = entries[index];
        require(f.seek(qint64(e.offset)), "Cannot seek frame");
        auto bytes = f.read(e.size);
        require(bytes.size() == e.size, "Truncated recording frame");
        auto next = decode(bytes, initial);
        cache.insert(index, new QVector<Object>(next));
        state = std::move(next);
        return true;
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return false;
    }
}
bool Recording::sample(double t, QVector<Object> &state, QString *error, bool interpolate) const {
    if (times.isEmpty() || !std::isfinite(t)) {
        if (error)
            *error = QStringLiteral("没有可回放的记录");
        return false;
    }
    t = std::clamp(t, times.first(), times.last());
    int a = int(std::upper_bound(times.cbegin(), times.cend(), t) - times.cbegin()) - 1;
    a = std::max(0, a);
    QVector<Object> first, second;
    if (!frame(a, first, error))
        return false;
    if (!interpolate || a + 1 == times.size() || t == times[a]) {
        state = std::move(first);
        return true;
    }
    if (!frame(a + 1, second, error))
        return false;
    float alpha = float((t - times[a]) / (times[a + 1] - times[a]));
    for (qsizetype i = 0; i < first.size(); ++i) {
        auto &o = first[i];
        const auto &b = second[i];
        o.position = o.position * (1 - alpha) + b.position * alpha;
        o.rotation = QQuaternion::slerp(QQuaternion::fromEulerAngles(o.rotation),
                                        QQuaternion::fromEulerAngles(b.rotation), alpha)
                         .toEulerAngles();
        o.scale = o.scale * (1 - alpha) + b.scale * alpha;
        o.dimensions = o.dimensions * (1 - alpha) + b.dimensions * alpha;
        if (o.mesh) {
            auto mesh = std::make_shared<MeshData>(*o.mesh);
            for (qsizetype k = 0; k < mesh->positions.size(); ++k)
                mesh->positions[k] = mesh->positions[k] * (1 - alpha) + b.mesh->positions[k] * alpha;
            o.mesh = mesh;
        }
    }
    state = std::move(first);
    return true;
}
bool Recording::load(const QString &filename, QString *error) {
    try {
        QFile f(filename);
        require(f.open(QIODevice::ReadOnly), "Cannot open recording");
        require(f.size() <= 8ll * 1024 * 1024 * 1024 && f.read(8) == "BGREC001",
                "Invalid recording magic or file size");
        QDataStream s(&f);
        configure(s);
        quint32 headerSize;
        s >> headerSize;
        require(headerSize > 0 && headerSize <= 256 * 1024 * 1024, "Invalid recording header length");
        QJsonParseError pe;
        auto header = QJsonDocument::fromJson(f.read(headerSize), &pe).object();
        require(pe.error == QJsonParseError::NoError && header.value("format") == "baguadt.recording" &&
                    header.value("version").toInt() == 1,
                "Unsupported recording format");
        Scene scene;
        QString message;
        require(scene.loadJson(header.value("scene").toObject(), &message), qPrintable(message));
        quint32 count;
        s >> count;
        require(count > 0 && count <= 1000000, "Invalid frame count");
        QVector<double> nextTimes;
        QVector<Entry> nextEntries;
        quint64 end = quint64(f.pos()) + count * 20ull;
        for (quint32 i = 0; i < count; ++i) {
            s.setFloatingPointPrecision(QDataStream::DoublePrecision);
            double t;
            quint64 offset;
            quint32 size;
            s >> t >> offset >> size;
            s.setFloatingPointPrecision(QDataStream::SinglePrecision);
            require(std::isfinite(t) && t >= 0 && (i == 0 ? t == 0 : t > nextTimes.last()) && offset == end &&
                        size >= 4 && size <= 128 * 1024 * 1024 && offset + size <= quint64(f.size()),
                    "Invalid recording index");
            nextTimes.push_back(t);
            nextEntries.push_back({offset, size});
            end = offset + size;
        }
        require(s.status() == QDataStream::Ok && end == quint64(f.size()), "Truncated recording index");
        // Decode boundary frames before changing an existing document.
        for (int i : {0, int(count) - 1}) {
            f.seek(qint64(nextEntries[i].offset));
            decode(f.read(nextEntries[i].size), scene);
        }
        initial.loadJson(scene.json());
        metadata = header.value("metadata").toObject();
        times = std::move(nextTimes);
        entries = std::move(nextEntries);
        captured.clear();
        cache.clear();
        path = QFileInfo(filename).absoluteFilePath();
        dirty = false;
        return true;
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return false;
    }
}
bool Recording::save(const QString &filename, QString *error) {
    try {
        require(!times.isEmpty(), "No recording frames");
        auto header = QJsonDocument(QJsonObject{{"format", "baguadt.recording"},
                                                {"version", 1},
                                                {"scene", initial.json()},
                                                {"metadata", metadata}})
                          .toJson(QJsonDocument::Compact);
        QSaveFile f(filename);
        require(f.open(QIODevice::WriteOnly), "Cannot create recording file");
        f.write("BGREC001", 8);
        QDataStream s(&f);
        configure(s);
        s << quint32(header.size());
        f.write(header);
        s << quint32(times.size());
        const qint64 table = f.pos();
        QByteArray blank(int(times.size() * 20), 0);
        f.write(blank);
        QVector<Entry> written;
        for (int i = 0; i < times.size(); ++i) {
            QVector<Object> objects;
            QString message;
            require(frame(i, objects, &message), qPrintable(message));
            auto bytes = encode(objects);
            written.push_back({quint64(f.pos()), quint32(bytes.size())});
            require(f.write(bytes) == bytes.size(), "Cannot write recording frame");
        }
        require(f.seek(table), "Cannot write recording index");
        s.setFloatingPointPrecision(QDataStream::DoublePrecision);
        for (int i = 0; i < times.size(); ++i)
            s << times[i] << written[i].offset << written[i].size;
        require(s.status() == QDataStream::Ok && f.commit(), "Cannot commit recording file");
        path = QFileInfo(filename).absoluteFilePath();
        entries = std::move(written);
        captured.clear();
        cache.clear();
        dirty = false;
        return true;
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return false;
    }
}
} // namespace bagua
