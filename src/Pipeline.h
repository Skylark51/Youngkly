#pragma once
#include <QDate>
#include <QJsonObject>
namespace youngkly {
QJsonObject loadConfig(const QString &path);
QJsonObject resolveFiles(QJsonObject plan, const QJsonObject &config, const QString &cache);
QJsonObject analyze(QJsonObject plan, const QJsonObject &config, const QString &cache);
QString bbName(const QString &filename);
QString shellQuote(QString value);
} // namespace youngkly
