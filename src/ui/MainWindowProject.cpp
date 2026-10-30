#include "MainWindow.h"
#include "ProjectFile.h"
#include "ProjectManagerDialog.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>
#include <algorithm>

namespace bagua {
namespace {
QString projectSettingsPath() {
    return QCoreApplication::applicationDirPath() + QStringLiteral("/BaguaDT.ini");
}
QString absoluteProjectPath(const QString &path) { return QFileInfo(path).absoluteFilePath(); }
bool samePath(const QString &a, const QString &b) {
#ifdef Q_OS_WIN
    return absoluteProjectPath(a).compare(absoluteProjectPath(b), Qt::CaseInsensitive) == 0;
#else
    return absoluteProjectPath(a) == absoluteProjectPath(b);
#endif
}
QString defaultProjectPath(const QString &projectPath) {
    QString directory;
    if (!projectPath.isEmpty())
        directory = QFileInfo(projectPath).absolutePath();
    auto writable = [](const QString &path) {
        if (path.isEmpty() || !QFileInfo(path).isDir())
            return false;
        QTemporaryFile probe(QDir(path).filePath(QStringLiteral("baguadt-write-XXXXXX")));
        return probe.open();
    };
    if (!writable(directory))
        directory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (!writable(directory))
        directory = QDir::homePath();
    if (!writable(directory))
        directory = QDir::tempPath();
    return QDir(directory).filePath(QStringLiteral("BaguaDT-Project.bgp"));
}
} // namespace

QStringList MainWindow::recentProjects() const {
    QSettings settings(projectSettingsPath(), QSettings::IniFormat);
    const bool initialized = settings.contains(QStringLiteral("projects/recent"));
    QStringList result = settings.value(QStringLiteral("projects/recent")).toStringList();
    QStringList cleaned;
    for (const auto &path : result) {
        if (path.trimmed().isEmpty())
            continue;
        const QString absolute = absoluteProjectPath(path);
        if (std::none_of(cleaned.cbegin(), cleaned.cend(), [&](const QString &existing) { return samePath(existing, absolute); }))
            cleaned.push_back(absolute);
    }
    // A first run still has a useful project manager: offer the bundled .bgp
    // examples. Once the user has a recent list (even an intentionally empty
    // one), removing an example stays removed.
    if (!initialized && cleaned.isEmpty()) {
        for (const QString &relative :
             {QStringLiteral("examples/hsc-vehicle/HSC-Vehicle.bgp"), QStringLiteral("examples/HSC/HSC.bgp")}) {
            for (const QString &root : {QCoreApplication::applicationDirPath(), QDir::currentPath()}) {
                const QString candidate = QFileInfo(QDir(root).filePath(relative)).absoluteFilePath();
                if (QFileInfo::exists(candidate) &&
                    std::none_of(cleaned.cbegin(), cleaned.cend(),
                                 [&](const QString &existing) { return samePath(existing, candidate); })) {
                    cleaned.push_back(candidate);
                    break;
                }
            }
        }
    }
    return cleaned;
}
void MainWindow::rememberProject(const QString &path) {
    if (path.trimmed().isEmpty())
        return;
    QSettings settings(projectSettingsPath(), QSettings::IniFormat);
    QStringList recent = settings.value(QStringLiteral("projects/recent")).toStringList();
    recent.erase(std::remove_if(recent.begin(), recent.end(),
                                [&](const QString &existing) {
                                    return existing.trimmed().isEmpty() || samePath(existing, path);
                                }),
                 recent.end());
    recent.prepend(absoluteProjectPath(path));
    while (recent.size() > 20)
        recent.removeLast();
    settings.setValue(QStringLiteral("projects/recent"), recent);
    settings.sync();
}
void MainWindow::forgetProject(const QString &path) {
    QSettings settings(projectSettingsPath(), QSettings::IniFormat);
    QStringList recent = settings.value(QStringLiteral("projects/recent")).toStringList();
    recent.erase(std::remove_if(recent.begin(), recent.end(),
                                [&](const QString &existing) { return samePath(existing, path); }),
                 recent.end());
    settings.setValue(QStringLiteral("projects/recent"), recent);
    settings.sync();
}
void MainWindow::showProjectManager() {
    ProjectManagerDialog dialog(recentProjects(), projectPath, this);
    connect(&dialog, &ProjectManagerDialog::openProjectRequested, this, [this](const QString &path) {
        QTimer::singleShot(0, this, [this, path] {
            if (isProjectFile(path))
                openProject(path);
            else
                openPath(path);
        });
    });
    connect(&dialog, &ProjectManagerDialog::createProjectRequested, this,
            [this] { QTimer::singleShot(0, this, [this] { createProjectFromTabs(); }); });
    connect(&dialog, &ProjectManagerDialog::removeRecentRequested, this, &MainWindow::forgetProject);
    dialog.exec();
}
bool MainWindow::showStartupProjectManager() {
    // The launcher is the first visible UI on a normal interactive start. It
    // runs before any scene tab is created, so an empty document cannot flash
    // behind the dialog or be mistaken for an opened project.
    while (true) {
        ProjectManagerDialog dialog(recentProjects(), projectPath, this);
        QString selectedPath;
        bool create = false;
        connect(&dialog, &ProjectManagerDialog::openProjectRequested, this,
                [&](const QString &path) { selectedPath = path; });
        connect(&dialog, &ProjectManagerDialog::createProjectRequested, this, [&] { create = true; });
        connect(&dialog, &ProjectManagerDialog::removeRecentRequested, this, &MainWindow::forgetProject);
        if (dialog.exec() != QDialog::Accepted)
            return false;
        if (create) {
            if (sceneTabs->count() == 0)
                newScene(false);
            if (createProjectFromTabs())
                return true;
            return false;
        }
        if (selectedPath.isEmpty())
            return false;
        const bool opened = isProjectFile(selectedPath) ? openProject(selectedPath) : openPath(selectedPath);
        if (opened)
            return true;
        log(QStringLiteral("项目打开失败，请重新选择：") + QDir::toNativeSeparators(selectedPath));
    }
}
bool MainWindow::createProjectFromTabs() {
    if (sceneTabs->count() == 0)
        newScene(false);
    const QString suggested = defaultProjectPath(projectPath);
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("从当前页签新建项目"), suggested,
                                                QStringLiteral("BaguaDT 项目 (*.bgp)"));
    if (path.isEmpty())
        return false;
    path = withProjectSuffix(path);
    const QFileInfo target(path);
    if (target.exists() && target.isDir()) {
        QMessageBox::warning(this, QStringLiteral("无法创建项目"),
                             QStringLiteral("所选路径是文件夹，请指定一个 .bgp 文件名：\n%1")
                                 .arg(QDir::toNativeSeparators(path)));
        return false;
    }
    QDir parent(target.absolutePath());
    if (!parent.exists() && !QDir().mkpath(parent.absolutePath())) {
        QMessageBox::warning(this, QStringLiteral("无法创建项目"),
                             QStringLiteral("无法创建项目目录：\n%1").arg(QDir::toNativeSeparators(parent.absolutePath())));
        return false;
    }
    if (!QFileInfo(parent.absolutePath()).isDir()) {
        QMessageBox::warning(this, QStringLiteral("无法创建项目"),
                             QStringLiteral("项目目录不存在：\n%1").arg(QDir::toNativeSeparators(parent.absolutePath())));
        return false;
    }
    QTemporaryFile writeProbe(QDir(parent.absolutePath()).filePath(QStringLiteral("baguadt-write-XXXXXX")));
    if (!writeProbe.open()) {
        QMessageBox::warning(this, QStringLiteral("无法创建项目"),
                             QStringLiteral("项目目录不可写：\n%1\n\n请在文档目录或其他可写位置创建项目。")
                                 .arg(QDir::toNativeSeparators(parent.absolutePath())));
        return false;
    }
    writeProbe.close();
    QString error;
    if (!saveProject(path, &error)) {
        if (error.isEmpty())
            error = QStringLiteral("无法写入项目文件。");
        if (!property("nonInteractive").toBool())
            QMessageBox::warning(this, QStringLiteral("无法创建项目"),
                                 error + QStringLiteral("\n\n目标位置：") + QDir::toNativeSeparators(path));
        return false;
    }
    log(QStringLiteral("项目已创建：") + QDir::toNativeSeparators(projectPath));
    return true;
}
bool MainWindow::saveCurrentProject() {
    if (projectPath.isEmpty())
        return createProjectFromTabs();
    QString error;
    if (!saveProject(projectPath, &error)) {
        if (!property("nonInteractive").toBool())
            QMessageBox::warning(this, QStringLiteral("保存项目失败"),
                                 error + QStringLiteral("\n\n目标位置：") + QDir::toNativeSeparators(projectPath));
        return false;
    }
    log(QStringLiteral("项目已保存：") + QDir::toNativeSeparators(projectPath));
    return true;
}
} // namespace bagua
