#include "MainWindow.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QSurfaceFormat>

using namespace Qt::StringLiterals;

namespace {

QPalette darkPalette() {
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(30, 31, 34));
    palette.setColor(QPalette::WindowText, QColor(220, 220, 220));
    palette.setColor(QPalette::Base, QColor(24, 25, 28));
    palette.setColor(QPalette::Text, QColor(220, 220, 220));
    palette.setColor(QPalette::Button, QColor(45, 46, 50));
    palette.setColor(QPalette::ButtonText, QColor(230, 230, 230));
    palette.setColor(QPalette::Highlight, QColor(64, 132, 214));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    return palette;
}

}  // namespace

int main(int argc, char* argv[]) {
    // OpenGL 3.3 core profile with a depth buffer and 4x MSAA. The default
    // format has to be set before the application object is created.
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    QApplication::setStyle(u"Fusion"_s);
    QApplication::setPalette(darkPalette());

    MainWindow window;
    window.show();
    return QApplication::exec();
}
