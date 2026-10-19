#include "MainWindow.h"
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSettings>
#include <QSpinBox>
#include <algorithm>
#include <cmath>
namespace bagua {
bool MainWindow::replayActive() const {
    auto *d = activeDocument();
    return d && d->replayActive;
}
bool MainWindow::replayRunning() const {
    auto *d = activeDocument();
    return d && d->replayPlaying;
}
double MainWindow::replayTime() const {
    auto *d = activeDocument();
    return d ? d->replayTime : 0;
}
const Scene *MainWindow::replayPreview() const {
    auto *d = activeDocument();
    return d && d->replayActive ? &d->replayPreview : nullptr;
}
bool MainWindow::editingLocked() const { return simulationActive() || replayActive(); }
namespace {
QString settingsPath() { return QCoreApplication::applicationDirPath() + "/BaguaDT.ini"; }
} // namespace
void MainWindow::loadPhysicsSettings() {
    QSettings s(settingsPath(), QSettings::IniFormat);
    solverOptions.enabled = s.value("physics/hsc", false).toBool();
    solverOptions.maxRank = std::clamp(s.value("physics/rank", 48).toInt(), 1, 256);
    solverOptions.neumannTerms = std::clamp(s.value("physics/terms", 5).toInt(), 1, 32);
    solverOptions.omega = std::clamp(s.value("physics/omega", .7).toDouble(), .01, 1.);
    solverOptions.tolerance = std::clamp(s.value("physics/tolerance", 1e-8).toDouble(), 1e-10, 1e-3);
    solverOptions.nativeAdaptiveInverse = s.value("physics/nativeAdaptiveInverse", false).toBool();
    solverOptions.linearTolerance = std::clamp(s.value("physics/nativeLinearTolerance", 1e-4).toDouble(),1e-8,1e-2);
    solverOptions.maxIterations = std::clamp(s.value("physics/maxIterations",2000).toInt(),1,10000);
}
void MainWindow::setPhysicsOptions(HscOptions options) {
    solverOptions = options;
    if (console)
        log(QStringLiteral("HSC 设置：%1；在重置后再次开始模拟时生效，运行中的求解器不变。")
                .arg(options.enabled ? QStringLiteral("开启") : QStringLiteral("关闭")));
}
void MainWindow::showPhysicsSettings() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("物理模拟设置"));
    dialog.setMinimumWidth(470);
    QFormLayout form(&dialog);
    QCheckBox enabled(QStringLiteral("启用 HSC 原生物理系统（CUDA）"));
    enabled.setObjectName("hscEnabled");
    enabled.setChecked(solverOptions.enabled);
    form.addRow(QStringLiteral("刚柔耦合求解"), &enabled);
    QSpinBox rank, terms, iterations;
    iterations.setRange(1,10000);iterations.setValue(solverOptions.maxIterations);
    rank.setRange(1, 256);
    rank.setValue(solverOptions.maxRank);
    terms.setRange(1, 32);
    terms.setValue(solverOptions.neumannTerms);
    QDoubleSpinBox omega, tolerance;
    omega.setRange(.01, 1);
    omega.setSingleStep(.05);
    omega.setValue(solverOptions.omega);
    tolerance.setDecimals(8);
    tolerance.setRange(1e-8, 1e-2);
    tolerance.setValue(solverOptions.linearTolerance);
    QCheckBox adaptive(QStringLiteral("自适应延长刚体 Neumann 逆近似"));
    adaptive.setChecked(solverOptions.nativeAdaptiveInverse);
    form.addRow(QStringLiteral("ACA 秩上限"), &rank);
    form.addRow(QStringLiteral("Neumann 项数"), &terms);
    form.addRow(QStringLiteral("Jacobi ω"), &omega);
    form.addRow(QStringLiteral("Schur PCG 相对残差"), &tolerance);
    form.addRow(QStringLiteral("PCG 迭代上限"), &iterations);
    form.addRow(&adaptive);
    QLabel scope(QStringLiteral("开启：12 自由度 ABD、弹性体 FEM、IPC/CCD、Newton 与 CUDA HSC。\n关闭：Chrono 原有刚体 / FEM / 接触 / 关节计算链。\n此开关替换整套物理模块，两侧的物理模型并不相同。\n默认使用固定项数 Neumann；自适应延长是复现增强选项。\n回放只读取记录，不受开关影响；修改后请重置并重新开始模拟。"));
    scope.setWordWrap(true);
    form.addRow(&scope);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    auto options = solverOptions;
    options.enabled = enabled.isChecked();
    options.maxRank = rank.value();
    options.neumannTerms = terms.value();
    options.omega = omega.value();
    options.linearTolerance = tolerance.value();
    options.maxIterations = iterations.value();
    options.nativePipeline = true;
    options.nativeAdaptiveInverse = adaptive.isChecked();
    setPhysicsOptions(options);
    QSettings s(settingsPath(), QSettings::IniFormat);
    s.setValue("physics/hsc", options.enabled);
    s.setValue("physics/rank", options.maxRank);
    s.setValue("physics/terms", options.neumannTerms);
    s.setValue("physics/omega", options.omega);
    s.setValue("physics/nativeLinearTolerance",options.linearTolerance);
    s.setValue("physics/nativeAdaptiveInverse",options.nativeAdaptiveInverse);
    s.setValue("physics/maxIterations",options.maxIterations);
    s.sync();
    if (s.status() != QSettings::NoError)
        log(QStringLiteral("无法写入 BaguaDT.ini；物理设置仅在本次会话生效。"));
}
} // namespace bagua
