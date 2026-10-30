#pragma once
#include "Scene.h"
#include <QCache>
namespace bagua {
// Indexed, independently compressed frames: opening a long run does not load every mesh into memory.
class Recording {
public:
    Scene initial;
    QJsonObject metadata;
    QVector<double> times;
    bool dirty = false;
    bool load(const QString &path, QString *error = nullptr);
    bool save(const QString &path, QString *error = nullptr);
    void begin(const Scene &scene, QJsonObject provenance = {});
    bool append(double time, const Scene &state, QString *error = nullptr);
    bool sample(double time, QVector<Object> &state, QString *error = nullptr, bool interpolate = true) const;
    bool frame(int index, QVector<Object> &state, QString *error = nullptr) const;
    int frameCount() const { return int(times.size()); }
    double duration() const { return times.isEmpty() ? 0 : times.last(); }
    QString sourcePath() const { return path; }

private:
    struct Entry {
        quint64 offset;
        quint32 size;
    };
    QVector<Entry> entries;
    QVector<QVector<Object>> captured;
    QString path;
    mutable QCache<int, QVector<Object>> cache{3};
};
} // namespace bagua
