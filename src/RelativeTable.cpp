#include "RelativeTable.h"
#include "si/SIModel.hpp"
#include <cmath>
namespace youngkly {
// Transpose analysis-facing tables into SI Generator's structure-per-row document layout.
QJsonObject siDocumentOrientation(const QJsonObject &source) {
    const auto headers = source["headers"].toArray();
    const auto rows = source["rows"].toArray();
    if (headers.isEmpty()) return source;
    QJsonArray outputHeaders{headers[0]}, outputRows;
    for (const auto &value : rows)
        outputHeaders << (value.toArray().isEmpty() ? QJsonValue(QJsonValue::Null) : value.toArray().at(0));
    for (int column = 1; column < headers.size(); ++column) {
        QJsonArray row{headers[column]};
        for (const auto &value : rows) {
            const auto cells = value.toArray();
            row << (column < cells.size() ? cells[column] : QJsonValue(QJsonValue::Null));
        }
        outputRows << row;
    }
    auto result = source;
    result["headers"] = outputHeaders;
    result["rows"] = outputRows;
    result["si_document_orientation"] = true;
    return result;
}
QString relativeCellText(const QJsonValue &value, int logical, int digits) {
    if (value.isNull() || value.isUndefined())
        return "N/A";
    if (!value.isDouble())
        return value.toString();
    const double v = value.toDouble();
    QString result = QString::number(v, 'f', digits);
    const bool correction = logical == 1 || logical == 3 || logical == 4 || logical == 5;
    const bool optional = logical == 6 || logical == 7;
    if (((correction && v > 0) || (optional && v >= 0)) && !result.startsWith('-'))
        result.prepend('+');
    return result;
}
QJsonObject siRelativeTable(const QJsonArray &sheetRows, bool dispersion, bool complexation) {
    const QStringList names{"ΔE",       "ΔΔE",        "E_{Total}",        "ΔZ_{0}", "ΔΔE_{Thermal}",
                            "-TΔS", "Δ_{Dispersion}", "Δ_{Complexation}", "ΔG"};
    auto columns = si::relativeEnergyColumns(dispersion, complexation);
    QJsonArray headers{QString()}, logical{-1}, bold, rows;
    for (int c : columns) {
        headers << names[c];
        logical << c;
        if (c == 2 || c == 8)
            bold << headers.size() - 1;
    }
    for (const auto &row : sheetRows) {
        auto input = row.toArray();
        QJsonArray values{input[0]};
        for (int c : columns) {
            if (c == 7)
                values << 0.0;
            else
                values << input[c == 8 ? 8 : c + 1];
        }
        rows << values;
    }
    return {{"headers", headers},
            {"rows", rows},
            {"logical_columns", logical},
            {"bold_columns", bold},
            {"include_dispersion", dispersion},
            {"include_complexation", complexation},
            {"unit", "kcal/mol"},
            {"format", "SI Generator"}};
}
} // namespace youngkly
