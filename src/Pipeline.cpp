#include "Pipeline.h"
#include "GaussianStore.h"
#include "AtomMapping.h"
#include "Alignment.h"
#include "Narrative.h"
#include <QElapsedTimer>
#include "RelativeTable.h"
#include "Archive.h"
#include "si/GaussianParser.hpp"
#include "si/SIModel.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace youngkly {
QString shellQuote(QString v) {
    return "'" + v.replace("'", "'\"'\"'") + "'";
}
QString bbName(const QString &name) {
    if (!name.endsWith("freq.out", Qt::CaseInsensitive) && !name.endsWith("freq.log", Qt::CaseInsensitive))
        return {};
    return name.left(name.size() - 4) + "-bb" + name.right(4);
}
QJsonObject loadConfig(const QString &path) {
    QJsonParseError e;
    auto d = QJsonDocument::fromJson(readFile(path), &e);
    if (e.error != QJsonParseError::NoError || !d.isObject())
        throw std::runtime_error("Invalid JSON config");
    return d.object();
}
namespace {
QJsonValue number(std::optional<double> v) {
    return v ? QJsonValue(*v) : QJsonValue(QJsonValue::Null);
}
bool metal(int z) {
    return (z >= 21 && z <= 30) || (z >= 39 && z <= 48) || (z >= 57 && z <= 80) || (z >= 89);
}
double contactLimit(int a, int b) {
    if (a == 1 && b == 1)
        return 0;
    if (metal(a) || metal(b))
        return 3.0;
    if (a == 1 || b == 1)
        return (a == 6 || b == 6) ? 1.35 : 1.8;
    if (a == 16 || b == 16 || a == 15 || b == 15)
        return 2.3;
    return 1.9;
}
} // namespace
QJsonObject analyze(QJsonObject plan, const QJsonObject &config, const QString &cache) {
    QElapsedTimer analysisTimer; analysisTimer.start();
    auto metrics = plan["metrics"].toObject();
    const auto phrases = loadConfig(config["phrases_file"].toString());
    auto rows = plan["rows"].toArray();
    std::map<int, si::EnergyRecord> records;
    QMap<int, QJsonObject> sheetRows;

    for (int i = 0; i < rows.size(); ++i) {
        auto row = rows[i].toObject();
        const auto primary = row["primary_source"].toObject(), bb = row["bb_source"].toObject();
        int id = row["sheet_row"].toInt();
        sheetRows[id] = row;
        if (primary["status"] != "found" || primary["local_path"].toString().isEmpty())
            continue;
        si::EnergyRecord record;
        record.name = row["filename"].toString().toStdString();
        record.frequency = cachedGaussian(primary["local_path"].toString(), cache, config["force_refresh"].toBool(), metrics);
        QJsonArray issues;
        if (!record.frequency.success || !record.frequency.normalTermination)
            issues << "Primary calculation incomplete or not recognized";
        if (bb["status"] == "found") {
            record.singlePoint = cachedGaussian(bb["local_path"].toString(), cache, config["force_refresh"].toBool(), metrics);
            const auto &high = *record.singlePoint;
            bool aligned = high.finalGeometry.size() == record.frequency.finalGeometry.size() &&
                           !high.finalGeometry.empty();
            bool sameFrame = true;
            if (aligned)
                for (size_t k = 0; k < high.finalGeometry.size(); ++k) {
                    if (high.finalGeometry[k].atomicNumber != record.frequency.finalGeometry[k].atomicNumber)
                        aligned = false;
                    for (int d = 0; d < 3; ++d)
                        if (std::abs(high.finalGeometry[k].xyz[d] -
                                     record.frequency.finalGeometry[k].xyz[d]) > 0.002)
                            sameFrame = false;
                }
            if (aligned && !sameFrame) {
                QVector<QVector3D> source, target;
                for (size_t k = 0; k < high.finalGeometry.size(); ++k) {
                    const auto &a = high.finalGeometry[k], &b = record.frequency.finalGeometry[k];
                    source << QVector3D(a.xyz[0], a.xyz[1], a.xyz[2]);
                    target << QVector3D(b.xyz[0], b.xyz[1], b.xyz[2]);
                }
                try {
                    const auto transform = rigidAlignment(source, target);
                    for (int k = 0; k < source.size(); ++k)
                        if ((transform.map(source[k]) - target[k]).length() > 0.002) aligned = false;
                } catch (...) { aligned = false; }
            }
            if (high.success && high.normalTermination && high.basisLevel == "TZVPP" && aligned) {
                record.frequency.spinDensities = high.spinDensities;
                record.frequency.spinSourcePath = high.path;
                record.frequency.spinBasisLevel = high.basisLevel;
                record.frequency.spinFallback = false;
            } else {
                issues << "BB basis, completion or atom correspondence requires review";
                record.singlePoint.reset();
                record.frequency.spinDensities.clear();
            }
        } else {
            issues << "TZVPP paired output missing";
            record.frequency.spinDensities.clear();
        }
        QJsonArray xyz;
        for (const auto &a : record.frequency.finalGeometry)
            xyz << QJsonObject{{"element", QString::fromStdString(a.symbol)},
                               {"x", a.xyz[0]},
                               {"y", a.xyz[1]},
                               {"z", a.xyz[2]}};
        const auto scfBB = record.singlePoint ? record.singlePoint->scfEnergy : std::nullopt;
        row["analysis"] =
            QJsonObject{{"normal_termination", record.frequency.normalTermination},
                        {"scf_svp_hartree", number(record.frequency.scfEnergy)},
                        {"scf_tzvpp_hartree", number(scfBB)},
                        {"spin_basis", QString::fromStdString(record.frequency.spinBasisLevel)},
                        {"spin_source", QString::fromStdWString(record.frequency.spinSourcePath.wstring())},
                        {"xyz", xyz},
                        {"issues", issues}};
        records[id] = std::move(record);
        rows[i] = row;
    }
    plan["rows"] = rows;
    QJsonArray analyses;
    auto reportGroups = plan["triplets"].toArray();
    for (auto g : plan["spin_groups"].toArray()) reportGroups << g;
    for (const auto &g : reportGroups) {
        auto group = g.toObject();
        const bool spinOptimization = group["kind"] == "spin_optimization";
        const int structureCount = group["rows"].toArray().size();
        std::vector<si::EnergyRecord> r;
        QVector<int> ids;
        for (const auto &v : group["rows"].toArray()) {
            ids << v.toInt();
            if (records.contains(v.toInt()))
                r.push_back(records.at(v.toInt()));
        }
        if (r.size() != size_t(structureCount) || r.empty() || group["duplicate_multiplicity"].toBool()) {
            group["status"] = "missing_outputs";
            analyses << group;
            continue;
        }
        QString mappingError;
        try {
            QVector<QVector<int>> elements;
            for (const auto &record : r) { QVector<int> e; for (const auto &a : record.frequency.finalGeometry) e << a.atomicNumber; elements << e; }
            const auto rule = config["reaction_rules"].toObject()[group["section"].toString()].toObject();
            const auto mapping = atomCorrespondence(elements, rule["atom_mapping"].toArray());
            group["atom_mapping"] = mappingJson(mapping);
            group["atom_id_reference"] = spinOptimization ? "Reference structure original atom IDs" : "TS original atom IDs";
            group["atom_mapping_valid"] = true;
            group["atom_mapping_confirmed"] = rule["atom_mapping_confirmed"].toBool();
            group["atom_correspondence"] = rule["atom_mapping_confirmed"].toBool() ? "user_confirmed" : "compatible_candidate_requires_confirmation";
            for (int i = 0; i < structureCount; ++i) {
                auto original = r[i].frequency.finalGeometry;
                QVector<int> inverse(original.size());
                for (int a = 0; a < mapping[i].size(); ++a) {
                    r[i].frequency.finalGeometry[a] = original[mapping[i][a] - 1]; inverse[mapping[i][a] - 1] = a + 1;
                }
                for (auto &spin : r[i].frequency.spinDensities)
                    if (spin.atomIndex >= 1 && spin.atomIndex <= inverse.size()) spin.atomIndex = inverse[spin.atomIndex - 1];
            }
        } catch (const std::exception &e) {
            mappingError = QString::fromUtf8(e.what()); group["atom_mapping_valid"] = false;
            group["atom_mapping_confirmed"] = false; group["atom_correspondence"] = "mapping_required";
        }
        const auto &first = r.front().frequency.finalGeometry;
        bool aligned = !first.empty() && mappingError.isEmpty();
        for (const auto &item : r) {
            if (item.frequency.finalGeometry.size() != first.size()) {
                aligned = false;
                break;
            }
            for (size_t a = 0; a < first.size(); ++a)
                if (first[a].atomicNumber != item.frequency.finalGeometry[a].atomicNumber)
                    aligned = false;
        }
        QJsonArray issues, paragraphs;
        if (!mappingError.isEmpty()) issues << mappingError;
        QStringList structureLabels;
        for (int i = 0; i < ids.size(); ++i)
            structureLabels << (spinOptimization ? group["structure_labels"].toArray()[i].toString() :
                                sheetRows[ids[i]]["role"] == "irc_forward"        ? "IRC forward"
                                : sheetRows[ids[i]]["role"] == "transition_state" ? "TS"
                                                                              : "IRC reverse");
        if (!aligned)
            issues << "Atom counts or element order differ; explicit atom mapping is required";
        // Sheet formulas and their cached results remain the authoritative profile.
        QJsonArray energyRows;
        const auto rel = si::legacyRelativeEnergies(r, r.front(), false, false);
        const QStringList cols{"L", "M", "N", "O", "P", "Q", "R", "S"};
        const int termIndex[]{0, 1, 2, 3, 4, 5, 6, 8};
        bool energyMatch = true, complete = true, arithmeticMatch = true;
        int checkedTerms = 0;
        QVector<double> narrativeValues;
        const auto narrativeColumn = config["narrative_energy_column"].toString("N");
        if (narrativeColumn != "N" && narrativeColumn != "S")
            throw std::runtime_error("Narrative energy column must be N (electronic) or S (Gibbs)");
        for (int i = 0; i < structureCount; ++i) {
            auto cells = sheetRows[ids[i]]["cells"].toObject();
            QJsonArray values{structureLabels[i]};
            for (int c = 0; c < cols.size(); ++c) {
                auto cell = cells[cols[c]].toObject();
                bool ok;
                double v = cell["value"].toString().toDouble(&ok);
                // Excel SUM ignores empty optional corrections; SI also defaults absent dispersion to zero.
                if (c == 6 && cell["value"].toString().trimmed().isEmpty()) {
                    v = 0.0;
                    ok = true;
                }
                values << (ok ? QJsonValue(v) : QJsonValue(QJsonValue::Null));
                if (!ok)
                    complete = false;
                if (c < 6 && ok && r[i].singlePoint && r.front().singlePoint &&
                    rel[i].kcalTerms[termIndex[c]] && rel[0].kcalTerms[termIndex[c]]) {
                    bool baseOk;
                    double base = sheetRows[ids[0]]["cells"]
                                      .toObject()[cols[c]]
                                      .toObject()["value"]
                                      .toString()
                                      .toDouble(&baseOk);
                    if (baseOk) {
                        ++checkedTerms;
                        if (std::abs((v - base) - *rel[i].kcalTerms[termIndex[c]]) >
                            config["energy_tolerance_kcal"].toDouble(0.02))
                            energyMatch = false;
                    }
                }
            }
            bool ok;
            double gv = cells[narrativeColumn].toObject()["value"].toString().toDouble(&ok);
            if (ok)
                narrativeValues << gv;
            if (complete) {
                double sum = 0;
                for (int c = 3; c <= 7; ++c)
                    sum += values[c].toDouble();
                if (std::abs(sum - values[8].toDouble()) > config["energy_tolerance_kcal"].toDouble(0.02) ||
                    std::abs(values[1].toDouble() + values[2].toDouble() - values[3].toDouble()) > 0.02)
                    arithmeticMatch = false;
            }
            energyRows << values;
        }
        if (!energyMatch)
            issues << "Sheet profile and SI component differences disagree beyond tolerance";
        if (!arithmeticMatch)
            issues << "Sheet component sums disagree with the displayed electronic or Gibbs energy";
        auto energyTable = siRelativeTable(energyRows, config["include_dispersion"].toBool(true),
                                           config["include_complexation"].toBool(true));
        energyTable["source"] = "sheet cached values with restored shared formulas in row cells";
        energyTable["si_component_check"] = checkedTerms != structureCount * 6 ? "incomplete"
                                            : energyMatch      ? "matched"
                                                               : "mismatch";
        energyTable["component_arithmetic_check"] = !complete         ? "incomplete"
                                                    : arithmeticMatch ? "matched"
                                                                      : "mismatch";
        group["energy_table"] = energyTable;
        const bool bbComplete =
            std::all_of(r.begin(), r.end(), [](const auto &item) { return item.singlePoint.has_value(); });
        bool stationary = true;
        for (int i = 0; i < structureCount; ++i) {
            const auto &f = r[i].frequency;
            if (!f.normalTermination || !f.frequenciesPresent ||
                f.imaginaryFrequencies.size() != size_t(!spinOptimization && i == 1 ? 1 : 0))
                stationary = false;
        }
        if (!stationary)
            issues << (spinOptimization ? "Optimized structures require zero imaginary modes" :
                      "Stationary point validation failed: endpoints require zero imaginary modes and TS requires one");
        if (!spinOptimization && complete && energyMatch && arithmeticMatch && checkedTerms == 18 && bbComplete && stationary &&
            narrativeValues.size() == 3) {
            const auto barrier = narrativeValues[1] - narrativeValues[0],
                       delta = narrativeValues[2] - narrativeValues[0];
            const auto basis = narrativeColumn == "N" ? "Def2-TZVPP electronic energy" : "Gibbs free energy";
            auto sentence = phrases["energy"]
                                .toString()
                                .replace("{basis}", basis)
                                .replace("{barrier}", QString::number(barrier, 'f', 1))
                                .replace("{product}", QString::number(delta, 'f', 1));
            paragraphs << QJsonObject{{"template_key", "energy"},
                                      {"text", sentence},
                                      {"highlight", false},
                                      {"template_status", "provisional"},
                                      {"sheet_energy_column", narrativeColumn}};
        }
        if (spinOptimization) {
            QStringList entries;
            for (int i = 0; i < narrativeValues.size(); ++i)
                entries << structureLabels[i] + ": " + QString::number(narrativeValues[i], 'f', 2);
            if (narrativeValues.size() == structureCount)
                paragraphs << QJsonObject{{"template_key", "energy"}, {"highlight", false},
                    {"text", QString("시트의 %1 값은 %2 kcal/mol입니다.")
                        .arg(narrativeColumn == "N" ? "E Total" : "ΔG", entries.join(", "))},
                    {"sheet_energy_column", narrativeColumn}};
            for (int id : ids)
                for (auto issue : sheetRows[id]["analysis"].toObject()["issues"].toArray())
                    issues << sheetRows[id]["filename"].toString() + ": " + issue.toString();
        }
        // SI top-six maxima across structures, with metal sites forced to the front.
        std::map<int, double> maxima;
        QSet<int> metals;
        for (const auto &item : r)
            for (const auto &s : item.frequency.spinDensities) {
                if (s.atomIndex < 1 || s.atomIndex > int(first.size())) continue;
                maxima[s.atomIndex] = std::max(maxima[s.atomIndex], std::abs(s.spin));
                if (s.atomIndex > 0 && s.atomIndex <= int(first.size()) &&
                    metal(first[s.atomIndex - 1].atomicNumber))
                    metals.insert(s.atomIndex);
            }
        std::vector<int> selected;
        for (const auto &[id, v] : maxima)
            selected.push_back(id);
        std::sort(selected.begin(), selected.end(), [&](int a, int b) {
            if (metals.contains(a) != metals.contains(b))
                return metals.contains(a);
            return maxima[a] != maxima[b] ? maxima[a] > maxima[b] : a < b;
        });
        selected.resize(std::min(selected.size(), std::max<size_t>(6, metals.size())));
        QJsonArray spinRows;
        if (aligned)
            for (int atom : selected) {
                const auto label = QString::fromStdString(first[atom - 1].symbol) + QString::number(atom);
                QJsonArray values{label};
                for (const auto &item : r) {
                    std::optional<double> value;
                    for (const auto &s : item.frequency.spinDensities)
                        if (s.atomIndex == atom)
                            value = s.spin;
                    values << number(value);

                }
                spinRows << values;

            }
        if (aligned)
            for (const auto &label : {"Rest", "Sum"}) {
                QJsonArray values{QString(label)};
                for (const auto &item : r) {
                    if (item.frequency.spinDensities.size() != first.size()) {
                        values << QJsonValue(QJsonValue::Null);
                        continue;
                    }
                    double sum = 0;
                    for (const auto &s : item.frequency.spinDensities)
                        if (QString(label) == "Sum" ||
                            std::find(selected.begin(), selected.end(), s.atomIndex) == selected.end())
                            sum += s.spin;
                    values << sum;
                }
                spinRows << values;
            }
        QJsonArray spinHeaders{"Atom"};
        for (const auto &label : structureLabels) spinHeaders << label;
        group["spin_table"] = QJsonObject{
            {"headers", spinHeaders},
            {"rows", spinRows},
            {"basis", "TZVPP"},
            {"metal_first", !metals.isEmpty()}};
        if (metals.isEmpty())
            issues << "Metal spin data unavailable";
        QJsonArray geometryRows;
        QJsonArray geometryIds;
        const double threshold = config["distance_change_threshold_angstrom"].toDouble(0.15);
        const auto explicitGroups = config["reaction_rules"]
                                        .toObject()[group["section"].toString()]
                                        .toObject()["geometry_atom_groups"]
                                        .toArray();
        if (aligned)
            for (const auto &value : explicitGroups) {
                auto atoms = value.toArray();
                if (atoms.size() < 2 || atoms.size() > 4)
                    throw std::runtime_error("Geometry group needs 2, 3 or 4 atom IDs");
                QStringList labels;
                for (const auto &id : atoms) {
                    if (id.toInt() < 1 || id.toInt() > int(first.size()))
                        throw std::runtime_error("Geometry atom ID out of range");
                    labels << QString::fromStdString(first[id.toInt() - 1].symbol) +
                                  QString::number(id.toInt());
                }
                const auto unit = atoms.size() == 2 ? "Å" : "°";
                const auto label = labels.join("−");
                QJsonArray values{label + QString(" (%1)").arg(unit)};
                QVector<double> numeric;
                for (const auto &item : r) {
                    const auto &xyz = item.frequency.finalGeometry;
                    auto metric = atoms.size() == 2 ? si::distance(xyz, atoms[0].toInt(), atoms[1].toInt())
                                  : atoms.size() == 3
                                      ? si::angle(xyz, atoms[0].toInt(), atoms[1].toInt(), atoms[2].toInt())
                                      : si::dihedral(xyz, atoms[0].toInt(), atoms[1].toInt(),
                                                     atoms[2].toInt(), atoms[3].toInt());
                    values << number(metric);
                    if (metric)
                        numeric << *metric;
                }
                geometryRows << values;
                geometryIds << atoms;
            }
        // Suggest changed short contacts; numerical geometry is evaluated by the SI functions.
        if (aligned && explicitGroups.isEmpty())
            for (int a = 1; a <= int(first.size()); ++a)
                for (int b = a + 1; b <= int(first.size()); ++b) {
                    QVector<double> ds;
                    for (const auto &item : r)
                        ds << *si::distance(item.frequency.finalGeometry, a, b);
                    auto [lo, hi] = std::minmax_element(ds.begin(), ds.end());
                    if (*lo > contactLimit(first[a - 1].atomicNumber, first[b - 1].atomicNumber) ||
                        *hi - *lo < threshold)
                        continue;
                    const auto label = QString::fromStdString(first[a - 1].symbol) + QString::number(a) +
                                       "−" + QString::fromStdString(first[b - 1].symbol) + QString::number(b);
                    QJsonArray values{label};
                    for (auto d : ds)
                        values << d;
                    geometryRows << values;
                    geometryIds << QJsonArray{a, b};

                }
        QJsonArray geometryHeaders{explicitGroups.isEmpty() ? "Distance (Å)" : "Geometry (Å / °)"};
        for (const auto &label : structureLabels) geometryHeaders << label;
        group["geometry_table"] =
            QJsonObject{{"headers", geometryHeaders},
                        {"rows", geometryRows},
                        {"atom_ids", geometryIds},
                        {"selection", explicitGroups.isEmpty()
                                          ? "automatic suggestions; atom correspondence requires review"
                                          : "configured SI atom groups"}};
        group["paragraphs"] = paragraphs;
        rebuildNarratives(group, config);
        group["issues"] = issues;
        group["status"] = "draft_requires_review";
        analyses << group;
    }
    plan["analyses"] = analyses;
    metrics["analysis_ms"] = double(analysisTimer.elapsed());
    plan["metrics"] = metrics;
    QTextStream(stdout) << "Analysis metrics: " << QJsonDocument(metrics).toJson(QJsonDocument::Compact) << Qt::endl;
    return plan;
}
} // namespace youngkly
