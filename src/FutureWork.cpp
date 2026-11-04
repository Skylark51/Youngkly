#include "FutureWork.h"
#include "Archive.h"
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
namespace youngkly {
QJsonArray futureItems(const QJsonObject &config) {
    const auto path = config["future_work_file"].toString("config/future-work.json");
    if (!QFileInfo::exists(path)) return {};
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(readFile(path), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        throw std::runtime_error("Invalid Future work JSON");
    return doc.object()["items"].toArray();
}
QJsonArray confirmedFuture(const QJsonObject &config, const QString &date, const QString &project) {
    QJsonArray result;
    QSet<QString> seen;
    for (auto v : futureItems(config)) {
        auto item = v.toObject();
        if (item["report_date"] != date || item["project_id"] != project || !item["confirmed"].toBool()) continue;
        if (item["source"].toString().trimmed().isEmpty() || item["next_action"].toString().trimmed().isEmpty()) continue;
        const auto text = futureText(item);
        const auto key = item["topic"].toString() + "\n" + text;
        if (!text.isEmpty() && !seen.contains(key)) { result << item; seen.insert(key); }
    }
    return result;
}
QString futureText(const QJsonObject &item) {
    QStringList parts;
    // These fields are complete user-authored sentences; no inferred completion or timing.
    for (const auto *key : {"current_result", "open_question", "next_action", "purpose"}) {
        const auto text = item[key].toString().trimmed();
        if (!text.isEmpty()) parts << text;
    }
    const auto text = parts.join(" ");
    if (text.contains(QRegularExpression("\\{[^{}]+\\}|~~|작성해주세요"))) return {};
    return text;
}
}
