#include "MainWindow.h"
#include <QFormLayout>
#include <QGroupBox>
#include <QSpinBox>
namespace bagua {
QWidget *MainWindow::makeFemInspector() {
    auto *group = new QGroupBox(QStringLiteral("弹性体 · FEM"));
    auto *form = new QFormLayout(group);
    form->setContentsMargins(8, 8, 8, 8);
    form->setSpacing(4);
    femEnabled = new QCheckBox(QStringLiteral("弹性体（四面体网格）"));
    femEnabled->setObjectName("objectFem");
    femBottom = new QCheckBox(QStringLiteral("固定局部底面节点"));
    femYoung = new QDoubleSpinBox;
    femYoung->setObjectName("femYoungModulus");
    femYoung->setRange(10, 1e12);
    femYoung->setDecimals(1);
    femPoisson = new QDoubleSpinBox;
    femPoisson->setRange(0, .489);
    femPoisson->setDecimals(3);
    femResolution = new QSpinBox;
    femResolution->setRange(1, 6);
    femAttachment = new QComboBox;
    femAttachment->setObjectName("femAttachmentBody");
    femStiffness = new QDoubleSpinBox;
    femStiffness->setRange(1, 1e8);
    femStiffness->setDecimals(1);
    for (auto *field : {femYoung, femPoisson, femStiffness})
        field->setKeyboardTracking(false);
    femResolution->setKeyboardTracking(false);
    form->addRow(femEnabled);
    form->addRow(QStringLiteral("杨氏模量 (Pa)"), femYoung);
    form->addRow(QStringLiteral("泊松比"), femPoisson);
    form->addRow(QStringLiteral("每轴划分"), femResolution);
    form->addRow(femBottom);
    form->addRow(QStringLiteral("顶部连接刚体"), femAttachment);
    form->addRow(QStringLiteral("连接刚度 (N/m)"), femStiffness);
    group->setToolTip(
        QStringLiteral("支持弹性盒与四面体资产；顶部中心节点通过三向弹簧连接所选刚体。HSC 开关位于工具 → 物理模拟设置。"));
    auto commit = [this] {
        if (syncing || editingLocked() || !scene->current())
            return;
        auto object = *scene->current();
        object.deformable = femEnabled->isChecked();
        object.pinBottom = femBottom->isChecked();
        object.youngModulus = femYoung->value();
        object.poissonRatio = femPoisson->value();
        object.femResolution = femResolution->value();
        object.femAttachmentBody = femAttachment->currentData().toString();
        object.attachmentStiffness = femStiffness->value();
        scene->edit(object, QStringLiteral("修改弹性体参数"));
    };
    connect(femEnabled, &QCheckBox::toggled, this, commit);
    connect(femBottom, &QCheckBox::toggled, this, commit);
    connect(femAttachment, &QComboBox::currentIndexChanged, this, commit);
    connect(femResolution, &QSpinBox::editingFinished, this, commit);
    for (auto *field : {femYoung, femPoisson, femStiffness})
        connect(field, &QDoubleSpinBox::editingFinished, this, commit);
    return group;
}
void MainWindow::syncFemInspector() {
    const auto *o = displayScene()->current();
    femEnabled->setChecked(o && o->deformable);
    femEnabled->setEnabled(o && (o->shape == Shape::Box || (o->mesh && !o->mesh->tetrahedra.isEmpty())));
    femBottom->setChecked(!o || o->pinBottom);
    femYoung->setValue(o ? o->youngModulus : 5000);
    femPoisson->setValue(o ? o->poissonRatio : .3);
    femResolution->setValue(o ? o->femResolution : 2);
    femStiffness->setValue(o ? o->attachmentStiffness : 20000);
    femAttachment->clear();
    femAttachment->addItem(QStringLiteral("无"), QString());
    for (const auto &body : displayScene()->objects)
        if (o && body.id != o->id && !body.deformable && !body.fixed && body.shape != Shape::Plane)
            femAttachment->addItem(body.name, body.id);
    if (o && !o->femAttachmentBody.isEmpty()) {
        int index = femAttachment->findData(o->femAttachmentBody);
        if (index < 0) {
            femAttachment->addItem(QStringLiteral("缺失：") + o->femAttachmentBody, o->femAttachmentBody);
            index = femAttachment->count() - 1;
        }
        femAttachment->setCurrentIndex(index);
    }
    for (QWidget *field :
         std::array<QWidget *, 6>{femYoung, femPoisson, femResolution, femBottom, femAttachment, femStiffness})
        field->setEnabled(o && o->deformable);
}
} // namespace bagua
