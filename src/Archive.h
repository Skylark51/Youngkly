#pragma once
#include <QByteArray>
#include <QMap>
#include <QString>
namespace youngkly {
using Archive = QMap<QString, QByteArray>;
Archive readZip(const QString &path);
void writeZip(const QString &path, const Archive &entries);
QByteArray readFile(const QString &path);
void writeFile(const QString &path, const QByteArray &bytes);
} // namespace youngkly
