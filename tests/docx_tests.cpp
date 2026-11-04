#include "Archive.h"
#include "Docx.h"
#include "RelativeTable.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
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
            QJsonArray{"Reactant", 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
            QJsonArray{"TS", 10.0, 1.0, 11.0, 0.2, 0.1, -0.1, 0.0, 11.2},
            QJsonArray{"Product", -5.0, 0.0, -5.0, 0.1, 0.0, 0.0, 0.0, -4.9}}, true, true);
        const QJsonObject spin{{"headers", QJsonArray{"Atom", "Reactant", "TS", "Product"}},
            {"rows", QJsonArray{QJsonArray{"Co1", 0.0, 0.5, 1.0},
                                 QJsonArray{"O2", 0.0, 0.1, 0.2},
                                 QJsonArray{"Rest", 0.0, 0.0, 0.0},
                                 QJsonArray{"Sum", 0.0, 0.6, 1.2}}}};
        const QJsonObject geometry{{"headers", QJsonArray{"Distance (Å)", "Reactant", "TS", "Product"}},
            {"rows", QJsonArray{QJsonArray{"Co1−O2", 1.9, 2.0, 2.2}}},
            {"atom_ids", QJsonArray{QJsonArray{1, 2}}}};
        const QJsonObject group{{"kind", "reaction"}, {"section", "SI format smoke test"},
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
        verify(xml.count("w:fldSimple") >= 4, "Sequence captions absent");
        verify(xml.contains("Co1") && xml.contains("Reactant") && xml.contains("Mulliken"), "Spin table missing");
        verify(xml.contains("w:sz=\\\"6\\\""), "Three-rule table border missing");
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
