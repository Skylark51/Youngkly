#include "Workspace.h"
#include "Archive.h"
#include "Pipeline.h"
#include "Sheet.h"
#include "FutureWork.h"
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QDateTime>
#include <QUuid>
#include <QJsonDocument>
#include <QSet>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTextStream>
#include <stdexcept>

namespace youngkly {
QJsonArray reportSections(const QJsonObject &model, const QJsonObject &config) {
    if (model.contains("sections"))
        return model["sections"].toArray();
    auto single = model;
    single["project_heading"] = config["project_heading"];
    single["project_id"] = "legacy";
    return {single};
}
int selectedRowCount(const QJsonObject &model) {
    if (!model.contains("sections"))
        return model["rows"].toArray().size();
    int n = 0;
    for (auto v : model["sections"].toArray())
        n += v.toObject()["rows"].toArray().size();
    return n;
}
int tripletCount(const QJsonObject &model) {
    if (!model.contains("sections"))
        return model["triplets"].toArray().size();
    int n = 0;
    for (auto v : model["sections"].toArray())
        n += v.toObject()["triplets"].toArray().size();
    return n;
}
QString displayHeading(QString s) {
    const QString digits = "⁰¹²³⁴⁵⁶⁷⁸⁹";
    QRegularExpression re("\\^\\{([0-9]+)\\}");
    auto matches = re.globalMatch(s);
    QVector<QRegularExpressionMatch> m;
    while (matches.hasNext())
        m << matches.next();
    for (auto i = m.crbegin(); i != m.crend(); ++i) {
        QString sup;
        for (auto d : i->captured(1))
            sup += digits[d.digitValue()];
        s.replace(i->capturedStart(), i->capturedLength(), sup);
    }
    return s;
}
QStringList saveProjectSnapshots(const QJsonObject &model, const QJsonObject &config,
                                 const QString &directory) {
    const auto date = model["report_date"].toString();
    if (!QDate::fromString(date, Qt::ISODate).isValid())
        throw std::runtime_error("Snapshot requires a valid report date");
    const auto d = QDate::fromString(date, Qt::ISODate);
    const int week = (d.day() + QDate(d.year(), d.month(), 1).dayOfWeek() - 2) / 7 + 1;
    const auto title = QString("%1년 %2월 %3주차 주간 보고서 (%4)").arg(d.year()).arg(d.month()).arg(week).arg(d.toString("MM-dd"));
    const auto folder = QDir(directory).absoluteFilePath(title);
    const auto path = folder + "/" + title + ".youngkly_project.json";
    QJsonArray sections = reportSections(model, config);
    // A report contains every selected research and tab, as one project.
    const auto versionRoot = folder + "/자료";
    int revision = 1;
    while (QDir(versionRoot + QString("/저장 %1").arg(revision, 3, 10, QChar('0'))).exists()) ++revision;
    const auto dataFolder = versionRoot + QString("/저장 %1").arg(revision, 3, 10, QChar('0'));
    {
        if (!QDir().mkpath(dataFolder + "/그림")) throw std::runtime_error("Cannot create project snapshot");
        // Copy captures so a saved preview does not depend on a transient run folder.
        int asset = 0;
        for (int i = 0; i < sections.size(); ++i) {
            auto section = sections[i].toObject(); auto groups = section["analyses"].toArray();
            for (int g = 0; g < groups.size(); ++g) {
                auto group = groups[g].toObject(); auto captures = group["captures"].toArray();
                for (int c = 0; c < captures.size(); ++c) {
                    auto capture = captures[c].toObject();
                    for (const auto &key : {"png", "gif", "animation"}) {
                        const auto source = capture[key].toString();
                        if (source.isEmpty()) continue;
                        const auto target = dataFolder + "/그림/" + QString::number(++asset) + "." + QFileInfo(source).suffix();
                        writeFile(target, readFile(source)); capture[key] = target;
                    }
                    captures[c] = capture;
                }
                group["captures"] = captures; groups[g] = group;
            }
            section["analyses"] = groups; sections[i] = section;
        }
        auto snapshot = model; snapshot["sections"] = sections;
        snapshot["saved_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        auto savedConfig = config;
        const auto futureFile = config["future_work_file"].toString();
        if (QFileInfo::exists(futureFile)) {
            writeFile(dataFolder + "/future-work.json", readFile(futureFile));
            savedConfig["future_work_file"] = dataFolder + "/future-work.json";
        }
        writeFile(dataFolder + "/config.json", QJsonDocument(savedConfig).toJson());
        snapshot["snapshot_config"] = dataFolder + "/config.json";
        snapshot["project_name"] = title;
        snapshot["word_output"] = model["word_output"];
        const auto word = model["word_output"].toString();
        if (!word.isEmpty() && QFileInfo::exists(word))
            writeFile(dataFolder + "/" + QFileInfo(word).fileName(), readFile(word));
        const auto historyPath = dataFolder + "/" + title + ".youngkly_project.json";
        writeFile(historyPath, QJsonDocument(snapshot).toJson());
        writeFile(path, QJsonDocument(snapshot).toJson());
        return {path};
    }
    return {};
}
QJsonObject buildWorkspace(const QJsonObject &config, const QDate &date, bool fetch, bool resolve,
                           bool analyzeOutputs, const QString &project) {
    const auto sources = config["sources"].toArray();
    QJsonArray sections;
    QMap<QString, QString> workbooks;
    bool matched = false;
    qint64 sheetDownloadMs = 0, sheetParseMs = 0;
    int downloadedBooks = 0;
    for (auto value : sources) {
        const auto source = value.toObject();
        const auto id = source["id"].toString();
        if (!project.isEmpty() && id != project)
            continue;
        matched = true;
        const auto book = source["spreadsheet_id"].toString();
        QString path = QDir(".youngkly/sheets").filePath(book + ".xlsx");
        QDir().mkpath(".youngkly/sheets");
        if (!workbooks.contains(book)) {
            if (fetch) {
                QTextStream(stdout) << "Downloading: " << displayHeading(source["project_heading"].toString())
                                    << Qt::endl;
                QElapsedTimer timer; timer.start();
                writeFile(path,
                          download("https://docs.google.com/spreadsheets/d/" + book + "/export?format=xlsx"));
                sheetDownloadMs += timer.elapsed(); ++downloadedBooks;
            }
            if (!QFileInfo::exists(path))
                throw std::runtime_error("Missing workbook snapshot; fetch first: " + id.toStdString());
            workbooks[book] = path;
        } else
            path = workbooks[book];
        const auto hash =
            QString::fromLatin1(QCryptographicHash::hash(readFile(path), QCryptographicHash::Sha256).toHex());
        for (auto tab : source["tabs"].toArray()) {
            QElapsedTimer timer; timer.start();
            auto plan = makePlan(readSheet(path, tab.toString()), date);
            sheetParseMs += timer.elapsed();
            plan["future_work"] = confirmedFuture(config, date.toString(Qt::ISODate), id);
            plan["future_work_review"] = plan["future_work"].toArray().isEmpty() ? "plan_input_or_confirmation_required" : "confirmed_for_report_date";
            plan["project_id"] = id;
            plan["project_heading"] = source["project_heading"];
            plan["section_id"] = id + "-" +
                                 QString::fromLatin1(QCryptographicHash::hash(tab.toString().toUtf8(),
                                                                              QCryptographicHash::Sha256)
                                                         .toHex()
                                                         .left(12));
            // A report can contain identically named stages in several projects.
            // Keep per-group rules keyed by both source tab and original row identity.
            for (const char *field : {"triplets", "spin_groups"}) {
                QJsonArray keyed;
                for (const auto &value : plan[field].toArray()) {
                    auto group = value.toObject();
                    const auto identifiers = group["rows"].toArray();
                    if (!identifiers.isEmpty())
                        group["rule_key"] = plan["section_id"].toString() + "::" +
                            QString::number(identifiers.at(0).toInt());
                    keyed << group;
                }
                plan[field] = keyed;
            }
            plan["source"] = QJsonObject{{"spreadsheet_id", book},
                                         {"tab", tab},
                                         {"xlsx_sha256", hash},
                                         {"retrieval", fetch ? "fresh_download" : "local_snapshot"}};
            if ((resolve || analyzeOutputs) && !plan["rows"].toArray().isEmpty()) {
                QTextStream(stdout) << "Searching: " << displayHeading(source["project_heading"].toString())
                                    << " / " << tab.toString() << Qt::endl;
                plan = resolveFiles(plan, config, ".youngkly/outputs");
            }
            if (analyzeOutputs && !plan["rows"].toArray().isEmpty())
                plan = analyze(plan, config, ".youngkly/outputs");
            sections << plan;
        }
    }
    if (!matched)
        throw std::runtime_error("Unknown project ID: " + project.toStdString());
    QTextStream(stdout) << "Sheet download ms: " << sheetDownloadMs << "; XLSX parse ms: " << sheetParseMs << "; workbooks: " << downloadedBooks << Qt::endl;
    return {{"metrics", QJsonObject{{"sheet_download_ms", double(sheetDownloadMs)}, {"xlsx_parse_ms", double(sheetParseMs)}, {"workbook_downloads", downloadedBooks}}},
            {"schema_version", 3},
            {"report_date", date.toString(Qt::ISODate)},
            {"sections", sections},
            {"template_status", "pending_user_text"}};
}
} // namespace youngkly
