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
QJsonObject groupRule(const QJsonObject &config, const QJsonObject &group) {
    const auto rules = config["reaction_rules"].toObject();
    const auto key = group["rule_key"].toString();
    return !key.isEmpty() && rules.contains(key) ? rules[key].toObject() :
        rules[group["section"].toString()].toObject();
}
QString routeSetting(const std::string &route, const QString &key) {
    const QRegularExpression token("\\b" + QRegularExpression::escape(key) +
        "\\s*(?:=\\s*)?(\\([^)]*\\)|[^\\s]+)", QRegularExpression::CaseInsensitiveOption);
    const auto match = token.match(QString::fromStdString(route));
    // Gaussian wraps route cards; whitespace around '=' is not a different method.
    auto value = match.hasMatch() ? match.captured(1).toLower() : QString();
    value.remove(QRegularExpression("\\s+"));
    return value;
}
bool sameElectronicState(const si::GaussianResult &primary, const si::GaussianResult &singlePoint) {
    if (primary.charge != singlePoint.charge || primary.multiplicity != singlePoint.multiplicity)
        return false;
    const auto methodA = QString::fromStdString(primary.method).toLower();
    const auto methodB = QString::fromStdString(singlePoint.method).toLower();
    if (methodA != methodB)
        return false;
    for (const auto &key : {QStringLiteral("scrf"), QStringLiteral("empiricaldispersion")})
        if (routeSetting(primary.route, key) != routeSetting(singlePoint.route, key))
            return false;
    return true;
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
            if (high.success && high.normalTermination && high.basisLevel == "TZVPP" && aligned &&
                sameElectronicState(record.frequency, high)) {
                record.frequency.spinDensities = high.spinDensities;
                record.frequency.spinSourcePath = high.path;
                record.frequency.spinBasisLevel = high.basisLevel;
                record.frequency.spinFallback = false;
            } else {
                issues << "BB basis, completion, atom correspondence or electronic state mismatch";
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
                        {"charge", record.frequency.charge ? QJsonValue(*record.frequency.charge) : QJsonValue(QJsonValue::Null)},
                        {"multiplicity", record.frequency.multiplicity ? QJsonValue(*record.frequency.multiplicity) : QJsonValue(QJsonValue::Null)},
                        {"method", QString::fromStdString(record.frequency.method)},
                        {"route", QString::fromStdString(record.frequency.route)},
                        {"spin_source", QString::fromStdWString(record.frequency.spinSourcePath.wstring())},
                        {"xyz", xyz},
                        {"issues", issues}};
        records[id] = std::move(record);
        rows[i] = row;
        sheetRows[id] = row;
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
            const auto rule = groupRule(config, group);
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
        QJsonArray energyRows, componentChecks;
        const auto rel = si::legacyRelativeEnergies(r, r.front(), true, true);
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
            QJsonObject rowChecks;
            bool rowComplete = true;
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
                    rowComplete = false;
                QString checkState = ok ? "not_applicable" : "missing";
                const bool hasPrimaryAndSpe = r[i].singlePoint && r.front().singlePoint;
                const bool canCompare = c < 6 ? hasPrimaryAndSpe :
                    c == 6 && r[i].frequency.dispersion && r.front().frequency.dispersion;
                if (c < 7 && ok && canCompare &&
                    rel[i].kcalTerms[termIndex[c]] && rel[0].kcalTerms[termIndex[c]]) {
                    bool baseOk;
                    const double base = sheetRows[ids[0]]["cells"].toObject()[cols[c]]
                                            .toObject()["value"].toString().toDouble(&baseOk);
                    if (baseOk) {
                        if (c < 6) ++checkedTerms;
                        const bool matched = std::abs((v - base) - *rel[i].kcalTerms[termIndex[c]]) <=
                            config["energy_tolerance_kcal"].toDouble(0.02);
                        checkState = matched ? "matched" : "mismatch";
                        if (!matched) energyMatch = false;
                    }
                }
                rowChecks[cols[c]] = checkState;
            }
            bool ok;
            double gv = cells[narrativeColumn].toObject()["value"].toString().toDouble(&ok);
            if (ok)
                narrativeValues << gv;
            complete = complete && rowComplete;
            if (rowComplete) {
                double sum = 0;
                for (int c = 3; c <= 7; ++c)
                    sum += values[c].toDouble();
                const bool rowArithmetic = std::abs(sum - values[8].toDouble()) <=
                    config["energy_tolerance_kcal"].toDouble(0.02) &&
                    std::abs(values[1].toDouble() + values[2].toDouble() - values[3].toDouble()) <= 0.02;
                rowChecks["S"] = rowArithmetic ? "matched" : "mismatch";
                if (!rowArithmetic) arithmeticMatch = false;
            }
            componentChecks << QJsonObject{{"sheet_row", ids[i]}, {"states", rowChecks}};
            energyRows << values;
        }
        if (!energyMatch)
            issues << "Sheet profile and SI component differences disagree beyond tolerance";
        if (!arithmeticMatch)
            issues << "Sheet component sums disagree with the displayed electronic or Gibbs energy";
        auto energyTable = siRelativeTable(energyRows, config["include_dispersion"].toBool(false),
                                           config["include_complexation"].toBool(false));
        energyTable["source"] = "sheet cached values with restored shared formulas in row cells";
        energyTable["component_checks"] = componentChecks;
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
            const auto rule = groupRule(config, group);
            const int reactantRow = rule["reactant_row_id"].toInt(group["reactant_row_id"].toInt());
            const int productRow = rule["product_row_id"].toInt(group["product_row_id"].toInt());
            if (reactantRow == 0 || productRow == 0 || reactantRow == productRow ||
                !ids.contains(reactantRow) || !ids.contains(productRow) ||
                ids.indexOf(reactantRow) == 1 || ids.indexOf(productRow) == 1) {
                issues << "Reaction direction not confirmed: forward barrier narrative withheld";
            } else {
            group["reactant_row_id"] = reactantRow;
            group["product_row_id"] = productRow;
            group["reaction_direction_confirmed"] = true;
            const auto barrier = narrativeValues[1] - narrativeValues[ids.indexOf(reactantRow)],
                       delta = narrativeValues[ids.indexOf(productRow)] - narrativeValues[ids.indexOf(reactantRow)];
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
        // SI Generator stage selection: common atom IDs ranked by maximum absolute spin.
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
        for (const auto &[id, v] : maxima) {
            bool common = true;
            std::string symbol;
            for (const auto &item : r) {
                const auto &spins = item.frequency.spinDensities;
                const auto found = std::find_if(spins.begin(), spins.end(),
                    [&](const auto &spin) { return spin.atomIndex == id; });
                if (found == spins.end() || (!symbol.empty() && symbol != found->atom)) {
                    common = false; break;
                }
                symbol = found->atom;
            }
            if (common) selected.push_back(id);
        }
        std::sort(selected.begin(), selected.end(), [&](int a, int b) {
            return maxima[a] != maxima[b] ? maxima[a] > maxima[b] : a < b;
        });
        selected.resize(std::min<size_t>(selected.size(), 6));
        QJsonArray spinRows, spinIds;
        if (aligned)
            for (int atom : selected) {
                const auto label = QString::fromStdString(first[atom - 1].symbol);
                QJsonArray values{label};
                for (const auto &item : r) {
                    std::optional<double> value;
                    for (const auto &s : item.frequency.spinDensities)
                        if (s.atomIndex == atom)
                            value = s.spin;
                    values << number(value);

                }
                spinRows << values;
                spinIds << atom;
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
            {"atom_ids", spinIds},
            {"selection", "SI Generator common atoms; top six maximum absolute spin"},
            {"basis", "TZVPP"},
            {"metal_first", !selected.empty() && metals.contains(selected.front())}};
        if (metals.isEmpty())
            issues << "Metal spin data unavailable";
        QJsonArray geometryRows;
        QJsonArray geometryIds;
        const auto explicitGroups = groupRule(config, group)["geometry_atom_groups"].toArray();
        if (aligned)
            for (const auto &value : explicitGroups) {
                auto atoms = value.toArray();
                if (atoms.size() < 2 || atoms.size() > 4)
                    throw std::runtime_error("Geometry group needs 2, 3 or 4 atom IDs");
                QStringList labels;
                for (const auto &id : atoms) {
                    if (id.toInt() < 1 || id.toInt() > int(first.size()))
                        throw std::runtime_error("Geometry atom ID out of range");
                    labels << QString::fromStdString(first[id.toInt() - 1].symbol);
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
        // Reuse SI Generator's bond-change and displacement ranking, capped at six.
        if (aligned && explicitGroups.isEmpty())
            for (const auto &pair : si::suggestGeometry(r, 6).distances) {
                const int a = pair[0], b = pair[1];
                QJsonArray values{QString::fromStdString(first[a - 1].symbol) + "−" +
                                  QString::fromStdString(first[b - 1].symbol)};
                for (const auto &item : r) values << number(si::distance(item.frequency.finalGeometry, a, b));
                geometryRows << values; geometryIds << QJsonArray{a, b};
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
