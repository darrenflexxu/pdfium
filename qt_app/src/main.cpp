#include "mainwindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

int main(int argc, char* argv[]) {
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
                                  tr("Open specified PDF file"),
                                  tr("file"));
    parser.addPositionalArgument("file", tr("PDF file to open"));
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
            // We need to load the file in the viewer
            // For now, just show the window - the viewer needs a public loadFile method
            // This would be added to PdfViewerWidget
            qDebug() << "Opening file:" << filePath;
        }
    }
    
    return app.exec();
}