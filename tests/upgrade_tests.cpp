#include "Alignment.h"
#include "AtomMapping.h"
#include "Archive.h"
#include "FutureWork.h"
#include "GaussianStore.h"
#include "Narrative.h"
#include "Pipeline.h"
#include "Workspace.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTextStream>
#include <algorithm>
#include <stdexcept>
using namespace youngkly;
void check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        if (app.arguments().size() == 6 && app.arguments()[1] == "--analyze-fixture") {
            auto args = app.arguments(); auto model = loadConfig(args[2]); auto config = loadConfig(args[3]);
            auto sections = reportSections(model, config);
            for (int i = 0; i < sections.size(); ++i) sections[i] = analyze(sections[i].toObject(), config, args[5]);
            model["sections"] = sections; writeFile(args[4], QJsonDocument(model).toJson()); return 0;
        }
        QTemporaryDir temp; check(temp.isValid(), "Temporary workspace");
        const auto root = temp.filePath("서버 공백"), cache = temp.filePath("cache"); QDir().mkpath(root);
        const QString name = "a-ts-freq.out", bb = "a-ts-freq-bb.out";
        auto fixture = [](double spin, const QString &basis) {
            return QString(" #p ub3lyp/%1 freq\n\n SCF Done: E(UB3LYP) = -123.456 A.U.\n"
                " Mulliken charges and spin densities:\n 1 Co 0.0 %2\n\n Normal termination\n").arg(basis).arg(spin, 0, 'f', 3).toUtf8();
        };
        writeFile(root + "/" + name, fixture(0.111, "def2SVP"));
        writeFile(root + "/" + bb, fixture(1.222, "def2TZVPP"));
        QJsonObject plan{{"rows", QJsonArray{QJsonObject{{"kind", "output"}, {"filename", name}, {"bb_filename", bb}}}}};
        QJsonObject config{{"servers", QJsonArray{QJsonObject{{"name", "lion"}, {"local_root", root}}}}};
        auto cold = resolveFiles(plan, config, cache);
        check(cold["metrics"].toObject()["downloads"].toInt() == 2, "Cold downloads");
        auto warm = resolveFiles(plan, config, cache);
        check(warm["metrics"].toObject()["downloads"].toInt() == 0 && warm["metrics"].toObject()["file_cache_hits"].toInt() == 2, "Warm transfers are zero");
        auto source = warm["rows"].toArray()[0].toObject()["primary_source"].toObject();
        const auto local = source["local_path"].toString();
        QJsonObject first, second;
        auto parsed = cachedGaussian(local, cache, false, first); auto cached = cachedGaussian(local, cache, false, second);
        check(first["gaussian_parses"].toInt() == 1 && second["gaussian_parses"].toInt() == 0 && second["parse_cache_hits"].toInt() == 1, "Persistent raw parse cache");
        check(parsed.scfEnergy == cached.scfEnergy && parsed.spinDensities.size() == cached.spinDensities.size() && parsed.normalTermination == cached.normalTermination, "Parse cache values preserved");
        writeFile(root + "/" + bb, fixture(2.333, "def2TZVPP") + "changed size\n");
        auto changed = resolveFiles(plan, config, cache);
        check(changed["metrics"].toObject()["downloads"].toInt() == 1, "Only changed BB transferred");
        QJsonObject third; auto revised = cachedGaussian(local, cache, false, third);
        check(third["gaussian_parses"].toInt() == 1 && revised.spinDensities.size() == 1 && std::abs(revised.spinDensities[0].spin - 2.333) < 1e-8, "Paired BB invalidates primary parse");
        writeFile(local, "bad local bytes");
        auto repaired = resolveFiles(plan, config, cache);
        check(repaired["metrics"].toObject()["downloads"].toInt() == 1, "Local corrupt cache repaired");
        auto forced = config; forced["force_refresh"] = true;
        check(resolveFiles(plan, forced, cache)["metrics"].toObject()["downloads"].toInt() == 2, "Force refresh");
        QDir().mkpath(root + "/duplicate"); writeFile(root + "/duplicate/" + name, "duplicate");
        check(resolveFiles(plan, config, cache)["rows"].toArray()[0].toObject()["primary_source"].toObject()["status"] == "ambiguous", "Warm cache never hides new duplicates");
        auto otherRoot = temp.filePath("project-B"); QDir().mkpath(otherRoot); writeFile(otherRoot + "/" + name, "other research");
        auto otherConfig = config;
        otherConfig["servers"] = QJsonArray{QJsonObject{{"name", "lion"}, {"local_root", otherRoot}}};
        auto other = resolveFiles(plan, otherConfig, cache)["rows"].toArray()[0].toObject()["primary_source"].toObject();
        check(other["local_path"] != source["local_path"] && readFile(other["local_path"].toString()) == "other research", "Different research roots never share a basename-only cache");
        QVector<QVector3D> a{{0,0,0},{1,0,0},{0,2,0},{0,0,3}}, b;
        QMatrix4x4 known; known.translate(4, -3, 2); known.rotate(81, QVector3D(1, 2, 3));
        for (auto p : a) b << known.map(p);
        auto transform = rigidAlignment(a, b);
        check(std::abs(transform.determinant() - 1) < 1e-4, "Alignment proper rotation");
        for (int i = 0; i < a.size(); ++i) check((transform.map(a[i]) - b[i]).length() < 1e-4, "Rigid alignment recovers rotated fixture");
        bool rejected = false; try { rigidAlignment({{0,0,0},{1,0,0},{2,0,0}}, {{0,0,0},{1,0,0},{2,0,0}}); } catch (...) { rejected = true; }
        check(rejected, "Collinear anchors rejected");
        auto maps = atomCorrespondence({{8,6,1},{6,8,1},{1,6,8}}, QJsonArray{QJsonArray{2,1,3},QJsonArray{1,2,3},QJsonArray{2,3,1}});
        check(atomCorrespondence({{6,8}}, {}).size() == 1 && atomCorrespondence({{6,8},{6,8}}, {}).size() == 2,
              "Spin optimization mapping supports one or two structures");
        check(maps[0][0] == 2 && maps[2][0] == 2, "Explicit permutation uses TS atom IDs");
        rejected = false; try { atomCorrespondence({{6,8},{6,8},{6,8}}, QJsonArray{QJsonArray{1,1},QJsonArray{1,2},QJsonArray{1,2}}); } catch (...) { rejected = true; }
        check(rejected, "Duplicate atom mapping rejected");
        QJsonArray headers{"Atom", "IRC forward", "TS", "IRC reverse"};
        auto desc = [&](QJsonArray row, QString kind = "spin", int n = 1, double threshold = 0.2) { return describeChange(row, headers, kind, n, threshold, {}); };
        check(desc({"Co1", 1.0, 0.5, 0.0})["rule"] == "spin_decrease", "Monotonic spin");
        check(desc({"Co1", 0.5, 1.0, 0.5})["rule"] == "spin_ts_peak", "TS-local peak");
        check(desc({"Co1", 1.0, 0.0, 0.5})["rule"] == "spin_ts_valley", "TS-local valley");
        check(desc({"Co1", -0.5, 0.0, 0.5})["sign_change"].toBool(), "Spin sign transition");
        check(!desc({"Co1", -0.0001, 0.0, 0.0001})["sign_change"].toBool(), "Zero noise not sign transition");
        check(desc({"Co1", QJsonValue(QJsonValue::Null), 1.0, 0.0}).isEmpty(), "Missing not zero");
        check(reportNumber(-0.0001) == "0.000", "No negative zero");
        auto periodic = desc({"A–B–C–D (°)", 179.0, 180.0, -179.0}, "geometry", 4, 5);
        check(periodic["rule"] == "geometry_small" && !periodic["text"].toString().contains("358"), "Periodic dihedral crossing");
        check(desc({"Co1", 0.0, 0.0, 0.0})["text"].toString().contains("0.000으로"), "Numeric Korean particle");
        check(desc({"A–B–C (°)", 90.0, 100.0, 110.0}, "geometry", 3, 5)["text"].toString().contains("각도"), "Angle units and subject");
        const auto spinPhrases = temp.filePath("spin-phrases.json"); writeFile(spinPhrases, "{}");
        for (int count : {1, 2, 3, 4}) {
            QJsonArray stateHeaders{"Atom"}, spinRow{"Mn1"};
            for (int i = 0; i < count; ++i) { stateHeaders << QString("Multiplicity %1").arg(i * 2 + 1); spinRow << double(i); }
            QJsonObject states{{"kind", "spin_optimization"}, {"atom_mapping_valid", true}, {"atom_mapping_confirmed", true},
                {"spin_table", QJsonObject{{"headers", stateHeaders}, {"rows", QJsonArray{spinRow}}}}};
            rebuildNarratives(states, QJsonObject{{"phrases_file", spinPhrases}});
            const auto paragraphs = states["paragraphs"].toArray();
            check(paragraphs.size() == 1 && paragraphs[0].toObject()["raw_values"].toArray().size() == count,
                  "Spin state narratives include every multiplicity for arbitrary group size");
            check(!paragraphs[0].toObject()["text"].toString().contains("TS") && !paragraphs[0].toObject()["text"].toString().contains("경로"),
                  "Spin alternatives are never described as a TS reaction path");
        }
        QJsonObject item{{"report_date", "2026-10-21"}, {"project_id", "test"}, {"source", "test-only user note"},
                          {"confirmed", true}, {"next_action", "다음 단계의 계산을 진행하고자 합니다."}};
        const auto future = temp.filePath("plans.json");
        auto proposal = item; proposal["confirmed"] = false;
        auto old = item; old["report_date"] = "2026-10-14";
        writeFile(future, QJsonDocument(QJsonObject{{"items", QJsonArray{item, proposal, old}}}).toJson());
        QJsonObject fc{{"future_work_file", future}};
        check(confirmedFuture(fc, "2026-10-21", "test").size() == 1, "Only confirmed current-date plans");
        check(confirmedFuture(fc, "2026-10-21", "other").isEmpty(), "Project-scoped plans");
        item["next_action"] = "{filename} 작성해주세요"; check(futureText(item).isEmpty(), "No placeholders");
        QTextStream(stdout) << "Upgrade cache, BB invalidation, alignment, narrative and Future work checks passed\n";
        return 0;
    } catch (const std::exception &e) { QTextStream(stderr) << e.what() << "\n"; return 1; }
}
