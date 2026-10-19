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
    // Use the Windows CJK UI family explicitly so fallback does not select a
    // serif document font for Chinese labels.
    QFont font(QStringLiteral("Microsoft YaHei UI"), 9);
    app.setFont(font);
}
} // namespace bagua
