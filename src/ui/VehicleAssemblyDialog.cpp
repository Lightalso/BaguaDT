#include "VehicleAssemblyDialog.h"
#include "Scene.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace bagua {
namespace {
QDoubleSpinBox *number(double value, double minimum, double maximum, int decimals = 3) {
    auto *box = new QDoubleSpinBox;
    box->setRange(minimum, maximum);
    box->setDecimals(decimals);
    box->setValue(value);
    box->setSingleStep(std::max((maximum - minimum) / 100., .001));
    return box;
}
QString matchObject(const Scene &scene, const QStringList &tokens) {
    for (const auto &token : tokens)
        for (const auto &object : scene.objects)
            if (object.name.contains(token, Qt::CaseInsensitive) ||
                object.id.contains(token, Qt::CaseInsensitive))
                return object.id;
    return {};
}
} // namespace

VehicleAssemblyDialog::VehicleAssemblyDialog(const Scene &scene, QWidget *parent)
    : QDialog(parent), scene(scene) {
    setObjectName(QStringLiteral("vehicleAssemblyDialog"));
    setWindowTitle(QStringLiteral("车辆四轮装配向导 — BaguaDT"));
    resize(980, 720);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 10);
    auto *title = new QLabel(QStringLiteral("车辆四轮装配向导"));
    title->setStyleSheet(QStringLiteral("font-size:15pt;font-weight:600;color:#17324d;"));
    root->addWidget(title);
    auto *hint = new QLabel(
        QStringLiteral("选择车架和每个车轮的转向节、轮辋、轮胎。向导会创建四轮悬挂（移动副 + 弹簧 + 转动副 + 轮辋/轮胎缝合）"
                       "与接触排除，并把选中的部件设为动态。松散轮胎只建立轮辋-轮胎缝合。"));
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#607080;"));
    root->addWidget(hint);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 4, 0);
    contentLayout->setSpacing(7);

    auto *chassisGroup = new QGroupBox(QStringLiteral("车架与基础"));
    auto *chassisForm = new QFormLayout(chassisGroup);
    chassisCombo = new QComboBox;
    chassisCombo->setObjectName(QStringLiteral("vehicleChassis"));
    addObjectItems(chassisCombo, scene);
    chassisForm->addRow(QStringLiteral("车架 / 底盘"), chassisCombo);
    enableHsc = new QCheckBox(QStringLiteral("生成后启用 HSC 原生后端"));
    enableHsc->setChecked(true);
    chassisForm->addRow(QString(), enableHsc);
    applyHscPreset = new QCheckBox(QStringLiteral("应用 HSC 车辆参数预设（重力、接触、Newton 迭代）"));
    applyHscPreset->setChecked(true);
    applyHscPreset->setObjectName(QStringLiteral("vehicleHscPreset"));
    chassisForm->addRow(QString(), applyHscPreset);
    contentLayout->addWidget(chassisGroup);

    auto *wheelGroup = new QGroupBox(QStringLiteral("四轮悬挂"));
    auto *wheelLayout = new QVBoxLayout(wheelGroup);
    wheelTable = new QTableWidget(4, 5);
    wheelTable->setObjectName(QStringLiteral("vehicleWheelTable"));
    wheelTable->setHorizontalHeaderLabels(
        {QStringLiteral("位置"), QStringLiteral("启用"), QStringLiteral("转向节"), QStringLiteral("轮辋"),
         QStringLiteral("轮胎（四面体）")});
    wheelTable->verticalHeader()->setVisible(false);
    wheelTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    wheelTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    for (int column = 2; column < 5; ++column)
        wheelTable->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Stretch);
    wheelTable->setMinimumHeight(160);
    const QStringList corners{QStringLiteral("前左 FL"), QStringLiteral("前右 FR"), QStringLiteral("后左 RL"),
                              QStringLiteral("后右 RR")};
    for (int row = 0; row < 4; ++row) {
        wheelTable->setItem(row, 0, new QTableWidgetItem(corners[row]));
        auto *enabled = new QCheckBox;
        enabled->setChecked(true);
        enabled->setObjectName(QStringLiteral("vehicleWheelEnabled%1").arg(row));
        wheelEnabled[size_t(row)] = enabled;
        wheelTable->setCellWidget(row, 1, enabled);
        for (int column = 2; column < 5; ++column) {
            auto *combo = new QComboBox;
            combo->setObjectName(QStringLiteral("vehicleWheel%1_%2").arg(row).arg(column));
            addObjectItems(combo, scene);
            wheelTable->setCellWidget(row, column, combo);
            if (column == 2)
                spindleCombos[size_t(row)] = combo;
            else if (column == 3)
                rimCombos[size_t(row)] = combo;
            else
                tireCombos[size_t(row)] = combo;
        }
    }
    wheelLayout->addWidget(wheelTable);
    auto *autoButton = new QPushButton(QStringLiteral("按名称自动匹配"));
    autoButton->setObjectName(QStringLiteral("vehicleAutoMatch"));
    autoButton->setToolTip(QStringLiteral("按 chassis、spindle_i、wheel_i_rim、wheel_i_tire 的名称匹配"));
    wheelLayout->addWidget(autoButton, 0, Qt::AlignLeft);
    contentLayout->addWidget(wheelGroup);

    auto *looseGroup = new QGroupBox(QStringLiteral("松散轮胎（可选，用于轮胎障碍）"));
    auto *looseLayout = new QVBoxLayout(looseGroup);
    looseTable = new QTableWidget(3, 4);
    looseTable->setObjectName(QStringLiteral("vehicleLooseTable"));
    looseTable->setHorizontalHeaderLabels(
        {QStringLiteral("序号"), QStringLiteral("启用"), QStringLiteral("轮辋"), QStringLiteral("轮胎")});
    looseTable->verticalHeader()->setVisible(false);
    looseTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    looseTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    looseTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    looseTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    looseTable->setMinimumHeight(130);
    for (int row = 0; row < 3; ++row) {
        looseTable->setItem(row, 0, new QTableWidgetItem(QString::number(row + 1)));
        auto *enabled = new QCheckBox;
        enabled->setChecked(true);
        enabled->setObjectName(QStringLiteral("vehicleLooseEnabled%1").arg(row));
        looseEnabled[size_t(row)] = enabled;
        looseTable->setCellWidget(row, 1, enabled);
        for (int column = 0; column < 2; ++column) {
            auto *combo = new QComboBox;
            combo->setObjectName(QStringLiteral("vehicleLoose%1_%2").arg(row).arg(column));
            addObjectItems(combo, scene);
            looseTable->setCellWidget(row, 2 + column, combo);
            if (column == 0)
                looseRimCombos[size_t(row)] = combo;
            else
                looseTireCombos[size_t(row)] = combo;
        }
    }
    looseLayout->addWidget(looseTable);
    contentLayout->addWidget(looseGroup);

    auto *params = new QGroupBox(QStringLiteral("悬挂与轮胎参数"));
    auto *grid = new QGridLayout(params);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(4);
    int column = 0;
    auto add = [&](const QString &label, QDoubleSpinBox *widget) {
        grid->addWidget(new QLabel(label), 0, column++);
        grid->addWidget(widget, 0, column++);
    };
    suspensionStrength = number(1e5, 1, 1e10, 1);
    suspensionStrength->setObjectName(QStringLiteral("vehicleSuspensionStrength"));
    suspensionDamping = number(4753.8923, 0, 1e9, 3);
    suspensionDamping->setObjectName(QStringLiteral("vehicleSuspensionDamping"));
    suspensionLower = number(-.15, -100, 0, 4);
    suspensionLower->setObjectName(QStringLiteral("vehicleSuspensionLower"));
    suspensionUpper = number(.15, 0, 100, 4);
    suspensionUpper->setObjectName(QStringLiteral("vehicleSuspensionUpper"));
    springStiffness = number(45374.0375, 0, 1e10, 3);
    springStiffness->setObjectName(QStringLiteral("vehicleSpringStiffness"));
    springRest = number(.46266297384269583, 0, 100, 6);
    springRest->setObjectName(QStringLiteral("vehicleSpringRest"));
    driveSpeed = number(2.0, -1000, 1000, 3);
    driveSpeed->setObjectName(QStringLiteral("vehicleDriveSpeed"));
    beadStiffness = number(1e8, 1, 1e12, 0);
    beadStiffness->setObjectName(QStringLiteral("vehicleBeadStiffness"));
    beadRadius = number(.25, .001, 10, 4);
    beadRadius->setObjectName(QStringLiteral("vehicleBeadRadius"));
    beadAnchors = new QSpinBox;
    beadAnchors->setRange(1, 256);
    beadAnchors->setValue(39);
    beadAnchors->setObjectName(QStringLiteral("vehicleBeadAnchors"));
    add(QStringLiteral("悬挂强度"), suspensionStrength);
    add(QStringLiteral("阻尼"), suspensionDamping);
    add(QStringLiteral("行程下限"), suspensionLower);
    add(QStringLiteral("行程上限"), suspensionUpper);
    column = 0;
    grid->addWidget(new QLabel(QStringLiteral("弹簧刚度")), 1, column++);
    grid->addWidget(springStiffness, 1, column++);
    grid->addWidget(new QLabel(QStringLiteral("弹簧静止长度")), 1, column++);
    grid->addWidget(springRest, 1, column++);
    grid->addWidget(new QLabel(QStringLiteral("驱动速度 rad/s")), 1, column++);
    grid->addWidget(driveSpeed, 1, column++);
    grid->addWidget(new QLabel(QStringLiteral("轮辋环状锚点数")), 2, 0);
    grid->addWidget(beadAnchors, 2, 1);
    grid->addWidget(new QLabel(QStringLiteral("环半径 m")), 2, 2);
    grid->addWidget(beadRadius, 2, 3);
    grid->addWidget(new QLabel(QStringLiteral("缝合刚度")), 2, 4);
    grid->addWidget(beadStiffness, 2, 5);
    auto *beadHint = new QLabel(
        QStringLiteral("轮辋已有锚点时直接使用；没有锚点时按局部 XY 平面生成环状锚点，并自动匹配最近的轮胎顶点。"));
    beadHint->setWordWrap(true);
    beadHint->setStyleSheet(QStringLiteral("color:#7a8a99;"));
    grid->addWidget(beadHint, 3, 0, 1, 6);
    contentLayout->addWidget(params);
    contentLayout->addStretch();
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("生成装配"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("vehicleBuild"));
    connect(buttons, &QDialogButtonBox::accepted, this, &VehicleAssemblyDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
    connect(autoButton, &QPushButton::clicked, this, &VehicleAssemblyDialog::autoMatch);
    autoMatch();
}

