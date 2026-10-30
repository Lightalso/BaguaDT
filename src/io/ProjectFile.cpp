#include "ProjectFile.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <algorithm>

namespace bagua {
bool isProjectFile(const QString &path) {
    return path.endsWith(QStringLiteral(".bgp"), Qt::CaseInsensitive) ||
           path.endsWith(QStringLiteral(".bagua.project.json"), Qt::CaseInsensitive);
}
QString withProjectSuffix(const QString &path) {
    if (path.endsWith(QStringLiteral(".bgp"), Qt::CaseInsensitive))
        return path;
    return path + QStringLiteral(".bgp");
}
ProjectInfo readProjectInfo(const QString &path) {
    ProjectInfo info;
    info.path = QFileInfo(path).absoluteFilePath();
    info.name = QFileInfo(info.path).completeBaseName();
    info.modified = QFileInfo(info.path).lastModified();
    QFile file(info.path);
    if (!file.open(QIODevice::ReadOnly)) {
        info.error = QStringLiteral("无法读取项目文件");
        return info;
    }
    if (file.size() > 4 * 1024 * 1024) {
        info.error = QStringLiteral("项目文件超过 4 MB 限制");
        return info;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        info.error = QStringLiteral("项目不是有效 JSON");
        return info;
    }
    const auto root = document.object();
    if (root.value("format") != QStringLiteral("baguadt.project") || root.value("version").toInt(-1) != 1) {
        info.error = QStringLiteral("不支持的 .bgp 项目格式或版本");
        return info;
    }
    const auto scenes = root.value("scenes").toArray();
    if (scenes.isEmpty() || scenes.size() > 2000) {
        info.error = QStringLiteral("项目场景列表为空或超过 2000 项");
        return info;
    }
    if (root.value("name").isString() && !root.value("name").toString().trimmed().isEmpty())
        info.name = root.value("name").toString().trimmed();
    const QDir base(QFileInfo(info.path).absolutePath());
    auto resolveInside = [&](const QString &relative, QString *absolute) {
        if (relative.isEmpty() || QDir::isAbsolutePath(relative))
            return false;
        const QString resolved = QDir::cleanPath(base.absoluteFilePath(relative));
        if (!resolved.startsWith(base.absolutePath() + "/", Qt::CaseInsensitive))
            return false;
        if (absolute)
            *absolute = resolved;
        return true;
    };
    for (const auto &value : scenes) {
        if (!value.isObject()) {
            info.error = QStringLiteral("项目场景条目必须是对象");
            return info;
        }
        const auto scene = value.toObject();
        ProjectSceneInfo entry;
        entry.name = scene.value("name").toString();
        if (entry.name.trimmed().isEmpty())
            entry.name = QStringLiteral("场景 %1").arg(info.scenes.size() + 1);
        entry.mode = scene.value("mode").toString(scene.contains("recording") ? QStringLiteral("replay")
                                                                            : QStringLiteral("edit"));
        if (entry.mode != QStringLiteral("edit") && entry.mode != QStringLiteral("replay")) {
            info.error = QStringLiteral("项目场景模式无效：") + entry.name;
            return info;
        }
        const double time = scene.value("time").toDouble(0);
        if (!std::isfinite(time) || time < 0) {
            info.error = QStringLiteral("项目回放时间无效：") + entry.name;
            return info;
        }
        entry.time = time;
        QStringList takeKeys;
        if (scene.contains("recordings")) {
            if (!scene.value("recordings").isArray()) {
                info.error = QStringLiteral("项目 recordings 必须是列表");
                return info;
            }
            for (const auto &take : scene.value("recordings").toArray()) {
                if (!take.isString()) {
                    info.error = QStringLiteral("项目 recording 条目必须是字符串");
                    return info;
                }
                takeKeys.push_back(take.toString());
            }
        } else if (scene.contains("recording")) {
            if (!scene.value("recording").isString()) {
                info.error = QStringLiteral("项目 recording 必须是字符串");
                return info;
            }
            takeKeys.push_back(scene.value("recording").toString());
        }
        if (takeKeys.size() > 256) {
            info.error = QStringLiteral("项目录制条数超限：") + entry.name;
            return info;
        }
        if (scene.contains("scene")) {
            if (!scene.value("scene").isString() ||
                !resolveInside(scene.value("scene").toString(), &entry.scenePath)) {
                info.error = QStringLiteral("项目场景路径越界或无效：") + entry.name;
                return info;
            }
            entry.sceneRelative = scene.value("scene").toString();
            entry.sceneExists = QFileInfo::exists(entry.scenePath);
            if (!entry.sceneExists)
                ++info.missingScenes;
        }
        for (const auto &relative : takeKeys) {
            QString absolute;
            if (!resolveInside(relative, &absolute)) {
                info.error = QStringLiteral("项目录制路径越界或无效：") + entry.name;
                return info;
            }
            entry.recordingRelative.push_back(relative);
            entry.recordingPaths.push_back(absolute);
            if (!QFileInfo::exists(absolute))
                ++entry.missingRecordings;
        }
        entry.recordingCount = int(entry.recordingPaths.size());
        if (scene.contains("recording") && scene.value("recording").isString()) {
            const QString named = scene.value("recording").toString();
            if (entry.recordingRelative.contains(named)) {
                entry.activeRecordingRelative = named;
                entry.activeRecordingPath = entry.recordingPaths[entry.recordingRelative.indexOf(named)];
            }
        }
        if (entry.activeRecordingRelative.isEmpty() && !entry.recordingRelative.isEmpty()) {
            entry.activeRecordingRelative = entry.recordingRelative.first();
            entry.activeRecordingPath = entry.recordingPaths.first();
        }
        if (entry.scenePath.isEmpty() && entry.recordingPaths.isEmpty()) {
            info.error = QStringLiteral("项目场景缺少场景或录制：") + entry.name;
            return info;
        }
        info.scenes.push_back(entry);
    }
    info.valid = true;
    return info;
}
} // namespace bagua
