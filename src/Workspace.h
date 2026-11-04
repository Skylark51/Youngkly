#pragma once
#include <QDate>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
namespace youngkly {
QJsonObject buildWorkspace(const QJsonObject &config, const QDate &date, bool fetch, bool resolve,
                           bool analyzeOutputs, const QString &project = {});
QJsonArray reportSections(const QJsonObject &model, const QJsonObject &config);
int selectedRowCount(const QJsonObject &model);
int tripletCount(const QJsonObject &model);
// One report project containing all selected research sections, with preserved save history.
QStringList saveProjectSnapshots(const QJsonObject &model, const QJsonObject &config,
                                 const QString &directory);
QString displayHeading(QString heading);
} // namespace youngkly
