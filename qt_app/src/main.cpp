#include "mainwindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QSurfaceFormat>
#include <QTimer> // [PROBE] temp

int main(int argc, char* argv[]) {
    // Skia's Ganesh GL backend needs GLSL 1.30+ (gl_VertexID); request a core
    // profile before any OpenGL context is created (default is 2.1 on macOS).
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    app.setApplicationName("PDF Reader");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("PdfReader");
    app.setOrganizationDomain("pdfreader.example.com");
    
    // Enable high DPI scaling (Qt 6 handles this automatically)
    // QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    
    QCommandLineParser parser;
    parser.setApplicationDescription("PDF Reader - A simple PDF viewer using PDFium and Qt6");
    parser.addHelpOption();
    parser.addVersionOption();
    
    QCommandLineOption fileOption(QStringList() << "f" << "file",
                                   "Open specified PDF file",
                                   "file");
    parser.addPositionalArgument("file", "PDF file to open");
    parser.addOption(fileOption);
    
    parser.process(app);
    
    MainWindow window;
    window.show();
    
    // Handle command line file argument
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        QString filePath = args.first();
        QFileInfo fi(filePath);
        if (fi.exists() && fi.isFile()) {
            qDebug() << "Opening file:" << filePath;
            // [PROBE] temp: auto-open + auto-toggle to GPU after show.
            QTimer::singleShot(0, &window, [&window, filePath]() {
                window.probeAutoToggle(filePath);
            });
        }
    }
    
    return app.exec();
}