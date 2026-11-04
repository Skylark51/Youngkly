#pragma once
#include <QJsonObject>
#include <QString>
namespace youngkly {
void writeReport(const QString &reference, const QString &output, const QJsonObject &model,
                 const QJsonObject &config);
}
