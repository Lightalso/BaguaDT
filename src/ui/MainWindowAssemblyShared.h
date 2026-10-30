#pragma once
#include "MainWindow.h"
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>

namespace bagua {
// Small helpers shared by the assembly panel construction and its state logic.
inline DragSpinBox *vectorField(QWidget *parent, const char *objectName, double step = .01, int decimals = 4) {
    auto *field = new DragSpinBox(parent);
    field->setObjectName(objectName);
    field->setRange(-1e5, 1e5);
    field->setDecimals(decimals);
    field->setSingleStep(step);
    field->setScrubStep(step * .05);
    field->setKeyboardTracking(false);
    field->setMinimumWidth(66);
    return field;
}
inline QLabel *fieldLabel(const QString &text) {
    auto *label = new QLabel(text);
    label->setMinimumWidth(54);
    return label;
}
inline QPushButton *actionButton(const QString &text, const char *name) {
    auto *button = new QPushButton(text);
    if (name)
        button->setObjectName(name);
    button->setMinimumHeight(24);
    return button;
}
inline QString linkTypeName(Link::Type type) {
    switch (type) {
    case Link::Type::Spherical:
        return QStringLiteral("球铰");
    case Link::Type::Revolute:
        return QStringLiteral("转动副");
    case Link::Type::Prismatic:
        return QStringLiteral("移动副");
    case Link::Type::Fixed:
        return QStringLiteral("固定副");
    case Link::Type::Rotation:
        return QStringLiteral("转动约束");
    case Link::Type::Stitch:
        return QStringLiteral("缝合 / 弹簧");
    }
    return QStringLiteral("约束");
}
inline QString linkTypeTip(Link::Type type) {
    switch (type) {
    case Link::Type::Spherical:
        return QStringLiteral("保留两个部件之间的一个共同点，允许任意方向转动。");
    case Link::Type::Revolute:
        return QStringLiteral("绕一条世界轴转动；可设置角度限位、驱动和轴向阻尼。");
    case Link::Type::Prismatic:
        return QStringLiteral("沿一条世界轴平移；常用于导轨、滑轨和悬架行程。");
    case Link::Type::Fixed:
        return QStringLiteral("把两个部件焊接成一个刚体；连接点与轴向不参与计算。");
    case Link::Type::Rotation:
        return QStringLiteral("约束指定轴上的相对转角，并可按角速度驱动。");
    case Link::Type::Stitch:
        return QStringLiteral("按顶点 / 锚点对建立软约束；用于轮胎胎唇、网格边界等。");
    }
    return {};
}
inline QString linkSummary(const Link &link) {
    if (link.type == Link::Type::Stitch) {
        QString text = QStringLiteral("%1 对 · k=%2").arg(link.pairs.size()).arg(link.stiffness, 0, 'g', 4);
        if (link.rest > 0)
            text += QStringLiteral(" · 静止 %1 m").arg(link.rest, 0, 'g', 4);
        return text;
    }
    QString text = QStringLiteral("k=%1").arg(link.strength, 0, 'g', 4);
    if (link.hasLimit)
        text += QStringLiteral(" · 限位");
    if (link.driven)
        text += QStringLiteral(" · 驱动");
    if (link.damped)
        text += QStringLiteral(" · 阻尼");
    return text;
}
inline QColor linkTypeColor(Link::Type type) {
    switch (type) {
    case Link::Type::Spherical:
        return QColor("#8e5bd8");
    case Link::Type::Revolute:
        return QColor("#e08a2e");
    case Link::Type::Prismatic:
        return QColor("#1f9fc4");
    case Link::Type::Fixed:
        return QColor("#d04545");
    case Link::Type::Rotation:
        return QColor("#2b7de0");
    case Link::Type::Stitch:
        return QColor("#7a5cff");
    }
    return QColor("#555555");
}
inline QVector3D objectCenterWorld(const Object &object) {
    const auto bounds = localBounds(object);
    return object.matrix().map((bounds.first + bounds.second) * .5f);
}
inline QString bodyText(const Scene *scene, const QString &id) {
    const auto *object = scene->find(id);
    return object ? object->name : QStringLiteral("(缺失 %1)").arg(id.left(6));
}
inline QIcon linkIcon(const Link &link) {
    QPixmap pixmap(12, 12);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(linkTypeColor(link.type).darker(130), 1));
    painter.setBrush(linkTypeColor(link.type));
    painter.drawRoundedRect(QRectF(1, 1, 10, 10), 2, 2);
    return QIcon(pixmap);
}
} // namespace bagua
