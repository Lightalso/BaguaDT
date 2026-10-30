#include "MainWindow.h"
#include "Cache.h"
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QProcess>
#include <QProgressDialog>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
namespace bagua {
void MainWindow::importHscVehicle() {
    const auto source = QFileDialog::getExistingDirectory(this, QStringLiteral("选择 HSC_reimpl 原复现项目"),
                                                          "D:/Collection/HSC_reimpl");
    if (source.isEmpty())
        return;
    bool ok = false;
    const auto chosen = QInputDialog::getItem(this, QStringLiteral("整车物理案例"),
                                              QStringLiteral("安装轮胎 E=1e10 Pa；选择地面轮胎材料"),
                                              {"ground_1e8", "ground_1e6"}, 0, false, &ok);
    if (!ok)
        return;
    auto destination = QFileDialog::getSaveFileName(this, QStringLiteral("保存可仿真的整车场景"),
                                                    chosen + ".bagua.json", "BaguaDT scene (*.bagua.json)");
    if (destination.isEmpty())
        return;
    if (!destination.endsWith(".bagua.json"))
        destination += ".bagua.json";
    QString script = QCoreApplication::applicationDirPath() + "/tools/ImportHSCVehicle.py";
    if (!QFileInfo::exists(script))
        script = QDir::currentPath() + "/scripts/ImportHSCVehicle.py";
    QString python = qEnvironmentVariable("BAGUA_PYTHON");
    if (python.isEmpty())
        python = "C:/Users/Lightalso/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe";
    if (!QFileInfo::exists(python))
        python = QStandardPaths::findExecutable("python");
    if (python.isEmpty() || !QFileInfo::exists(script)) {
        log(QStringLiteral("整车导入需要 Python + NumPy；可通过 BAGUA_PYTHON 指定解释器。"));
        return;
    }
    QTemporaryDir staging{cache::temporaryTemplate(QStringLiteral("import"), QStringLiteral("vehicle"))};
    if (!staging.isValid()) {
        log(QStringLiteral("无法创建导入缓存目录"));
        return;
    }
    QProcess process;
    QProgressDialog progress(QStringLiteral("读取原始资产、重建四面体轮胎与装配关系…"), QStringLiteral("取消"), 0, 0,
                             this);
    progress.setWindowModality(Qt::WindowModal);
    connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &progress, &QProgressDialog::accept);
    connect(&progress, &QProgressDialog::canceled, &process, &QProcess::kill);
    process.start(python, {script, "--source", source, "--output", staging.path(), "--case", chosen});
    if (!process.waitForStarted(5000)) {
        log(process.errorString());
        return;
    }
    progress.exec();
    process.waitForFinished(5000);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        log(QStringLiteral("整车导入失败：") + QString::fromUtf8(process.readAllStandardError()));
        return;
    }
    QFile converted(staging.filePath(chosen + ".bagua.json"));
    if (!converted.open(QIODevice::ReadOnly))
        return;
    auto bytes = converted.readAll();
    QSaveFile target(destination);
    if (!target.open(QIODevice::WriteOnly) || target.write(bytes) != bytes.size() || !target.commit()) {
        log(QStringLiteral("保存整车失败：") + target.errorString());
        return;
    }
    QFile provenance(staging.filePath(chosen + ".provenance.json"));
    if (provenance.open(QIODevice::ReadOnly)) {
        QSaveFile record(destination + ".provenance.json");
        auto provenanceBytes = provenance.readAll();
        if (record.open(QIODevice::WriteOnly)) {
            record.write(provenanceBytes);
            record.commit();
        }
    }
    if (openPath(destination)) {
        auto options = physicsOptions();
        options.enabled = true;
        setPhysicsOptions(options);
        log(QStringLiteral("整车资产与连接已导入。顶栏开始物理模拟，底部可逐步录制。"));
    }
}
} // namespace bagua
