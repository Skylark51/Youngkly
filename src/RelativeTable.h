#pragma once
#include <QJsonObject>
#include <QJsonArray>
namespace youngkly {
QJsonObject siDocumentOrientation(const QJsonObject &analysisTable);
// Input: Structure, L, M, N, O, P, Q, R, S in the original sheet reference.
QJsonObject siRelativeTable(const QJsonArray &sheetRows, bool dispersion = false, bool complexation = false);
QString relativeCellText(const QJsonValue &value, int logicalColumn, int digits = 2);
} // namespace youngkly
