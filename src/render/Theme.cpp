#include "Theme.h"
#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

namespace bagua {
void applyEditorTheme(QApplication &app) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    app.styleHints()->setColorScheme(Qt::ColorScheme::Light);
#endif
    const auto styles = QStyleFactory::keys();
    for (const auto &name : {"windowsvista", "windows11", "windows", "Fusion"}) {
        if (styles.contains(QString::fromLatin1(name), Qt::CaseInsensitive)) {
            app.setStyle(QString::fromLatin1(name));
            break;
        }
    }
    auto palette = app.style()->standardPalette();
    palette.setColor(QPalette::Window, QColor("#f3f3f3"));
    palette.setColor(QPalette::WindowText, QColor("#202020"));
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::AlternateBase, QColor("#f7f8fa"));
    palette.setColor(QPalette::Text, QColor("#202020"));
    palette.setColor(QPalette::Button, QColor("#f3f3f3"));
    palette.setColor(QPalette::ButtonText, QColor("#202020"));
    palette.setColor(QPalette::Highlight, QColor("#0078d4"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, QColor("#ffffe1"));
    palette.setColor(QPalette::ToolTipText, QColor("#202020"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#888888"));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#888888"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#888888"));
    app.setPalette(palette);
    // A restrained, light editor chrome inspired by Unreal's dark panels and
    // Unity's flat light theme. The point is a clear visual hierarchy between
    // the viewport, panels, and the small controls used for precise editing.
    app.setStyleSheet(QStringLiteral(R"(
QMainWindow { background: #eef1f5; }
QToolBar { background: #fbfcfd; border: none; border-bottom: 1px solid #d6dde6; spacing: 3px; padding: 3px 4px; }
QToolBar::separator { background: #d9e0e8; width: 1px; margin: 3px 5px; }
QToolButton { border: 1px solid transparent; border-radius: 3px; padding: 3px 6px; }
QToolButton:hover { background: #eaf3fc; border-color: #b9d4ee; }
QToolButton:pressed { background: #d8e9fa; }
QDockWidget { font-weight: 600; }
QDockWidget::title { background: #e7ecf3; border-bottom: 1px solid #d6dde6; padding: 5px 8px; text-align: left; }
QGroupBox { background: #fbfcfd; border: 1px solid #d8dfe8; border-radius: 5px; margin-top: 12px;
            padding-top: 4px; font-weight: 600; color: #2b3d50; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }
QLineEdit, QDoubleSpinBox, QSpinBox, QComboBox { background: #ffffff; border: 1px solid #c9d3df;
            border-radius: 3px; padding: 2px 5px; selection-background-color: #0f6cbd; }
QLineEdit:focus, QDoubleSpinBox:focus, QSpinBox:focus, QComboBox:focus { border: 1px solid #0f6cbd; }
QComboBox::drop-down { border: none; width: 18px; }
QPushButton { background: #fbfcfd; border: 1px solid #c6d0dc; border-radius: 3px; padding: 3px 10px; }
QPushButton:hover { background: #eef6ff; border-color: #7fb2e3; }
QPushButton:pressed { background: #dcecfa; }
QPushButton:disabled { color: #9aa5b1; background: #f2f4f7; border-color: #dde3ea; }
QTreeWidget, QListWidget, QTableWidget, QPlainTextEdit { background: #ffffff; border: 1px solid #d8dfe8;
            border-radius: 3px; alternate-background-color: #f8fafc; }
QTreeWidget::item:selected, QListWidget::item:selected { background: #dcecfa; color: #102a43; }
QTreeWidget::item:hover, QListWidget::item:hover { background: #eef6ff; }
QHeaderView::section { background: #f1f5f9; border: none; border-right: 1px solid #e0e6ee;
            border-bottom: 1px solid #d8dfe8; padding: 3px 6px; color: #4a5b6d; }
QTabWidget::pane { border: 1px solid #d8dfe8; border-radius: 3px; background: #fbfcfd; }
QTabBar::tab { background: #e8edf4; border: 1px solid #d8dfe8; border-bottom: none;
            border-top-left-radius: 3px; border-top-right-radius: 3px; padding: 5px 12px; margin-right: 2px; }
QTabBar::tab:selected { background: #ffffff; color: #12395f; }
QTabBar::tab:hover:!selected { background: #eef4fb; }
QSplitter::handle { background: #e1e7ee; }
QSplitter::handle:horizontal { width: 4px; }
QSplitter::handle:vertical { height: 4px; }
QStatusBar { background: #e7ecf3; border-top: 1px solid #d6dde6; }
QStatusBar QLabel { color: #43566a; }
QToolTip { background: #ffffe1; color: #202020; border: 1px solid #b9c2cd; padding: 2px 4px; }
QMenu::item:selected { background: #dcecfa; color: #102a43; }
)"));
    // Use the Windows CJK UI family explicitly so fallback does not select a
    // serif document font for Chinese labels.
    QFont font(QStringLiteral("Microsoft YaHei UI"), 9);
    app.setFont(font);
}
} // namespace bagua
