#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QImageReader>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Easel"));
    QApplication::setOrganizationName(QStringLiteral("Easel"));
    QApplication::setApplicationVersion(QStringLiteral(EASEL_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/easel.png")));

    // Qt's default decode limit (a few hundred MB) rejects very large canvases. Allow 2 GB.
    QImageReader::setAllocationLimit(2048);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Balanced layered image editor"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"),
                                 QStringLiteral("Easel document or image to open (optional)."));
    parser.process(app);

    MainWindow window;
    window.show();

    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty())
        window.openDocument(args.first());

    return app.exec();
}
