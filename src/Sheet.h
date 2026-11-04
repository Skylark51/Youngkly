#pragma once
#include "Archive.h"
#include <QDate>
#include <QJsonObject>
#include <QVector>
namespace youngkly {
struct Cell {
    QString ref, text, formula, type;
    int style{};
};
struct SheetRow {
    int number{};
    QMap<QString, Cell> cells;
    QString text(const QString &col) const {
        return cells.value(col).text;
    }
};
struct Sheet {
    QString name;
    QVector<SheetRow> rows;
    QMap<int, QString> formats;
    bool date1904{};
};
Sheet readSheet(const QString &path, const QString &name);
QDate reportDate(const Cell &cell, const Sheet &sheet, int year);
QString translateFormula(const QString &formula, const QString &from, const QString &to);
QJsonObject makePlan(const Sheet &sheet, const QDate &date);
QByteArray download(const QString &url);
} // namespace youngkly
