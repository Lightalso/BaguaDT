#pragma once
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace bagua {
// A resolved scene entry from a .bgp project manifest. Paths are absolute and
// checked to remain inside the project directory.
struct ProjectSceneInfo {
    QString name;
    QString mode = QStringLiteral("edit");
    QString sceneRelative, scenePath;
    QStringList recordingRelative, recordingPaths;
    QString activeRecordingRelative, activeRecordingPath;
    double time = 0;
    bool sceneExists = false;
    int missingRecordings = 0;
    int recordingCount = 0;
};

struct ProjectInfo {
    QString path, name, error;
    QDateTime modified;
    QVector<ProjectSceneInfo> scenes;
    bool valid = false;
    int missingScenes = 0;
};

// .bgp is the project suffix. The legacy .bagua.project.json is still
// accepted when opening old projects so a converted workspace is not locked in.
bool isProjectFile(const QString &path);
QString withProjectSuffix(const QString &path);
ProjectInfo readProjectInfo(const QString &path);
} // namespace bagua
