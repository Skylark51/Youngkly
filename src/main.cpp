#include "Archive.h"
#include "Docx.h"
#include "Pipeline.h"
#include "Sheet.h"
#include "Workspace.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <stdexcept>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("Youngkly");
    app.setApplicationVersion("0.2.0");
    QCommandLineParser p;
    p.addHelpOption();
    p.addVersionOption();
    p.addOption({"config", "Config JSON", "file", "config/youngkly.json"});
    p.addOption({"date", "Report date (YYYY-MM-DD)", "date"});
    p.addOption({"force-refresh", "Redownload and reparse output files"});
    p.addOption({"force-output", "Allow overwriting an existing report"});
    p.addOption({"fetch", "Download current Google workbook"});
    p.addOption({"resolve", "Find and download outputs using configured server priority"});
    p.addOption({"analyze", "Analyze resolved outputs with SI Generator"});
    p.addOption({"xlsx", "Local workbook path", "file", ".youngkly/source.xlsx"});
    p.addOption({"out", "Plan/model JSON", "file", "outputs/report-model.json"});
    p.addOption({"model", "Existing analyzed/rendered model for DOCX export", "file"});
    p.addOption({"docx", "Write DOCX from the reference report", "file"});
    p.addOption({"project", "Process one configured project ID", "id"});
    p.process(app);
    try {
        auto config = youngkly::loadConfig(p.value("config"));
        config["force_refresh"] = p.isSet("force-refresh") || config["force_refresh"].toBool();
        if (p.isSet("docx")) {
            QElapsedTimer timer; timer.start();
            if (QFileInfo::exists(p.value("docx")) && !p.isSet("force-output"))
                throw std::runtime_error("Report exists; choose a new output or use --force-output");
            if (!p.isSet("model"))
                throw std::runtime_error("--docx requires --model after analysis and rendering");
            auto model = youngkly::loadConfig(p.value("model"));
            QDir().mkpath(QFileInfo(p.value("docx")).path());
            youngkly::writeReport(config["reference_docx"].toString(), p.value("docx"), model, config);
            QTextStream(stdout) << "DOCX ms: " << timer.elapsed() << "\n";
            QTextStream(stdout) << "DOCX: " << QFileInfo(p.value("docx")).absoluteFilePath() << "\n";
            return 0;
        }
        auto date = QDate::fromString(p.value("date"), Qt::ISODate);
        if (!date.isValid())
            throw std::runtime_error("--date YYYY-MM-DD is required; dates are never selected implicitly");
        if (config.contains("sources") && !p.isSet("xlsx")) {
            const auto model = youngkly::buildWorkspace(config, date, p.isSet("fetch"), p.isSet("resolve"),
                                                        p.isSet("analyze"), p.value("project"));
            QDir().mkpath(QFileInfo(p.value("out")).path());
            youngkly::writeFile(p.value("out"), QJsonDocument(model).toJson());
            QTextStream(stdout) << "Selected rows: " << youngkly::selectedRowCount(model)
                                << "\nTriplets: " << youngkly::tripletCount(model)
                                << "\nModel: " << QFileInfo(p.value("out")).absoluteFilePath() << Qt::endl;
            return 0;
        }
        const auto xlsx = p.value("xlsx");
        QDir().mkpath(QFileInfo(xlsx).path());
        if (p.isSet("fetch"))
            youngkly::writeFile(xlsx, youngkly::download(config["xlsx_url"].toString()));
        const auto sheet = youngkly::readSheet(xlsx, config["sheet_name"].toString());
        auto plan = youngkly::makePlan(sheet, date);
        if (p.isSet("resolve") || p.isSet("analyze"))
            plan = youngkly::resolveFiles(plan, config, ".youngkly/outputs");
        if (p.isSet("analyze"))
            plan = youngkly::analyze(plan, config, ".youngkly/outputs");
        plan["source"] = QJsonObject{
            {"url", config["sheet_url"]},
            {"gid", config["gid"]},
            {"xlsx_sha256",
             QString::fromLatin1(
                 QCryptographicHash::hash(youngkly::readFile(xlsx), QCryptographicHash::Sha256).toHex())},
            {"retrieval", p.isSet("fetch") ? "fresh_download" : "local_snapshot"}};
        QDir().mkpath(QFileInfo(p.value("out")).path());
        youngkly::writeFile(p.value("out"), QJsonDocument(plan).toJson());
        QTextStream(stdout) << "Date: " << date.toString(Qt::ISODate)
                            << "\nSelected rows: " << plan["rows"].toArray().size()
                            << "\nTriplets: " << plan["triplets"].toArray().size()
                            << "\nModel: " << QFileInfo(p.value("out")).absoluteFilePath() << "\n";
        return 0;
    } catch (const std::exception &e) {
        QTextStream(stderr) << "Youngkly: " << e.what() << "\n";
        return 1;
    }
}
