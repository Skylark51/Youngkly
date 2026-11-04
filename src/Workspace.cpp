#include "Workspace.h"
#include "Archive.h"
#include "Pipeline.h"
#include "Sheet.h"
#include "FutureWork.h"
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
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
