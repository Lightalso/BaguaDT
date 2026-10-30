#include "EditorControls.h"
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QStyle>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace bagua {
DragSpinBox::DragSpinBox(QWidget *parent) : QDoubleSpinBox(parent) {
    setKeyboardTracking(false);
    setAccelerated(true);
    setGroupSeparatorShown(false);
    setScrubStep(0.01);
    // Keep the native arrows: they are the discoverable path for keyboard and
    // single-step users. Dragging is an addition, not a replacement.
    if (auto *edit = lineEdit()) {
        edit->installEventFilter(this);
        edit->setMouseTracking(true);
    }
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint &point) {
        QMenu menu(this);
        menu.addAction(QStringLiteral("归零"), this, [this] { setValue(0); });
        menu.addAction(QStringLiteral("重置为默认值"), this, [this] { setValue(defaultValue); });
        menu.addSeparator();
        menu.addAction(QStringLiteral("取整"), this, [this] { setValue(std::round(value())); });
        menu.addAction(QStringLiteral("复制数值"), this,
                       [this] { QApplication::clipboard()->setText(QString::number(value(), 'g', 12)); });
        const QString copied = QApplication::clipboard()->text();
        bool ok = false;
        copied.toDouble(&ok);
        auto *paste = menu.addAction(QStringLiteral("粘贴数值"), this, [this, copied] { setValue(copied.toDouble()); });
        paste->setEnabled(ok);
        menu.exec(mapToGlobal(point));
    });
    connect(this, &QDoubleSpinBox::editingFinished, this, [this] {
        if (!scrubbing)
            committedValue = value();
    });
    committedValue = value();
}

void DragSpinBox::setScrubStep(double step) {
    scrubPerPixel = std::abs(step);
}

void DragSpinBox::focusInEvent(QFocusEvent *event) {
    if (!scrubbing)
        committedValue = value();
    QDoubleSpinBox::focusInEvent(event);
}

void DragSpinBox::focusOutEvent(QFocusEvent *event) {
    QDoubleSpinBox::focusOutEvent(event);
    if (!scrubbing)
        committedValue = value();
}

void DragSpinBox::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        restoreCommitted();
        event->accept();
        return;
    }
    QDoubleSpinBox::keyPressEvent(event);
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        committedValue = value();
}

void DragSpinBox::restoreCommitted() {
    {
        const QSignalBlocker blocker(this);
        setValue(committedValue);
    }
    if (auto *edit = lineEdit())
        edit->setText(textFromValue(committedValue));
    clearFocus();
}

void DragSpinBox::applyScrubDelta(int dx) {
    double step = scrubPerPixel;
    const Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();
    if (modifiers & Qt::ShiftModifier)
        step *= .1;
    double next = pressedValue + double(dx) * step;
    if (singleStep() > 0 && (modifiers & Qt::ControlModifier))
        next = std::round(next / singleStep()) * singleStep();
    // setValue clamps to range and keeps the displayed text in sync.
    setValue(next);
    emit scrubChanged();
}

bool DragSpinBox::eventFilter(QObject *watched, QEvent *event) {
    auto *edit = lineEdit();
    if (watched != edit)
        return QDoubleSpinBox::eventFilter(watched, event);
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            pressed = true;
            scrubbing = false;
            pressedPos = mouse->position().toPoint();
            pressedValue = value();
        }
        return QDoubleSpinBox::eventFilter(watched, event);
    }
    case QEvent::MouseMove: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (!pressed || !(mouse->buttons() & Qt::LeftButton))
            return QDoubleSpinBox::eventFilter(watched, event);
        const QPoint delta = mouse->position().toPoint() - pressedPos;
        if (!scrubbing && delta.manhattanLength() < 4)
            return QDoubleSpinBox::eventFilter(watched, event);
        if (!scrubbing) {
            scrubbing = true;
            pressedValue = value();
            edit->setReadOnly(true);
            edit->deselect();
            setCursor(Qt::SizeHorCursor);
            emit scrubStarted();
        }
        applyScrubDelta(delta.x());
        return true;
    }
    case QEvent::MouseButtonRelease: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton && pressed) {
            pressed = false;
            if (scrubbing) {
                scrubbing = false;
                edit->setReadOnly(false);
                unsetCursor();
                committedValue = value();
                emit scrubFinished();
                // One editingFinished per scrub gives the scene exactly one
                // undo command, the same contract as a gizmo drag.
                interpretText();
                QMetaObject::invokeMethod(this, "editingFinished", Qt::DirectConnection);
                return true;
            }
        }
        return QDoubleSpinBox::eventFilter(watched, event);
    }
    default:
        break;
    }
    return QDoubleSpinBox::eventFilter(watched, event);
}

CollapsibleGroupBox::CollapsibleGroupBox(const QString &title, QWidget *parent) : QGroupBox(title, parent) {
    setCheckable(true);
    setChecked(true);
    content = new QWidget(this);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(4);
    layout->addWidget(content);
    connect(this, &QGroupBox::toggled, content, &QWidget::setVisible);
}

void CollapsibleGroupBox::setExpanded(bool expanded) {
    setChecked(expanded);
    if (content)
        content->setVisible(expanded);
}
} // namespace bagua
