#include "desktop.h"
#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("Whippet");
    app.setOrganizationName("WhippetDesktop");
    app.setApplicationVersion("0.1.0");
    app.setStyle("Fusion");
    QCommandLineParser parser;
    parser.setApplicationDescription("Whippet native wallet, miner and full node");
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"data-dir", "Separate desktop data directory", "directory"});
    parser.addOption({"regtest", "Use an isolated local test chain"});
    parser.addOption({"smoke-test", "Start, connect and exit after 15 seconds"});
    parser.process(app);
    QString root = parser.value("data-dir");
    if (root.isEmpty()) root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    try {
        Desktop window(QDir(root).absolutePath(), parser.isSet("regtest") ? "regtest" : QString(), parser.isSet("smoke-test"));
        window.show();
        return app.exec();
    } catch (const std::exception& e) {
        QMessageBox::critical(nullptr, "Whippet", QString::fromUtf8(e.what()));
        return 1;
    }
}
