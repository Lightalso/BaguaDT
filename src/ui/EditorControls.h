#pragma once
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QPoint>
#include <QWidget>

namespace bagua {
// Numeric field used by the inspector and the constraint composer.
//
// Unity and Unreal both let you drag a numeric field to scrub its value. Plain
// QDoubleSpinBox only offers the two tiny arrows, which makes precise editing
// in a docked panel slow. This field keeps the native spin-box behaviour
// (typing, arrows, wheel, range clamping) and adds:
//   * horizontal drag scrubbing on the text area,
//   * Shift while scrubbing for a ten times finer step,
//   * Esc to restore the value the field had when it gained focus,
//   * a configurable context menu with reset / round actions.
class DragSpinBox : public QDoubleSpinBox {
    Q_OBJECT
public:
    explicit DragSpinBox(QWidget *parent = nullptr);
    // Value change per horizontal pixel while scrubbing. Defaults to one
    // hundredth of singleStep.
    void setScrubStep(double step);
    double scrubStep() const { return scrubPerPixel; }
    void setResetValue(double value) { defaultValue = value; }
    double resetValue() const { return defaultValue; }
signals:
    // Emitted once when a drag becomes a scrub, for every update, and at the
    // end. The receiver can preview the object while the mouse moves.
    void scrubStarted();
    void scrubChanged();
    void scrubFinished();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void applyScrubDelta(int dx);
    void restoreCommitted();
    double committedValue = 0;
    double defaultValue = 0;
    double scrubPerPixel = 0.001;
    double pressedValue = 0;
    QPoint pressedPos;
    bool pressed = false;
    bool scrubbing = false;
};

// A details section that can be collapsed, matching the hierarchy used by the
// Unreal Details panel and Unity Inspector. The checkbox in the group title is
// the collapse control; `body()` is the container for the actual rows.
class CollapsibleGroupBox : public QGroupBox {
    Q_OBJECT
public:
    explicit CollapsibleGroupBox(const QString &title, QWidget *parent = nullptr);
    QWidget *body() const { return content; }
    void setExpanded(bool expanded);
    bool isExpanded() const { return isChecked(); }

private:
    QWidget *content = nullptr;
};

} // namespace bagua
