#include <QApplication>
#include <QSurfaceFormat>
#include <QStyleFactory>
#include <QPalette>
#include <QColor>
#include "MainWindow.h"

int main(int argc, char** argv) {
    // Request an OpenGL 3.3 core profile with a depth buffer before any widget.
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);                 // 4x MSAA for smoother cube edges
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);

    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette dark;
    dark.setColor(QPalette::Window,          QColor(30, 31, 34));
    dark.setColor(QPalette::WindowText,      QColor(220, 220, 220));
    dark.setColor(QPalette::Base,            QColor(24, 25, 28));
    dark.setColor(QPalette::Text,            QColor(220, 220, 220));
    dark.setColor(QPalette::Button,          QColor(45, 46, 50));
    dark.setColor(QPalette::ButtonText,      QColor(230, 230, 230));
    dark.setColor(QPalette::Highlight,       QColor(64, 132, 214));
    dark.setColor(QPalette::HighlightedText, Qt::white);
    app.setPalette(dark);

    MainWindow w;
    w.show();
    return app.exec();
}
