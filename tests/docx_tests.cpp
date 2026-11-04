#include "Archive.h"
#include "Docx.h"
#include "RelativeTable.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QDomDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>
#include <stdexcept>

using namespace youngkly;

static void verify(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temp;
        verify(temp.isValid(), "Temporary folder missing");
        QDir root(QCoreApplication::applicationDirPath());
        verify(root.cdUp(), "Source folder unavailable");
        const QString templatePath = root.filePath(QString::fromUtf8("주간_연구_보고서_이영섭_10월_3주차_261014.docx"));
        verify(QFileInfo::exists(templatePath), "Reference DOCX fixture missing");
        QJsonArray captures;
        const QStringList roles{"irc_reverse", "transition_state", "irc_forward"};
        QJsonArray sourceRows;
        for (int i = 0; i < 3; ++i) {
            const auto path = temp.filePath(QString("figure-%1.png").arg(i));
            QImage image(128, 128, QImage::Format_ARGB32);
            image.fill(Qt::white);
            verify(image.save(path, "PNG"), "PNG fixture failed");
            captures << QJsonObject{{"png", path}};
            sourceRows << QJsonObject{{"sheet_row", i + 3}, {"kind", "output"},
                {"filename", QString("stage-%1.out").arg(i)}, {"role", roles[i]}};
        }
        const auto energy = siRelativeTable(QJsonArray{
            QJsonArray{"S = 0", 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
            QJsonArray{"S = 1", 10.0, 1.0, 11.0, 0.2, 0.1, -0.1, 0.0, 11.2},
            QJsonArray{"S = 2", -5.0, 0.0, -5.0, 0.1, 0.0, 0.0, 0.0, -4.9}}, true, true);
        const QJsonObject spin{{"headers", QJsonArray{"Atom", "S = 0", "S = 1", "S = 2"}},
            {"rows", QJsonArray{QJsonArray{"Co", 0.0, 0.5, 1.0},
                                 QJsonArray{"O", 0.0, 0.1, 0.2},
                                 QJsonArray{"Rest", 0.0, 0.0, 0.0},
                                 QJsonArray{"Sum", 0.0, 0.6, 1.2}}}};
        const QJsonObject geometry{{"headers", QJsonArray{"Distance (Å)", "S = 0", "S = 1", "S = 2"}},
            {"rows", QJsonArray{QJsonArray{"Co−O", 1.9, 2.0, 2.2}}},
            {"atom_ids", QJsonArray{QJsonArray{1, 2}}}};
        const QJsonObject group{{"kind", "spin_optimization"}, {"structure_labels", QJsonArray{"S = 0", "S = 1", "S = 2"}}, {"section", "SI format smoke test"},
            {"status", "draft_requires_review"}, {"rows", QJsonArray{3, 4, 5}},
            {"captures", captures}, {"energy_table", energy}, {"spin_table", spin},
            {"geometry_table", geometry}};
        const QJsonObject section{{"project_id", "si-smoke"}, {"project_heading", "SI Generator comparison"},
            {"rows", sourceRows}, {"analyses", QJsonArray{group}}};
        const QJsonObject model{{"report_date", "2026-10-21"},
            {"sections", QJsonArray{section}}};
        const QJsonObject config{{"image_width_cm", 5.0},
            {"future_work_file", temp.filePath("empty-future.json")}};
        const auto out = temp.filePath("report.docx");
        writeReport(templatePath, out, model, config);
        const auto parts = readZip(out);
        const QString xml = QString::fromUtf8(parts.value("word/document.xml"));
        verify(xml.count("<w:tbl>") == 3, "Missing three editable SI-format tables");
        verify(xml.count("SEQ Table_S") == 3 && xml.count("SEQ Figure_S") == 1, "Sequence captions absent");
        verify(xml.contains("Co") && xml.contains("S = 0") && xml.contains("Mulliken"), "Spin table missing");
        verify(xml.contains("w:sz=\"6\""), "Three-rule table border missing");
        verify(!xml.contains("스핀 상태별 최적화 구조:"), "Unnecessary spin structure sentence");
        verify(!xml.contains("E Total ="), "Weekly report must omit footnote explanations");
        verify(xml.contains("w:tblLayout w:type=\"fixed\""), "Stable table columns");
        QDomDocument document; verify(bool(document.setContent(parts.value("word/document.xml"))), "Invalid Word XML");
        int italicStates = 0, captions = 0;
        const auto runs = document.elementsByTagName("w:r");
        for (int i = 0; i < runs.size(); ++i) {
            const auto run = runs.at(i).toElement();
            const auto text = run.firstChildElement("w:t").text();
            if (text.startsWith("S = ")) {
                ++italicStates;
                verify(!run.firstChildElement("w:rPr").firstChildElement("w:i").isNull(), "Spin state is not italic");
            }
            if (text == ". ") {
                ++captions; verify(run.firstChildElement("w:t").attribute("xml:space") == "preserve", "Caption space lost");
            }
            if (run.firstChildElement("w:instrText").text().contains("SEQ "))
                verify(!run.firstChildElement("w:rPr").firstChildElement("w:b").isNull(), "Caption field instruction must stay bold");
        }
        verify(italicStates >= 9 && captions == 4, "Caption or italic spin labels absent");
        const auto headers = energy["headers"].toArray();
        for (const auto &header : headers) verify(!header.toString().contains("^{"), "Weekly energy footnote markers");
        int exportedPng = 0;
        for (const auto &name : parts.keys()) {
            if (name.startsWith("word/media/youngkly-") && name.endsWith(".png")) ++exportedPng;
            verify(!name.startsWith("docProps/"), "Old template metadata leaked");
        }
        verify(exportedPng == 3, "PNG stills missing from Word package");
        verify(!parts.value("_rels/.rels").contains("docProps/"), "Stale metadata relationship");
        QTextStream(stdout) << "SI Word table, caption, PNG, metadata smoke passed\n";
        return 0;
    } catch (const std::exception &e) {
        QTextStream(stderr) << e.what() << "\n";
        return 1;
    }
}
