#include "Cache.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace bagua::cache {
namespace {
QString selectRoot() {
    const QString applicationDir = QCoreApplication::applicationDirPath();
    if (!applicationDir.isEmpty()) {
        const QString portable = QDir(applicationDir).filePath(QStringLiteral("cache"));
        if (QDir().mkpath(portable)) {
            const QFileInfo info(portable);
            if (info.isDir() && info.isWritable())
                return portable;
        }
    }
    QString generic = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (generic.isEmpty())
        generic = QDir::tempPath();
    const QString fallback = QDir(generic).filePath(QStringLiteral("BaguaDT-cache"));
    QDir().mkpath(fallback);
    return fallback;
}
QString rootPath() {
    static const QString value = selectRoot();
    return value;
}
bool isInside(const QString &path, const QString &parent) {
    const QString normalized = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString base = QDir::cleanPath(QFileInfo(parent).absoluteFilePath());
    return normalized.startsWith(base + QLatin1Char('/'), Qt::CaseInsensitive);
}
} // namespace
QString root() { return rootPath(); }
QString ensure(const QString &category) {
    QDir base(rootPath());
    const QString path = base.filePath(category);
    if (!QDir().mkpath(path))
        return QDir::temp().filePath(QStringLiteral("BaguaDT-cache/") + category);
    return path;
}
QString temporaryTemplate(const QString &category, const QString &prefix) {
    return QDir(ensure(category)).filePath(prefix + QStringLiteral("-XXXXXX"));
}
void removeTree(const QString &path) {
    if (path.trimmed().isEmpty())
        return;
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir() || info.isSymLink())
        return;
    if (!isInside(path, root()) && !isInside(path, QCoreApplication::applicationDirPath()))
        return;
    QDir(path).removeRecursively();
}
void cleanupStale() {
    // prepare() runs before any workspace is created. Remove every previous
    // scratch directory; locked directories (another running instance) simply
    // fail to delete and are retried on the next start.
    for (const QString &category : {QStringLiteral("hsc"), QStringLiteral("import"), QStringLiteral("tests")}) {
        QDir directory(QDir(rootPath()).filePath(category));
        if (!directory.exists())
            continue;
        for (const QFileInfo &entry :
             directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time)) {
            removeTree(entry.absoluteFilePath());
        }
    }
}
void cleanupLegacyApplicationTemp() {
    const QString applicationDir = QCoreApplication::applicationDirPath();
    if (applicationDir.isEmpty())
        return;
    QDir directory(applicationDir);
    for (const QFileInfo &entry :
         directory.entryInfoList(QStringList{QStringLiteral("hsc-runtime-*"), QStringLiteral("hsc-probe-*")},
                                 QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time)) {
        removeTree(entry.absoluteFilePath());
    }
}
void prepare() {
    ensure(QStringLiteral("hsc"));
    ensure(QStringLiteral("import"));
    cleanupStale();
    cleanupLegacyApplicationTemp();
}
} // namespace bagua::cache
