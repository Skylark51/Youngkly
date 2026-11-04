#pragma once
#include <QJsonObject>
#include <QJsonArray>
namespace youngkly {
QJsonArray futureItems(const QJsonObject &config);
QJsonArray confirmedFuture(const QJsonObject &config, const QString &date, const QString &project);
QString futureText(const QJsonObject &item);
}
