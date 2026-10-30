#include "MainWindow.h"
#include "ProjectFile.h"
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStatusBar>
#include <algorithm>

namespace bagua {
namespace {
QString normalizedScenePath(const QString &path) {
    const QFileInfo info(path);
    const auto canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}
bool sameScenePath(const QString &a, const QString &b) {
    if (a.isEmpty() || b.isEmpty())
        return false;
#ifdef Q_OS_WIN
    return normalizedScenePath(a).compare(normalizedScenePath(b), Qt::CaseInsensitive) == 0;
#else
    return normalizedScenePath(a) == normalizedScenePath(b);
#endif
}
} // namespace

bool MainWindow::savePath(const QString &path) {
    finishInspectorEdit();
    viewport->cancelDrag();
    auto *doc = activeDocument();
    if (!doc)
        return false;
    const QString resolved = normalizedScenePath(path);
    for (const auto &other : documents)
        if (other.get() != doc && sameScenePath(other->filePath, resolved)) {
            QMessageBox::warning(
                this, QStringLiteral("无法保存"),
                QStringLiteral("该文件已在页签“%1”中打开，请切换到该页签，或另选保存位置。").arg(other->title));
            return false;
        }
    scene->view = viewport->cameraJson();
    QString error;
    if (!scene->save(resolved, &error)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), error);
        log(QStringLiteral("保存失败：") + error);
        return false;
    }
    doc->filePath = resolved;
    doc->title = QFileInfo(resolved).fileName();
    updateTitle();
    log(QStringLiteral("已保存：") + path);
    return true;
}
bool MainWindow::openPath(const QString &path) {
    if (path.endsWith(".brec", Qt::CaseInsensitive))
        return openRecording(path);
    if (isProjectFile(path))
        return openProject(path);
    finishInspectorEdit();
    viewport->cancelDrag();
    const QString resolved = normalizedScenePath(path);
    for (const auto &doc : documents)
        if (sameScenePath(doc->filePath, resolved)) {
            sceneTabs->setCurrentIndex(indexOf(doc.get()));
            return true;
        }
    auto model = std::make_unique<Scene>();
    QString error;
    if (!model->load(resolved, &error)) {
        if(!property("nonInteractive").toBool())QMessageBox::warning(this, QStringLiteral("打开失败"), error);
        log(QStringLiteral("打开失败：") + error);
        return false;
    }
    addDocument(std::move(model), resolved, QFileInfo(resolved).fileName());
    log(QStringLiteral("已打开：") + path);
    return true;
}
bool MainWindow::save(bool as) {
    QString path = activeDocument()->filePath;
    if (as || path.isEmpty())
        path = QFileDialog::getSaveFileName(this, QStringLiteral("保存场景"),
                                            path.isEmpty() ? "untitled.bagua.json" : path, "BaguaDT (*.bagua.json)");
    if (path.isEmpty())
        return false;
    if (!path.endsWith(".json", Qt::CaseInsensitive))
        path += ".bagua.json";
    return savePath(path);
}
bool MainWindow::confirmDiscard() {
    finishInspectorEdit();
    endRecording();
    auto *doc = activeDocument();
    QVector<int> unsaved;
    if (doc)
        for (int i = 0; i < int(doc->recordings.size()); ++i)
            if (doc->recordings[size_t(i)]->dirty && doc->recordings[size_t(i)]->frameCount())
                unsaved.push_back(i);
    if (!unsaved.isEmpty()) {
        auto answer = QMessageBox::question(
            this, QStringLiteral("保存录制记录"),
            QStringLiteral("当前场景有 %1 条未保存的回放记录，是否逐条保存？").arg(unsaved.size()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (answer == QMessageBox::Cancel)
            return false;
        if (answer == QMessageBox::Save)
            for (int index : unsaved)
                if (!exportRecordingAt(index))
                    return false;
    }
    if (scene->undo.isClean())
        return true;
    auto answer =
        QMessageBox::question(this, QStringLiteral("保存场景"),
                              QStringLiteral("场景“%1”有未保存的修改，是否保存？").arg(activeDocument()->title),
                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save)
        return save();
    return true;
}
void MainWindow::closeEvent(QCloseEvent *e) {
    if (simulationStarting || simulationStepWaiting) {
        statusBar()->showMessage(QStringLiteral("仿真正在启动或单步计算，请稍候再退出。"), 3000);
        e->ignore();
        return;
    }
    finishInspectorEdit();
    viewport->cancelDrag();
    auto *previous = activeDocument();
    for (int i = 0; i < sceneTabs->count(); ++i) {
        sceneTabs->setCurrentIndex(i);
        if (!confirmDiscard()) {
            sceneTabs->setCurrentIndex(indexOf(previous));
            e->ignore();
            return;
        }
    }
    e->accept();
}
} // namespace bagua
