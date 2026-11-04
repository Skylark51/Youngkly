// Developer-only fixture runner. May link either the retained baseline or upgraded core.
#include "Archive.h"
#include "Pipeline.h"
#include "Workspace.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); auto args = app.arguments();
    if (args.size() != 5) return 2;
    try {
        auto model = youngkly::loadConfig(args[1]), config = youngkly::loadConfig(args[2]);
        auto sections = youngkly::reportSections(model, config);
        for (int i = 0; i < sections.size(); ++i) sections[i] = youngkly::analyze(sections[i].toObject(), config, args[4]);
        model["sections"] = sections; youngkly::writeFile(args[3], QJsonDocument(model).toJson()); return 0;
    } catch (const std::exception &e) { QTextStream(stderr) << e.what() << Qt::endl; return 1; }
}
