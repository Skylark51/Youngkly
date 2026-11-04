#include "Archive.h"
#include "CaptureCrop.h"
#include "Pipeline.h"
#include "Sheet.h"
#include "Workspace.h"
#include "RelativeTable.h"
#include "si/GaussianParser.hpp"
#include "si/SIModel.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTextStream>
#include <cmath>
#include <stdexcept>
using namespace youngkly;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temp;
        check(temp.isValid(), "temp");
        QImage white(200, 160, QImage::Format_ARGB32);
        white.fill(Qt::white);
        auto firstFrame = white, lastFrame = white;
        firstFrame.setPixelColor(30, 40, Qt::red);
        firstFrame.setPixelColor(50, 60, QColor(250, 250, 250));
        lastFrame.setPixelColor(170, 130, Qt::blue);
        const auto crop = captureCrop({firstFrame, lastFrame});
        check(crop.rect == QRect(26, 36, 149, 99) && crop.marginPixels == 4,
              "Animation crop includes all motion with a small common margin");
        check(firstFrame.copy(crop.rect).size() == lastFrame.copy(crop.rect).size(),
              "Animation crop dimensions are stable");
        check(captureCrop({white}).rect == white.rect(), "Blank capture remains valid");
        auto transparent = white;
        transparent.fill(Qt::transparent);
        transparent.setPixelColor(100, 80, QColor(200, 200, 200));
        check(captureCrop({transparent}).rect == QRect(96, 76, 9, 9),
              "Transparent background does not affect crop bounds");
        firstFrame.setPixelColor(0, 0, Qt::black);
        check(captureCrop({firstFrame}).rect.topLeft() == QPoint(0, 0),
              "Crop preserves foreground at viewport edges");
        QJsonArray sheetEnergyRows{QJsonArray{"TS", 1.0, 0.2, 1.2, 0.3, -0.4, 0.5, 0.0, 1.6}};
        auto siTable = siRelativeTable(sheetEnergyRows, true, true);
        check(siTable["headers"].toArray() == QJsonArray{"", "ΔE", "ΔΔE", "E_{Total}", "ΔZ_{0}",
                                                         "ΔΔE_{Thermal}", "-TΔS", "Δ_{Dispersion}",
                                                         "Δ_{Complexation}", "ΔG"},
              "Exact SI relative headers");
        check(siTable["rows"].toArray()[0].toArray()[3] == 1.2 &&
                  siTable["rows"].toArray()[0].toArray()[9] == 1.6,
              "Preserve SI electronic total and Gibbs components");
        check(siTable["bold_columns"].toArray() == QJsonArray{3, 9} && relativeCellText(0.2, 1) == "+0.20" &&
                  relativeCellText(0.0, 6) == "+0.00" && relativeCellText(1.2, 2) == "1.20",
              "SI correction signs and emphasis");
        const auto siSpinLayout = siDocumentOrientation(QJsonObject{
            {"headers", QJsonArray{"Atom", "Singlet", "Triplet"}},
            {"rows", QJsonArray{QJsonArray{"Co1", 0.0, 1.5},
                                 QJsonArray{"O2", 0.0, 0.5},
                                 QJsonArray{"Rest", 0.0, 0.0}}}});
        check(siSpinLayout["headers"].toArray() == QJsonArray{"Atom", "Co1", "O2", "Rest"} &&
              siSpinLayout["rows"].toArray()[1].toArray() == QJsonArray{"Triplet", 1.5, 0.5, 0.0},
              "SI Generator document table is structure-per-row");
        check(siRelativeTable(sheetEnergyRows, false, false)["headers"].toArray().size() == 8,
              "SI optional column selection");
        Archive zip{{"a.xml", "<a>한글</a>"}, {"b.txt", "fixture"}};
        writeZip(temp.filePath("test.zip"), zip);
        check(readZip(temp.filePath("test.zip")) == zip, "ZIP round trip");
        Sheet s;
        s.name = "test";
        s.formats[2] = "m/d";
        check(reportDate({"I1", "46316", "", "", 2}, s, 2026) == QDate(2026, 10, 21), "Excel date");
        check(!reportDate({"I1", "-0.12", "", "", 0}, s, 2026).isValid(), "Dispersion is not a date");
        check(!reportDate({"I1", "46316", "", "", 0}, s, 2026).isValid(),
              "Numeric value without date format");
        check(reportDate({"I1", "10/21", "", "s", 0}, s, 2026) == QDate(2026, 10, 21), "String date");
        check(!reportDate({"I1", "2/30", "", "s", 0}, s, 2026).isValid(), "Invalid date");
        check(translateFormula("(D30-D$30)*627.5095", "L30", "L31") == "(D31-D$30)*627.5095",
              "Shared formula relative row");
        check(translateFormula("$D30+D$30+$D$30+SUM(A1:B2)+\"A1\"", "L30", "M31") ==
                  "$D31+E$30+$D$30+SUM(B2:C3)+\"A1\"",
              "Absolute references and string literals");
        check(bbName("a-ircfwd-opt-freq.out") == "a-ircfwd-opt-freq-bb.out", "BB endpoint");
        check(bbName("a-freq-bb.out").isEmpty(), "No double BB");
        {
            auto gaussian = [](bool rotated, bool deformed) {
                return QString(" #p ub3lyp/%1 freq %4\n\n SCF Done: E(UB3LYP) = -10.0 A.U.\n"
                    " Standard orientation:\n -----\n Center Atomic Atomic Coordinates\n Number Number Type X Y Z\n -----\n"
                    " 1 25 0 0.0 0.0 0.0\n 2 8 0 %2\n 3 1 0 %3\n -----\n"
                    " Mulliken charges and spin densities:\n 1 Mn 0.0 1.222\n 2 O 0.0 0.1\n 3 H 0.0 0.0\n\n Normal termination\n")
                    .arg(rotated ? "def2tzvpp" : "def2svp",
                         rotated ? (deformed ? "0.0 1.5 0.0" : "0.0 1.0 0.0") : "1.0 0.0 0.0",
                         rotated ? "-1.0 0.0 0.0" : "0.0 1.0 0.0")
                    .arg(rotated ? "EmpiricalDispersion=GD3BJ" : "EmpiricalDispersion= GD3BJ").toUtf8();
            };
            const auto primary = temp.filePath("rotated-opt-freq.out"), high = temp.filePath("rotated-opt-freq-bb.out");
            writeFile(primary, gaussian(false, false)); writeFile(high, gaussian(true, false));
            const auto phrases = temp.filePath("phrases.json"); writeFile(phrases, "{}");
            QJsonObject row{{"sheet_row", 1}, {"filename", "rotated-opt-freq.out"},
                {"analysis", QJsonObject{{"issues", QJsonArray{"Stale BB warning"}}}},
                {"primary_source", QJsonObject{{"status", "found"}, {"local_path", primary}}},
                {"bb_source", QJsonObject{{"status", "found"}, {"local_path", high}}}};
            QJsonObject fixture{{"rows", QJsonArray{row}}, {"spin_groups", QJsonArray{QJsonObject{
                {"kind", "spin_optimization"}, {"section", "spin"}, {"rows", QJsonArray{1}},
                {"structure_labels", QJsonArray{"Multiplicity 3"}}}}}};
            QJsonObject settings{{"phrases_file", phrases}};
            const auto accepted = analyze(fixture, settings, temp.filePath("analysis-cache"));
            check(accepted["rows"].toArray()[0].toObject()["analysis"].toObject()["scf_tzvpp_hartree"].isDouble(),
                  "Rotated identical BB geometry retains TZVPP energy and spin");
            check(accepted["analyses"].toArray()[0].toObject()["spin_table"].toObject()["rows"].toArray()[0].toArray()[0] == "Mn",
                  "Spin table displays element symbols without atom IDs");
            check(!accepted["analyses"].toArray()[0].toObject()["issues"].toArray().contains("Stale BB warning"),
                  "Reanalysis discards outdated per-row issues");
            writeFile(high, gaussian(true, true));
            const auto rejectedBB = analyze(fixture, settings, temp.filePath("analysis-cache"));
            check(rejectedBB["rows"].toArray()[0].toObject()["analysis"].toObject()["scf_tzvpp_hartree"].isNull(),
                  "Different BB geometry remains excluded after rotation alignment");
        }
        const auto frequencyPath = temp.filePath("spin-freq.out");
        writeFile(frequencyPath,
                  " #p ub3lyp/def2svp freq\n\n SCF Done: E(UB3LYP) = -10.0 A.U.\n"
                  " Mulliken charges and spin densities:\n 1 Co 0.0 0.111\n\n Normal termination\n");
        writeFile(temp.filePath("spin-freq-bb.out"),
                  " #p ub3lyp/def2tzvpp sp\n\n SCF Done: E(UB3LYP) = -10.1 A.U.\n"
                  " Mulliken charges and spin densities:\n 1 Co 0.0 1.222\n\n Normal termination\n");
        const auto parsed = si::GaussianParser().parse(std::filesystem::path(frequencyPath.toStdWString()));
        check(parsed.scfEnergy && std::abs(*parsed.scfEnergy + 10.0) < 1e-10,
              "Paired BB must not overwrite primary SCF energy");
        check(parsed.spinBasisLevel == "TZVPP" && parsed.spinDensities.size() == 1 &&
                  std::abs(parsed.spinDensities[0].spin - 1.222) < 1e-10,
              "Nonzero spin must come from BB");
        std::vector<si::Atom> geometry{
            {6, "C", {1, 0, 0}}, {27, "Co", {0, 0, 0}}, {8, "O", {0, 1, 0}}, {1, "H", {0, 1, 1}}};
        check(std::abs(*si::distance(geometry, 1, 2) - 1) < 1e-10 &&
                  std::abs(*si::angle(geometry, 1, 2, 3) - 90) < 1e-10 &&
                  std::abs(std::abs(*si::dihedral(geometry, 1, 2, 3, 4)) - 90) < 1e-10,
              "SI distance, angle and dihedral units");
        const QStringList names{"a-ts-freq-ircrev-opt-freq.out", "a-ts-freq.out",
                                "a-ts-freq-ircfwd-opt-freq.out", "ongoing.inp"};
        for (int i = 0; i < 4; ++i) {
            SheetRow r;
            r.number = i + 3;
            r.cells["A"] = {"A", names[i], {}, "s"};
            r.cells["I"] = {"I", "10/21", {}, "s"};
            s.rows << r;
        }
        auto plan = makePlan(s, QDate(2026, 10, 21));
        check(plan["rows"].toArray().size() == 4, "Preserve dated inputs");
        check(plan["triplets"].toArray().size() == 1, "Triplet detection");
        Sheet spinSheet; spinSheet.name = "spin comparisons";
        const QStringList spinNames{"1-Mn-peroxo-opt-freq.out", "3-Mn-peroxo-opt-freq.out",
            "5-Mn-peroxo-opt-freq.out", "2-other-opt-freq.log", "4-other-opt-freq.log",
            "7-single-opt-freq.out", "9-other-opt-freq.inp"};
        for (int i = 0; i < spinNames.size(); ++i) {
            SheetRow row; row.number = i + 3;
            row.cells["A"] = {"A", spinNames[i], {}, "s"};
            row.cells["J"] = {"J", "11/5", {}, "s"}; spinSheet.rows << row;
        }
        auto spinPlan = makePlan(spinSheet, QDate(2026, 11, 5));
        auto spinGroups = spinPlan["spin_groups"].toArray();
        check(spinGroups[0].toObject()["structure_labels"].toArray() == QJsonArray{"S = 0", "S = 1", "S = 2"}, "Spin quantum number labels");
        check(spinGroups.size() == 3 && spinGroups[0].toObject()["rows"].toArray() == QJsonArray{3,4,5},
              "Spin optimized groups preserve sheet order and separate different structures");
        check(spinGroups[1].toObject()["multiplicities"].toArray() == QJsonArray{2,4} &&
              spinGroups[2].toObject()["rows"].toArray().size() == 1 && spinPlan["triplets"].toArray().isEmpty(),
              "Log outputs, two-state and single-state optimizations are included without a TS");
        spinSheet.rows << spinSheet.rows[0];
        check(makePlan(spinSheet, QDate(2026,11,5))["spin_groups"].toArray()[0].toObject()["duplicate_multiplicity"].toBool(),
              "Duplicate multiplicities require review instead of silently selecting a row");
        auto dualDates = s;
        dualDates.rows[0].cells.remove("I");
        dualDates.rows[0].cells["J"] = {"J3", "46316", {}, "", 2};
        dualDates.rows[1].cells["I"].text = "10/14";
        dualDates.rows[1].cells["J"] = {"J4", "10/21", {}, "s"};
        dualDates.rows[2].cells["J"] = {"J5", "10/21", {}, "s"};
        dualDates.rows[3].cells["I"].text = "invalid";
        dualDates.rows[3].cells["J"] = {"J6", "2026-10-21", {}, "s"};
        const auto dualPlan = makePlan(dualDates, QDate(2026, 10, 21));
        check(dualPlan["rows"].toArray().size() == 4,
              "Either I or J selects each row once, including missing or invalid I");
        check(dualPlan["available_dates"].toArray() == QJsonArray{"2026-10-21", "2026-10-14"},
              "Available dates include both columns without duplicates");
        check(dualPlan["triplets"].toArray().size() == 1,
              "Triplets can use dates across I and J");
        const auto earlierPlan = makePlan(dualDates, QDate(2026, 10, 14));
        check(earlierPlan["rows"].toArray().size() == 1 &&
                  earlierPlan["rows"].toArray()[0].toObject()["date"] == "2026-10-14" &&
                  dualPlan["rows"].toArray()[1].toObject()["date"] == "2026-10-21",
              "Rows with different I and J dates use the selected report date");
        check(makePlan(dualDates, QDate(2026, 10, 28))["rows"].toArray().isEmpty(),
              "Neither column matches an unrelated date");
        auto forwardFirst = s;
        std::swap(forwardFirst.rows[0].cells["A"].text, forwardFirst.rows[2].cells["A"].text);
        auto forwardPlan = makePlan(forwardFirst, QDate(2026, 10, 21));
        check(forwardPlan["triplets"].toArray().size() == 1 &&
                  forwardPlan["rows"].toArray()[0].toObject()["role"] == "irc_forward",
              "Forward-first triplet retains spreadsheet order");
        check(displayHeading("{FeNO}^{6} / {CoNO}^{7}") == "{FeNO}⁶ / {CoNO}⁷", "Superscript project labels");
        auto workspace = QJsonObject{{"sections", QJsonArray{plan, forwardPlan}}};
        check(selectedRowCount(workspace) == 8 && tripletCount(workspace) == 2,
              "Sections with repeated row IDs remain independent");
        auto unrelated = s;
        unrelated.rows[2].cells["A"].text = "different-ts-freq-ircfwd-opt-freq.out";
        check(makePlan(unrelated, QDate(2026, 10, 21))["triplets"].toArray().isEmpty(),
              "Reject unrelated endpoints");
        const auto lion = temp.filePath("lion"), dumbo = temp.filePath("dumbo"),
                   tachyon = temp.filePath("tachyon");
        QDir().mkpath(lion);
        QDir().mkpath(dumbo);
        QDir().mkpath(tachyon);
        for (const auto &name : names.mid(0, 3))
            writeFile(lion + "/" + name, "primary");
        for (const auto &name : names.mid(0, 3))
            writeFile(dumbo + "/" + bbName(name), "single point");
        auto config =
            QJsonObject{{"servers", QJsonArray{QJsonObject{{"name", "lion"}, {"local_root", lion}},
                                               QJsonObject{{"name", "dumbo"}, {"local_root", dumbo}},
                                               QJsonObject{{"name", "tachyon"}, {"local_root", tachyon}}}}};
        auto resolved = resolveFiles(plan, config, temp.filePath("cache"));
        auto audit = resolved["search_audit"].toArray();
        check(audit.size() == 2, "Skip tachyon when all outputs found");
        for (const auto &v : audit[1].toObject()["requested"].toArray())
            check(v.toString().contains("-bb.out"), "Only unresolved BB falls back");
        check(resolved["rows"].toArray()[0].toObject()["primary_source"].toObject()["server"] == "lion",
              "Preferred server");
        QDir().mkpath(lion + "/duplicate");
        writeFile(lion + "/duplicate/" + names[0], "other");
        auto duplicate = resolveFiles(plan, config, temp.filePath("cache2"));
        check(duplicate["rows"].toArray()[0].toObject()["primary_source"].toObject()["status"] == "ambiguous",
              "Never choose arbitrary duplicate");
        auto badConfig = config;
        auto servers = config["servers"].toArray();
        auto badLion = servers[0].toObject();
        badLion["local_root"] = temp.filePath("unavailable");
        servers[0] = badLion;
        badConfig["servers"] = servers;
        auto unavailable = resolveFiles(plan, badConfig, temp.filePath("cache3"));
        check(unavailable["search_audit"].toArray().size() == 1 &&
                  unavailable["rows"].toArray()[0].toObject()["primary_source"].toObject()["status"] ==
                      "unresolved",
              "An unavailable preferred server is not proof of absence");
        QTextStream(stdout) << "All core checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        QTextStream(stderr) << e.what() << "\n";
        return 1;
    }
}
