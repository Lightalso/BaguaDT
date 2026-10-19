#include "MainWindow.h"
#include <QGridLayout>
#include <QGroupBox>
namespace bagua {
QWidget *MainWindow::makeVelocityInspector() {
    auto *group=new QGroupBox(QStringLiteral("初始速度"));auto *grid=new QGridLayout(group);
    grid->setContentsMargins(8,8,8,8);grid->setSpacing(4);
    for(int i=0;i<3;++i)grid->addWidget(new QLabel(QString(QChar("XYZ"[i]))),0,i+1);
    grid->addWidget(new QLabel(QStringLiteral("平移 (m/s)")),1,0);
    grid->addWidget(new QLabel(QStringLiteral("转动 (°/s)")),2,0);
    for(int i=0;i<6;++i) {
        auto *field=new QDoubleSpinBox;field->setRange(-1e6,1e6);field->setDecimals(3);field->setMinimumWidth(60);field->setKeyboardTracking(false);
        field->setObjectName(QString("initialVelocity%1").arg(i));velocityFields[size_t(i)]=field;grid->addWidget(field,i/3+1,i%3+1);
        connect(field,&QDoubleSpinBox::editingFinished,this,[this,i]{
            if(syncing || editingLocked() || !scene->current())return;
            auto object=*scene->current();auto &v=i<3?object.linearVelocity:object.angularVelocity;v[i%3]=float(velocityFields[size_t(i)]->value());scene->edit(object,QStringLiteral("修改初始速度"));
        });
    }
    return group;
}
void MainWindow::syncVelocityInspector() {
    auto *o=displayScene()->current();for(int i=0;i<6;++i){velocityFields[size_t(i)]->setValue(o?(i<3?o->linearVelocity[i]:o->angularVelocity[i-3]):0);velocityFields[size_t(i)]->setEnabled(o && !o->fixed && o->shape!=Shape::Plane);}
}
}
