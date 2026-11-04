#pragma once
#include <QJsonArray>
#include <QVector>
namespace youngkly {
// Maps reference-original atom index to each structure's original 1-based atom ID.
// The reference is the second structure (TS for IRC paths), or the only structure.
// Element checks validate compatibility, never chemical identity.
QVector<QVector<int>> atomCorrespondence(const QVector<QVector<int>> &elements, const QJsonArray &configured);
QJsonArray mappingJson(const QVector<QVector<int>> &mapping);
}
