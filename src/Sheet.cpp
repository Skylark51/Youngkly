#include "Sheet.h"
#include <QDomDocument>
#include <QEventLoop>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <cmath>
#include <stdexcept>

namespace youngkly {
namespace {
QDomDocument xml(const QByteArray &b) {
    QDomDocument d;
    auto result = d.setContent(b, QDomDocument::ParseOption::PreserveSpacingOnlyNodes);
    if (!result)
        throw std::runtime_error(QString("XML %1:%2 %3")
                                     .arg(result.errorLine)
                                     .arg(result.errorColumn)
                                     .arg(result.errorMessage)
                                     .toStdString());
    return d;
}
QString texts(const QDomElement &e) {
    QString out;
    auto ts = e.elementsByTagName("t");
    for (int i = 0; i < ts.size(); ++i)
        out += ts.at(i).toElement().text();
    return out;
}
int columnNumber(const QString &col) {
    int n = 0;
    for (const auto c : col)
        n = n * 26 + c.unicode() - 'A' + 1;
    return n;
}
QString columnName(int n) {
    QString s;
    while (n > 0) {
        --n;
        s.prepend(QChar('A' + n % 26));
        n /= 26;
    }
    return s;
}
} // namespace
QString translateFormula(const QString &formula, const QString &from, const QString &to) {
    static const QRegularExpression address("^([A-Z]+)([0-9]+)$"),
        ref("(?<![A-Za-z0-9_])(\\$?)([A-Z]{1,3})(\\$?)([1-9][0-9]*)(?![A-Za-z0-9_])");
    auto a = address.match(from), b = address.match(to);
    if (!a.hasMatch() || !b.hasMatch())
        throw std::runtime_error("Invalid shared formula address");
    int dc = columnNumber(b.captured(1)) - columnNumber(a.captured(1)),
        dr = b.captured(2).toInt() - a.captured(2).toInt();
    QString result;
    int end = 0;
    auto matches = ref.globalMatch(formula);
    while (matches.hasNext()) {
        auto m = matches.next();
        bool quoted = false;
        for (int i = 0; i < m.capturedStart(); ++i)
            if (formula[i] == '\"')
                quoted = !quoted;
        if (quoted || formula.mid(m.capturedEnd(), 2) == "'!")
            continue;
        result += formula.mid(end, m.capturedStart() - end);
        const int c = columnNumber(m.captured(2)) + (m.captured(1).isEmpty() ? dc : 0),
                  r = m.captured(4).toInt() + (m.captured(3).isEmpty() ? dr : 0);
        result +=
            (c < 1 || r < 1) ? "#REF!" : m.captured(1) + columnName(c) + m.captured(3) + QString::number(r);
        end = m.capturedEnd();
    }
    return result + formula.mid(end);
}
Sheet readSheet(const QString &path, const QString &name) {
    const auto z = readZip(path);
    const auto wb = xml(z.value("xl/workbook.xml"));
    QString rid;
    const auto sheets = wb.elementsByTagName("sheet");
    for (int i = 0; i < sheets.size(); ++i) {
        const auto e = sheets.at(i).toElement();
        if (e.attribute("name") == name)
            rid = e.attribute("r:id");
    }
    if (rid.isEmpty())
        throw std::runtime_error("Worksheet name not found: " + name.toStdString());
    const auto rel = xml(z.value("xl/_rels/workbook.xml.rels")).elementsByTagName("Relationship");
    QString target;
    for (int i = 0; i < rel.size(); ++i)
        if (rel.at(i).toElement().attribute("Id") == rid)
            target = rel.at(i).toElement().attribute("Target");
    if (target.startsWith('/'))
        target = target.mid(1);
    else
        target = "xl/" + target;
    QStringList strings;
    if (z.contains("xl/sharedStrings.xml")) {
        const auto si = xml(z.value("xl/sharedStrings.xml")).elementsByTagName("si");
        for (int i = 0; i < si.size(); ++i)
            strings << texts(si.at(i).toElement());
    }
    Sheet sheet;
    sheet.name = name;
    sheet.date1904 = wb.documentElement().firstChildElement("workbookPr").attribute("date1904") == "1";
    const auto styles = xml(z.value("xl/styles.xml"));
    QMap<int, QString> numfmts;
    const auto fmts = styles.elementsByTagName("numFmt");
    for (int i = 0; i < fmts.size(); ++i) {
        auto e = fmts.at(i).toElement();
        numfmts[e.attribute("numFmtId").toInt()] = e.attribute("formatCode");
    }
    int index = 0;
    for (auto e = styles.documentElement().firstChildElement("cellXfs").firstChildElement("xf"); !e.isNull();
         e = e.nextSiblingElement("xf"), ++index) {
        int id = e.attribute("numFmtId").toInt();
        sheet.formats[index] = (id >= 14 && id <= 22) ? "builtin-date" : numfmts.value(id);
    }
    QMap<QString, QPair<QString, QString>> shared;
    QMap<QString, QString> sharedCells;
    const auto rows = xml(z.value(target)).elementsByTagName("row");
    for (int i = 0; i < rows.size(); ++i) {
        const auto e = rows.at(i).toElement();
        SheetRow row;
        row.number = e.attribute("r").toInt();
        for (auto c = e.firstChildElement("c"); !c.isNull(); c = c.nextSiblingElement("c")) {
            Cell v;
            v.ref = c.attribute("r");
            v.type = c.attribute("t");
            v.style = c.attribute("s").toInt();
            v.formula = c.firstChildElement("f").text();
            v.text = c.firstChildElement("v").text();
            auto f = c.firstChildElement("f");
            if (f.attribute("t") == "shared") {
                auto id = f.attribute("si");
                sharedCells[v.ref] = id;
                if (!v.formula.isEmpty())
                    shared[id] = {v.ref, v.formula};
            }
            if (v.type == "s") {
                bool ok = false;
                int id = v.text.toInt(&ok);
                if (!ok || id < 0 || id >= strings.size())
                    throw std::runtime_error("Invalid shared string");
                v.text = strings[id];
            }
            if (v.type == "inlineStr")
                v.text = texts(c.firstChildElement("is"));
            QString col = v.ref;
            col.remove(QRegularExpression("[0-9]"));
            row.cells[col] = v;
        }
        sheet.rows << row;
    }
    for (auto &row : sheet.rows)
        for (auto &cell : row.cells)
            if (sharedCells.contains(cell.ref) && cell.formula.isEmpty()) {
                const auto id = sharedCells[cell.ref];
                if (!shared.contains(id))
                    throw std::runtime_error("Shared formula anchor missing");
                const auto anchor = shared[id];
                cell.formula = translateFormula(anchor.second, anchor.first, cell.ref);
            }
    return sheet;
}
QDate reportDate(const Cell &c, const Sheet &sheet, int year) {
    const auto t = c.text.trimmed();
    static const QRegularExpression full("^(\\d{4})[-/.](\\d{1,2})[-/.](\\d{1,2})$");
    auto m = full.match(t);
    if (m.hasMatch())
        return QDate(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
    static const QRegularExpression md("^(\\d{1,2})[/.](\\d{1,2})$");
    m = md.match(t);
    if (m.hasMatch() && (c.type == "s" || c.type == "inlineStr" || t.contains('/')))
        return QDate(year, m.captured(1).toInt(), m.captured(2).toInt());
    bool ok;
    const auto value = t.toDouble(&ok);
    auto fmt = sheet.formats.value(c.style).toLower();
    fmt.remove(QRegularExpression("\"[^\"]*\"|\\[[^\\]]*\\]"));
    if (ok && value > 0 && value < 200000 &&
        (fmt == "builtin-date" || (fmt.contains('d') && fmt.contains('m'))))
        return (sheet.date1904 ? QDate(1904, 1, 1) : QDate(1899, 12, 30))
            .addDays(static_cast<qint64>(std::floor(value)));
    return {};
}
QJsonObject makePlan(const Sheet &sheet, const QDate &selected) {
    QJsonArray rows, dates, groups;
    QSet<QString> unique;
    QString section;
    for (const auto &row : sheet.rows) {
        const auto filename = row.text("A").trimmed();
        const bool output = filename.endsWith(".out", Qt::CaseInsensitive) ||
                            filename.endsWith(".log", Qt::CaseInsensitive),
                   input = filename.endsWith(".inp", Qt::CaseInsensitive) ||
                           filename.endsWith(".gjf", Qt::CaseInsensitive);
        if (!filename.isEmpty() && !output && !input && row.number > 2 && row.text("D").isEmpty())
            section = filename;
        bool matchesSelected = false;
        for (const auto &column : {"I", "J"}) {
            const auto date = reportDate(row.cells.value(column), sheet, selected.year());
            if (!date.isValid())
                continue;
            const auto iso = date.toString(Qt::ISODate);
            if (!unique.contains(iso)) {
                dates << iso;
                unique.insert(iso);
            }
            matchesSelected = matchesSelected || date == selected;
        }
        if (!matchesSelected)
            continue;
        const auto iso = selected.toString(Qt::ISODate);
        QJsonObject cells;
        for (auto it = row.cells.begin(); it != row.cells.end(); ++it)
            if (!it->text.isEmpty() || !it->formula.isEmpty())
                cells[it.key()] =
                    QJsonObject{{"value", it->text}, {"formula", it->formula}, {"ref", it->ref}};
        QString bb;
        if (output && filename.endsWith("freq.out", Qt::CaseInsensitive))
            bb = filename.left(filename.size() - 4) + "-bb.out";
        if (output && filename.endsWith("freq.log", Qt::CaseInsensitive))
            bb = filename.left(filename.size() - 4) + "-bb.log";
        const auto role = filename.contains("-ircrev-opt-freq", Qt::CaseInsensitive)   ? "irc_reverse"
                          : filename.contains("-ircfwd-opt-freq", Qt::CaseInsensitive) ? "irc_forward"
                          : row.text("C").compare("TS", Qt::CaseInsensitive) == 0 ||
                                  filename.contains("-ts-freq", Qt::CaseInsensitive)
                              ? "transition_state"
                              : "other";
        rows << QJsonObject{{"sheet_row", row.number},
                            {"section", section},
                            {"date", iso},
                            {"filename", filename},
                            {"bb_filename", bb},
                            {"role", role},
                            {"kind", output  ? "output"
                                     : input ? "in_progress"
                                             : "note"},
                            {"cells", cells}};
    }
    // Only consecutive, same-stem IRC endpoints and TS constitute an automatic triplet.
    for (int i = 0; i + 2 < rows.size(); ++i) {
        auto a = rows[i].toObject(), b = rows[i + 1].toObject(), c = rows[i + 2].toObject();
        if ((a["role"] != "irc_reverse" && a["role"] != "irc_forward") || b["role"] != "transition_state" ||
            (c["role"] != "irc_reverse" && c["role"] != "irc_forward") || a["role"] == c["role"])
            continue;
        auto stem = b["filename"].toString();
        const auto prefix = stem.left(stem.size() - 4);
        if (a["section"] != b["section"] || c["section"] != b["section"])
            continue;
        const auto extension = stem.right(4);
        auto suffix = [&extension](const QJsonObject &r) {
            return (r["role"] == "irc_reverse" ? "-ircrev-opt-freq" : "-ircfwd-opt-freq") + extension;
        };
        if (a["filename"] != prefix + suffix(a) || c["filename"] != prefix + suffix(c))
            continue;
        groups << QJsonObject{{"section", a["section"]},
                              {"rows", QJsonArray{a["sheet_row"], b["sheet_row"], c["sheet_row"]}},
                              {"c_h_activation", QJsonValue::Null},
                              {"reactant_row_id", QJsonValue::Null},
                              {"product_row_id", QJsonValue::Null},
                              {"reaction_direction_confirmed", false},
                              {"requires_review", QJsonArray{"reaction type / C-H activation",
                                                             "atom correspondence", "wording template"}}};
    }
    // Spin multiplicity is the leading integer; only otherwise identical optimized
    // filenames in the same sheet section belong to a comparison.
    QJsonArray spinGroups;
    QMap<QString, int> spinIndex;
    const QRegularExpression spinName("^([1-9][0-9]*)-(.+-opt-freq\\.(?:out|log))$",
                                     QRegularExpression::CaseInsensitiveOption);
    for (const auto &value : rows) {
        const auto row = value.toObject();
        if (row["kind"] != "output" || row["role"] != "other") continue;
        const auto match = spinName.match(row["filename"].toString());
        if (!match.hasMatch()) continue;
        const auto key = row["section"].toString() + QChar(0) + match.captured(2);
        if (!spinIndex.contains(key)) {
            spinIndex[key] = spinGroups.size();
            spinGroups << QJsonObject{{"kind", "spin_optimization"},
                {"section", match.captured(2).section('.', 0, -2)},
                {"sheet_section", row["section"]}, {"rows", QJsonArray()},
                {"structure_labels", QJsonArray()}, {"multiplicities", QJsonArray()}};
        }
        const int index = spinIndex[key];
        auto group = spinGroups[index].toObject();
        auto ids = group["rows"].toArray(), labels = group["structure_labels"].toArray(),
             multiplicities = group["multiplicities"].toArray();
        const int multiplicity = match.captured(1).toInt();
        if (multiplicities.contains(multiplicity)) group["duplicate_multiplicity"] = true;
        ids << row["sheet_row"]; labels << QString("S = %1").arg((multiplicity - 1) / 2.0, 0, 'g', 6);
        multiplicities << multiplicity;
        group["rows"] = ids; group["structure_labels"] = labels; group["multiplicities"] = multiplicities;
        spinGroups[index] = group;
    }
    return {{"schema_version", 1},
            {"sheet", sheet.name},
            {"report_date", selected.toString(Qt::ISODate)},
            {"available_dates", dates},
            {"rows", rows},
            {"triplets", groups},
            {"spin_groups", spinGroups},
            {"image_width_cm", 5.0},
            {"template_status", "pending_user_text"}};
}
QByteArray download(const QString &url) {
    QNetworkAccessManager manager;
    QNetworkRequest req{QUrl(url)};
    req.setTransferTimeout(30000);
    auto *reply = manager.get(req);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(35000);
    loop.exec();
    if (!reply->isFinished()) {
        reply->abort();
        throw std::runtime_error("Download timeout");
    }
    if (reply->error() != QNetworkReply::NoError)
        throw std::runtime_error(reply->errorString().toStdString());
    return reply->readAll();
}
} // namespace youngkly
