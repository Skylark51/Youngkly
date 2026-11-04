#include "Docx.h"
#include "Archive.h"
#include "Pipeline.h"
#include "Workspace.h"
#include "RelativeTable.h"
#include "FutureWork.h"
#include "Narrative.h"
#include <QDate>
#include <QDomDocument>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <stdexcept>
#include <numeric>

namespace youngkly {
namespace {
QString escape(QString s) {
    return s.toHtmlEscaped();
}
QDomDocument parse(const QByteArray &b) {
    QDomDocument d;
    if (!d.setContent(b, QDomDocument::ParseOption::PreserveSpacingOnlyNodes))
        throw std::runtime_error("Invalid template XML");
    return d;
}
QString serialize(const QDomNode &n) {
    QString s;
    QTextStream stream(&s);
    n.save(stream, 0);
    return s;
}
QString text(const QDomElement &e) {
    QString s;
    const auto nodes = e.elementsByTagName("w:t");
    for (int i = 0; i < nodes.size(); ++i)
        s += nodes.at(i).toElement().text();
    return s;
}
QString style(const QDomElement &e) {
    return e.firstChildElement("w:pPr").firstChildElement("w:pStyle").attribute("w:val");
}
QString p(const QDomElement &prototype, const QString &value, bool highlight = false, bool newPage = false,
          bool keepNext = false) {
    auto pr = prototype.firstChildElement("w:pPr").cloneNode(true).toElement();
    QDomDocument owner = prototype.ownerDocument();
    if (pr.isNull())
        pr = owner.createElement("w:pPr");
    if (style(prototype).isEmpty() || style(prototype) == "a") {
        auto jc = pr.firstChildElement("w:jc");
        if (jc.isNull()) {
            jc = owner.createElement("w:jc");
            pr.appendChild(jc);
        }
        jc.setAttribute("w:val", "both");
    }
    if (newPage && pr.firstChildElement("w:pageBreakBefore").isNull())
        pr.appendChild(owner.createElement("w:pageBreakBefore"));
    if (pr.firstChildElement("w:keepLines").isNull())
        pr.appendChild(owner.createElement("w:keepLines"));
    if (keepNext && pr.firstChildElement("w:keepNext").isNull())
        pr.appendChild(owner.createElement("w:keepNext"));
    QString props = serialize(pr);
    QString runProps;
    for (auto r = prototype.firstChildElement("w:r"); !r.isNull(); r = r.nextSiblingElement("w:r"))
        if (!r.firstChildElement("w:t").isNull()) {
            runProps = serialize(r.firstChildElement("w:rPr"));
            break;
        }
    if (highlight) {
        if (runProps.isEmpty())
            runProps = "<w:rPr/>";
        if (runProps.endsWith("/>\n"))
            runProps.replace("/>\n", "><w:highlight w:val=\"yellow\"/></w:rPr>");
        else if (runProps.contains("</w:rPr>"))
            runProps.replace("</w:rPr>", "<w:highlight w:val=\"yellow\"/></w:rPr>");
        else
            runProps = "<w:rPr><w:highlight w:val=\"yellow\"/></w:rPr>";
    }
    auto run = [&](const QString &s, bool superscript) {
        QString rp = runProps;
        if (superscript) {
            if (rp.contains("</w:rPr>"))
                rp.replace("</w:rPr>", "<w:vertAlign w:val=\"superscript\"/></w:rPr>");
            else
                rp = "<w:rPr><w:vertAlign w:val=\"superscript\"/></w:rPr>";
        }
        return "<w:r>" + rp + "<w:t xml:space=\"preserve\">" + escape(s) + "</w:t></w:r>";
    };
    QString runs;
    int offset = 0;
    QRegularExpression sup("\\^\\{([0-9a-z]+)\\}");
    auto matches = sup.globalMatch(value);
    while (matches.hasNext()) {
        auto m = matches.next();
        runs += run(value.mid(offset, m.capturedStart() - offset), false) + run(m.captured(1), true);
        offset = m.capturedEnd();
    }
    runs += run(value.mid(offset), false);
    return "<w:p>" + props + runs + "</w:p>";
}
QString scriptRuns(const QString &label, const QString &properties) {
    QString result;
    int position = 0;
    QRegularExpression re("([_^])(?:\\{([^{}]+)\\}|([IVX]+|[-+]?\\d+|[^_^\\s]))");
    auto plain = [&](QString s, QString vertical) {
        QString pr = properties;
        if (!vertical.isEmpty())
            pr += "<w:vertAlign w:val=\"" + vertical + "\"/>";
        return "<w:r><w:rPr>" + pr + "</w:rPr><w:t xml:space=\"preserve\">" + escape(s) + "</w:t></w:r>";
    };
    auto matches = re.globalMatch(label);
    while (matches.hasNext()) {
        auto m = matches.next();
        if (m.capturedStart() > position)
            result += plain(label.mid(position, m.capturedStart() - position), {});
        result += plain(m.captured(2).isEmpty() ? m.captured(3) : m.captured(2),
                        m.captured(1) == "_" ? "subscript" : "superscript");
        position = m.capturedEnd();
    }
    if (position < label.size())
        result += plain(label.mid(position), {});
    if (result.isEmpty())
        result = plain({}, {});
    return result;
}
QString gap(double points, bool keepNext = false) {
    return QString("<w:p><w:pPr>%1<w:spacing w:before=\"0\" w:after=\"0\" w:line=\"%2\" w:lineRule=\"exact\"/></w:pPr><w:r><w:rPr><w:sz w:val=\"2\"/></w:rPr></w:r></w:p>")
        .arg(keepNext ? "<w:keepNext/>" : "").arg(qRound(points * 20));
}
QString koreanFonts(QString content) {
    auto doc = parse(("<root xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
                      "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">" +
                      content + "</root>")
                         .toUtf8());
    auto runs = doc.elementsByTagName("w:r");
    for (int i = 0; i < runs.size(); ++i) {
        auto run = runs.at(i).toElement();
        auto pr = run.firstChildElement("w:rPr");
        if (pr.isNull()) {
            pr = doc.createElement("w:rPr");
            run.insertBefore(pr, run.firstChild());
        }
        auto fonts = pr.firstChildElement("w:rFonts");
        if (fonts.isNull()) {
            fonts = doc.createElement("w:rFonts");
            pr.insertBefore(fonts, pr.firstChild());
        }
        fonts.setAttribute("w:eastAsia", "맑은 고딕");
        fonts.removeAttribute("w:eastAsiaTheme");
    }
    QString out;
    for (auto e = doc.documentElement().firstChild(); !e.isNull(); e = e.nextSibling())
        out += serialize(e);
    return out;
}
QString table(const QJsonObject &t, int digits, const QJsonObject &layout, int pageWidth) {
    const auto heads = t["headers"].toArray();
    if (heads.isEmpty())
        return {};
    int columns = heads.size();
    const bool energy = t["format"] == "SI Generator";
    const double labelFraction = energy ? 0.16 : t.contains("atom_ids") ? 0.36 : 0.25;
    QVector<int> widths(columns, 0);
    widths[0] = qRound(pageWidth * labelFraction);
    for (int i = 1; i < columns; ++i) widths[i] = (pageWidth - widths[0]) / qMax(1, columns - 1);
    if (columns == 1) widths[0] = pageWidth;
    else widths[columns - 1] += pageWidth - std::accumulate(widths.begin(), widths.end(), 0);
    if (energy) {
        auto weights = layout["energy_column_weights"].toArray();
        if (weights.size() != 10) weights = QJsonArray{1.4, 0.85, 0.85, 0.85, 0.85, 1.15, 0.85, 1.2, 1.65, 0.85};
        QVector<double> selected;
        const auto logical = t["logical_columns"].toArray();
        double sum = 0;
        for (int i = 0; i < columns; ++i) {
            double weight = qMax(0.1, weights[logical[i].toInt(-1) + 1].toDouble(1));
            selected << weight; sum += weight;
        }
        for (int i = 0; i < columns; ++i) widths[i] = qRound(pageWidth * selected[i] / sum);
        widths.back() += pageWidth - std::accumulate(widths.begin(), widths.end(), 0);
    }
    QString out =
        "<w:tbl><w:tblPr><w:tblW w:w=\"5000\" w:type=\"pct\"/><w:tblBorders><w:top w:val=\"single\" "
        "w:sz=\"6\" w:color=\"000000\"/><w:left w:val=\"nil\"/><w:bottom w:val=\"single\" w:sz=\"6\" "
        "w:color=\"000000\"/><w:right w:val=\"nil\"/><w:insideH w:val=\"nil\"/><w:insideV "
        "w:val=\"nil\"/></w:tblBorders><w:tblLayout w:type=\"fixed\"/><w:tblCellMar><w:left w:w=\"40\" "
        "w:type=\"dxa\"/><w:right w:w=\"40\" w:type=\"dxa\"/></w:tblCellMar></w:tblPr><w:tblGrid>";
    const auto horizontal = qRound(layout["cell_horizontal_pt"].toDouble(4) * 20);
    const auto vertical = qRound(layout["cell_vertical_pt"].toDouble(3) * 20);
    out.replace("w:w=\"40\"", QString("w:w=\"%1\"").arg(horizontal));
    out.replace("</w:tblCellMar>", QString("<w:top w:w=\"%1\" w:type=\"dxa\"/><w:bottom w:w=\"%1\" w:type=\"dxa\"/></w:tblCellMar>").arg(vertical));
    for (int i = 0; i < columns; ++i)
        out += QString("<w:gridCol w:w=\"%1\"/>").arg(widths[i]);
    out += "</w:tblGrid>";
    auto row = [&](const QJsonArray &values, bool header, bool keepNext) {
        QString xml = "<w:tr><w:trPr><w:cantSplit/>";
        if (header)
            xml += "<w:tblHeader/>";
        xml += "</w:trPr>";
        for (int i = 0; i < columns; ++i) {
            const auto value = values.at(i);
            QString label = !header && t["format"] == "SI Generator"
                                ? relativeCellText(value, t["logical_columns"].toArray()[i].toInt(-1), digits)
                            : value.isDouble() ? reportNumber(value.toDouble(), digits)
                            : value.isNull()   ? "N/A"
                                               : value.toString();
            xml += QString("<w:tc><w:tcPr><w:tcW w:w=\"%1\" w:type=\"dxa\"/>").arg(widths[i]);
            if (header)
                xml +=
                    "<w:tcBorders><w:bottom w:val=\"single\" w:sz=\"4\" w:color=\"000000\"/></w:tcBorders>";
            const bool bold = header || t["bold_columns"].toArray().contains(i);
            QString properties = "<w:rFonts w:ascii=\"Times New Roman\" w:hAnsi=\"Times New Roman\" "
                                 "w:eastAsia=\"맑은 고딕\"/><w:sz w:val=\"" +
                                 QString::number(qRound(2 * layout[energy && columns > 8 ? "wide_table_font_pt" : "table_font_pt"].toDouble(energy && columns > 8 ? 8 : 9))) +
                                 "\"/>" + QString(bold ? "<w:b/>" : "");
            xml += "<w:vAlign w:val=\"center\"/></w:tcPr><w:p><w:pPr>" +
                   QString(keepNext ? "<w:keepNext/>" : "") +
                   "<w:jc w:val=\"center\"/><w:spacing "
                   "w:before=\"0\" w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/></w:pPr>";
            xml += header ? scriptRuns(label, properties)
                          : "<w:r><w:rPr>" + properties + "</w:rPr><w:t>" + escape(label) + "</w:t></w:r>";
            xml += "</w:p></w:tc>";
        }
        return xml + "</w:tr>";
    };
    out += row(heads, true, true);
    const auto rows = t["rows"].toArray();
    for (int i = 0; i < rows.size(); ++i)
        out += row(rows[i].toArray(), false, false);
    return out + "</w:tbl>";
}
} // namespace
void writeReport(const QString &reference, const QString &output, const QJsonObject &model,
                 const QJsonObject &config) {
    if (QFileInfo(reference).absoluteFilePath() == QFileInfo(output).absoluteFilePath())
        throw std::runtime_error("Report output must differ from the template");
    const auto layout = config["report_layout"].toObject();
    auto oneBlankLine = [&](bool keep = false) { return gap(layout["table_text_gap_pt"].toDouble(8), keep); };
    auto blankLines = [&] { return gap(layout["block_gap_pt"].toDouble(12)); };
    auto package = readZip(reference);
    const auto original = package.value("word/document.xml");
    auto doc = parse(original);
    auto body = doc.documentElement().firstChildElement("w:body");
    const auto sect = body.firstChildElement("w:sectPr");
    const auto margins = sect.firstChildElement("w:pgMar"), page = sect.firstChildElement("w:pgSz");
    const int pageWidth = page.attribute("w:w", "11906").toInt() - margins.attribute("w:left", "1440").toInt() - margins.attribute("w:right", "1440").toInt() - margins.attribute("w:gutter", "0").toInt();
    QDomElement h1, h2, h3, normal, future, toc;
    QVector<QDomElement> cover;
    int firstHeading = -1, index = 0;
    for (auto e = body.firstChildElement(); !e.isNull(); e = e.nextSiblingElement(), ++index) {
        auto st = style(e);
        if (st == "1" && h1.isNull()) {
            h1 = e;
            firstHeading = index;
        }
        if (st == "2" && h2.isNull())
            h2 = e;
        if (st == "3" && h3.isNull())
            h3 = e;
        if (st == "2" && text(e) == "Future work")
            future = e;
        if (st == "10" && toc.isNull())
            toc = e;
        if (firstHeading >= 0 && e.tagName() == "w:p" && st.isEmpty() && !text(e).isEmpty() &&
            normal.isNull())
            normal = e;
        if (index < 6)
            cover << e;
    }
    if (h1.isNull() || h2.isNull() || h3.isNull() || normal.isNull() || future.isNull() || cover.size() != 6)
        throw std::runtime_error("Reference report structure differs; explicit slot mapping is required");
    auto date = QDate::fromString(model["report_date"].toString(), Qt::ISODate);
    if (!date.isValid())
        throw std::runtime_error("Model report date invalid");
    const int week = config["week_of_month"].toInt(
        (date.day() + QDate(date.year(), date.month(), 1).dayOfWeek() - 2) / 7 + 1);
    QJsonArray sections;
    QMap<QString, QString> headings;
    for (auto value : reportSections(model, config)) {
        auto section = value.toObject();
        if (section["rows"].toArray().isEmpty() && confirmedFuture(config, model["report_date"].toString(), section["project_id"].toString()).isEmpty())
            continue;
        auto id = section["project_id"].toString();
        if (!headings.contains(id)) {
            auto title = section["project_heading"].toString();
            title.remove(QRegularExpression("^\\d+\\)\\s*"));
            headings[id] = QString::number(headings.size() + 1) + ") " + title;
        }
        section["project_heading"] = headings[id];
        sections << section;
    }
    if (sections.isEmpty())
        throw std::runtime_error("No rows selected for this report date");
    QString content;
    for (int i = 0; i < cover.size(); ++i) {
        if (i == 3)
            content += p(cover[i], QString("작성일: %1년 %2월 %3일, %2월 %4주차")
                                       .arg(date.year())
                                       .arg(date.month())
                                       .arg(date.day())
                                       .arg(week));
        else
            content += serialize(cover[i]);
    }
    // Preserve the TOC field and source TOC paragraph style; clear obsolete entries.
    QString tocProps = serialize(toc.firstChildElement("w:pPr"));
    content += "<w:p>" + tocProps +
               "<w:r><w:fldChar w:fldCharType=\"begin\" w:dirty=\"true\"/></w:r><w:r><w:instrText "
               "xml:space=\"preserve\"> TOC \\o &quot;1-3&quot; \\h \\z \\u "
               "</w:instrText></w:r><w:r><w:fldChar w:fldCharType=\"separate\"/></w:r></w:p>";
    bool hasResearch = false;
    for (auto value : sections) if (!value.toObject()["rows"].toArray().isEmpty()) hasResearch = true;
    if (hasResearch) content += p(toc, "연구 진행사항");
    QString previousHeading;
    for (auto value : sections) {
        auto section = value.toObject();
        if (section["rows"].toArray().isEmpty()) continue;
        auto heading = section["project_heading"].toString();
        if (heading != previousHeading)
            content += p(toc, heading);
        previousHeading = heading;
        for (const auto &g : section["analyses"].toArray())
            content += p(toc, g.toObject()["section"].toString());
    }
    content += p(toc, "Future work");
    content += "<w:p><w:r><w:fldChar w:fldCharType=\"end\"/></w:r></w:p>";
    if (hasResearch) {
        content += "<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>";
        content += p(h1, "연구 진행사항");
    }
    auto rel = parse(package.value("word/_rels/document.xml.rels"));
    auto types = parse(package.value("[Content_Types].xml"));
    int imageId = 5000;
    previousHeading.clear();
    for (auto sectionValue : sections) {
        const auto sectionModel = sectionValue.toObject();
        if (sectionModel["rows"].toArray().isEmpty()) continue;
        const auto heading = sectionModel["project_heading"].toString();
        if (heading != previousHeading)
            content += p(h2, heading);
        previousHeading = heading;
        QSet<int> covered;
        for (const auto &item : sectionModel["analyses"].toArray()) {
            auto group = item.toObject();
            for (auto id : group["rows"].toArray())
                covered.insert(id.toInt());
            content += p(h3, group["section"].toString());
            if (group["status"] == "missing_outputs") {
                content += p(normal, group["duplicate_multiplicity"].toBool() ?
                    "같은 스핀 다중도의 항목이 중복되어 비교 대상을 확인해야 합니다." : "출력 파일 경로 확인이 필요합니다.", true);
                for (auto id : group["rows"].toArray())
                    for (auto value : sectionModel["rows"].toArray()) {
                        auto row = value.toObject();
                        if (row["sheet_row"] != id) continue;
                        auto source = row["primary_source"].toObject();
                        content += p(normal, row["filename"].toString() + " (" + source["status"].toString() + ")", true);
                    }
                continue;
            }
            const auto captures = group["captures"].toArray();
            if (captures.size() != group["rows"].toArray().size())
                throw std::runtime_error("Group images must be rendered before DOCX export");
            if (group["kind"] == "spin_optimization") {
                QStringList labels;
                for (auto label : group["structure_labels"].toArray()) labels << label.toString();
                content += p(normal, "스핀 상태별 최적화 구조: " + labels.join(" / "));
            }
            content += "<w:p>" + serialize(normal.firstChildElement("w:pPr"));
            int captureIndex = 0;
            for (const auto &value : captures) {
                if (captureIndex > 0 && captureIndex % 3 == 0)
                    content += "</w:p><w:p>" + serialize(normal.firstChildElement("w:pPr"));
                ++captureIndex;
                auto capture = value.toObject();
                const auto path = capture.contains("animation") ? capture["animation"].toString()
                                                                : capture["png"].toString();
                const auto ext = QFileInfo(path).suffix().toLower();
                const auto bytes = readFile(path);
                QImageReader reader(path);
                auto size = reader.size();
                if (!size.isValid())
                    throw std::runtime_error("Invalid image");
                const int id = ++imageId;
                const auto name = QString("youngkly-%1.%2").arg(id).arg(ext),
                           rid = QString("rIdYoungkly%1").arg(id);
                package["word/media/" + name] = bytes;
                auto relationship = rel.createElement("Relationship");
                relationship.setAttribute("Id", rid);
                relationship.setAttribute(
                    "Type", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image");
                relationship.setAttribute("Target", "media/" + name);
                rel.documentElement().appendChild(relationship);
                bool found = false;
                auto defaults = types.elementsByTagName("Default");
                for (int i = 0; i < defaults.size(); ++i)
                    if (defaults.at(i).toElement().attribute("Extension") == ext)
                        found = true;
                if (!found) {
                    auto d = types.createElement("Default");
                    d.setAttribute("Extension", ext);
                    d.setAttribute("ContentType", ext == "gif" ? "image/gif" : "image/png");
                    types.documentElement().appendChild(d);
                }
                const auto width = qMin(qint64(config["image_width_cm"].toDouble(5) * 360000), qint64(pageWidth * 635 / qMin(3, int(captures.size())))),
                           height = width * size.height() / size.width();
                content +=
                    QString(
                        "<w:r><w:drawing><wp:inline "
                        "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/"
                        "wordprocessingDrawing\"><wp:extent cx=\"%1\" cy=\"%2\"/><wp:docPr id=\"%3\" "
                        "name=\"%4\"/><a:graphic "
                        "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\"><a:graphicData "
                        "uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"><pic:pic "
                        "xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/"
                        "picture\"><pic:nvPicPr><pic:cNvPr id=\"%3\" "
                        "name=\"%4\"/><pic:cNvPicPr/></pic:nvPicPr><pic:blipFill><a:blip "
                        "r:embed=\"%5\"/><a:stretch><a:fillRect/></a:stretch></"
                        "pic:blipFill><pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%1\" "
                        "cy=\"%2\"/></a:xfrm><a:prstGeom "
                        "prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr></pic:pic></a:graphicData></"
                        "a:graphic></wp:inline></w:drawing></w:r>")
                        .arg(width)
                        .arg(height)
                        .arg(id)
                        .arg(name, rid);
            }
            content += "</w:p>";
            content += p(normal, "Relative energy (kcal/mol)", false, false, true);
            content += oneBlankLine(true);
            content += table(group["energy_table"].toObject(), 2, layout, pageWidth);
            content += oneBlankLine();
            content += p(
                normal,
                "^{a} E Total = ΔE + ΔΔE.  ^{b} T = 298.15 K.  ^{c} ΔG = E Total + ΔZ0 + ΔΔE Thermal − TΔS" +
                    QString(group["energy_table"].toObject()["include_dispersion"].toBool() ? " + ΔDispersion"
                                                                                            : "") +
                    QString(group["energy_table"].toObject()["include_complexation"].toBool()
                                ? " + ΔComplexation."
                                : "."));
            for (auto value : group["paragraphs"].toArray()) {
                auto paragraph = value.toObject();
                if (paragraph["template_key"] == "energy") content += p(normal, paragraph["text"].toString(), paragraph["highlight"].toBool());
            }
            content += blankLines();
            content += p(normal, "Mulliken Spin Density (Def2-TZVPP)", false, false, true);
            content += oneBlankLine(true);
            content += table(group["spin_table"].toObject(), 3, layout, pageWidth);
            content += oneBlankLine();
            for (const auto &value : group["paragraphs"].toArray()) {
                auto paragraph = value.toObject();
                if (paragraph["template_key"] == "spin_change")
                    content += p(normal, paragraph["text"].toString(), paragraph["highlight"].toBool());
            }
            content += blankLines();
            bool hasAngles = false;
            for (auto ids : group["geometry_table"].toObject()["atom_ids"].toArray()) if (ids.toArray().size() > 2) hasAngles = true;
            content += p(normal, hasAngles ? "Selected Geometries (Å / °)" : "Selected Geometries (Å)", false, false, true);
            content += oneBlankLine(true);
            content += table(group["geometry_table"].toObject(), 3, layout, pageWidth);
            content += oneBlankLine();
            for (const auto &item : group["paragraphs"].toArray()) {
                auto paragraph = item.toObject();
                if (paragraph["template_key"] != "geometry_change")
                    continue;
                content += p(normal, paragraph["text"].toString(), paragraph["highlight"].toBool());
            }
            for (const auto &issue : group["issues"].toArray())
                content += p(normal, issue.toString(), true);
        }
        for (auto rowValue : sectionModel["rows"].toArray()) {
            auto row = rowValue.toObject();
            if (row["kind"] == "output" && !covered.contains(row["sheet_row"].toInt()))
                content += p(normal, "결과 확인 예정: " + row["filename"].toString(), true);
        }
    }
    content += p(future, "Future work", false, true);
    QSet<QString> futureProjects;
    const auto reportDate = model["report_date"].toString();
    for (auto sectionValue : sections) {
        auto sectionModel = sectionValue.toObject();
        const auto project = sectionModel["project_id"].toString("legacy");
        if (futureProjects.contains(project)) continue;
        futureProjects.insert(project);
        auto items = confirmedFuture(config, reportDate, project);
        if (items.isEmpty()) continue;
        content += p(h2, sectionModel["project_heading"].toString());
        QString previousTopic;
        for (auto value : items) {
            auto item = value.toObject(); auto topic = item["topic"].toString();
            if (!topic.isEmpty() && topic != previousTopic) content += p(h3, topic);
            previousTopic = topic;
            content += p(normal, futureText(item));
        }
    }
    const auto raw = QString::fromUtf8(original);
    const auto bs = raw.indexOf('>', raw.indexOf("<w:body")) + 1, be = raw.lastIndexOf("</w:body>");
    const auto sectionStart = raw.lastIndexOf("<w:sectPr");
    if (bs <= 0 || be < bs || sectionStart < bs)
        throw std::runtime_error("Template body boundaries unavailable");
    const auto section = raw.mid(sectionStart, be - sectionStart);
    package["word/document.xml"] = (raw.left(bs) + koreanFonts(content) + section + raw.mid(be)).toUtf8();
    parse(package["word/document.xml"]);
    package["word/_rels/document.xml.rels"] = rel.toByteArray();
    package["[Content_Types].xml"] = types.toByteArray();
    auto settings = parse(package["word/settings.xml"]);
    auto update = settings.documentElement().firstChildElement("w:updateFields");
    if (update.isNull()) {
        update = settings.createElement("w:updateFields");
        settings.documentElement().appendChild(update);
    }
    update.setAttribute("w:val", "true");
    if (settings.documentElement().firstChildElement("w:doNotAutoCompressPictures").isNull())
        settings.documentElement().appendChild(settings.createElement("w:doNotAutoCompressPictures"));
    package["word/settings.xml"] = settings.toByteArray();
    writeZip(output, package);
}
} // namespace youngkly
