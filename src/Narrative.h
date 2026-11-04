#pragma once
#include <QJsonObject>
namespace youngkly {
QString reportNumber(double value, int digits = 3);
QJsonObject describeChange(const QJsonArray &row, const QJsonArray &headers, const QString &kind,
                           int atomCount, double threshold, const QJsonObject &templates);
void rebuildNarratives(QJsonObject &group, const QJsonObject &config);
}