void VehicleAssemblyDialog::addObjectItems(QComboBox *combo, const Scene &scene) {
    combo->addItem(QStringLiteral("— 未选择 —"), QString{});
    for (const auto &object : scene.objects)
        combo->addItem(object.name, object.id);
}
void VehicleAssemblyDialog::autoMatch() {
    auto select = [](QComboBox *combo, const QString &id) {
        const int index = combo->findData(id);
        if (index >= 0)
            combo->setCurrentIndex(index);
    };
    select(chassisCombo, matchObject(scene, {QStringLiteral("chassis"), QStringLiteral("车架"), QStringLiteral("底盘")}));
    for (int i = 0; i < 4; ++i) {
        select(spindleCombos[size_t(i)],
               matchObject(scene, {QStringLiteral("spindle_%1").arg(i), QStringLiteral("spindle%1").arg(i)}));
        select(rimCombos[size_t(i)],
               matchObject(scene, {QStringLiteral("wheel_%1_rim").arg(i), QStringLiteral("rim_%1").arg(i),
                                   QStringLiteral("轮辋_%1").arg(i)}));
        select(tireCombos[size_t(i)],
               matchObject(scene, {QStringLiteral("wheel_%1_tire").arg(i), QStringLiteral("tire_%1").arg(i),
                                   QStringLiteral("轮胎_%1").arg(i)}));
    }
    for (int i = 0; i < 3; ++i) {
        select(looseRimCombos[size_t(i)],
               matchObject(scene, {QStringLiteral("loose_%1_rim").arg(i), QStringLiteral("loose_rim_%1").arg(i)}));
        select(looseTireCombos[size_t(i)],
               matchObject(scene, {QStringLiteral("loose_%1_tire").arg(i), QStringLiteral("loose_tire_%1").arg(i)}));
    }
}
VehicleAssemblyRequest VehicleAssemblyDialog::request() const {
    VehicleAssemblyRequest result;
    result.chassis = chassisCombo->currentData().toString();
    for (int i = 0; i < 4; ++i) {
        auto &wheel = result.wheels[size_t(i)];
        wheel.enabled = wheelEnabled[size_t(i)]->isChecked();
        wheel.spindle = spindleCombos[size_t(i)]->currentData().toString();
        wheel.rim = rimCombos[size_t(i)]->currentData().toString();
        wheel.tire = tireCombos[size_t(i)]->currentData().toString();
        if (!wheel.enabled)
            continue;
        if (!wheel.spindle.isEmpty() && !wheel.rim.isEmpty() &&
            (wheel.spindle == wheel.rim || wheel.spindle == wheel.tire || wheel.rim == wheel.tire)) {
            // Validated in accept(); keeping the request harmless if it is called directly.
            wheel.enabled = false;
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (!looseEnabled[size_t(i)]->isChecked())
            continue;
        const QString rim = looseRimCombos[size_t(i)]->currentData().toString();
        const QString tire = looseTireCombos[size_t(i)]->currentData().toString();
        if (!rim.isEmpty() && !tire.isEmpty() && rim != tire)
            result.loosePairs.push_back({rim, tire});
    }
    result.suspensionStrength = suspensionStrength->value();
    result.suspensionDamping = suspensionDamping->value();
    result.suspensionLower = suspensionLower->value();
    result.suspensionUpper = suspensionUpper->value();
    result.springStiffness = springStiffness->value();
    result.springRest = springRest->value();
    result.driveSpeed = driveSpeed->value();
    result.beadAnchors = beadAnchors->value();
    result.beadRadius = beadRadius->value();
    result.beadStiffness = beadStiffness->value();
    result.enableHsc = enableHsc->isChecked();
    result.applyHscPreset = applyHscPreset->isChecked();
    return result;
}
void VehicleAssemblyDialog::accept() {
    if (chassisCombo->currentData().toString().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("无法生成"), QStringLiteral("请选择车架 / 底盘。"));
        return;
    }
    int wheels = 0, loose = 0;
    for (int i = 0; i < 4; ++i) {
        if (!wheelEnabled[size_t(i)]->isChecked())
            continue;
        const QString spindle = spindleCombos[size_t(i)]->currentData().toString();
        const QString rim = rimCombos[size_t(i)]->currentData().toString();
        const QString tire = tireCombos[size_t(i)]->currentData().toString();
        if (spindle.isEmpty() || rim.isEmpty() || tire.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("无法生成"),
                                 QStringLiteral("四轮表中第 %1 行需要同时选择转向节、轮辋和轮胎。").arg(i + 1));
            return;
        }
        if (spindle == rim || spindle == tire || rim == tire) {
            QMessageBox::warning(this, QStringLiteral("无法生成"),
                                 QStringLiteral("四轮表中第 %1 行选择了重复对象。").arg(i + 1));
            return;
        }
        ++wheels;
    }
    for (int i = 0; i < 3; ++i) {
        if (!looseEnabled[size_t(i)]->isChecked())
            continue;
        const QString rim = looseRimCombos[size_t(i)]->currentData().toString();
        const QString tire = looseTireCombos[size_t(i)]->currentData().toString();
        if (!rim.isEmpty() && !tire.isEmpty() && rim != tire)
            ++loose;
    }
    if (wheels == 0 && loose == 0) {
        QMessageBox::warning(this, QStringLiteral("无法生成"),
                             QStringLiteral("至少选择一组完整车轮或一组松散轮胎。"));
        return;
    }
    QDialog::accept();
}
} // namespace bagua
