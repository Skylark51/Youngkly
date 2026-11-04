#include "Narrative.h"
#include "Pipeline.h"
#include <QJsonArray>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace youngkly {
QString reportNumber(double value, int digits) {
    if (std::abs(value) < 0.5 * std::pow(10.0, -digits)) value = 0;
    return QString::number(value, 'f', digits);
}
QJsonObject describeChange(const QJsonArray &row, const QJsonArray &headers, const QString &kind,
                           int atomCount, double threshold, const QJsonObject &templates) {
    if (row.size() != 4 || headers.size() != 4) return {};
    double raw[3], v[3]; QString shown[3];
    for (int i = 0; i < 3; ++i) {
        if (!row[i + 1].isDouble() || !std::isfinite(row[i + 1].toDouble())) return {};
        raw[i] = v[i] = row[i + 1].toDouble(); shown[i] = reportNumber(raw[i]);
    }
    if (kind == "geometry" && atomCount == 4)
        for (int i = 1; i < 3; ++i) v[i] = v[i - 1] + std::remainder(raw[i] - v[i - 1], 360.0);
    const double low = *std::min_element(v, v + 3), high = *std::max_element(v, v + 3);
    const double eps = 0.001;
    const bool same = shown[0] == shown[1] && shown[1] == shown[2];
    QString type;
    if (same) type = "same";
    else if (high - low < threshold) type = "small";
    else if (v[1] > std::max(v[0], v[2]) + eps) type = "ts_peak";
    else if (v[1] < std::min(v[0], v[2]) - eps) type = "ts_valley";
    else if (v[2] > v[0] && v[1] >= v[0] - eps && v[1] <= v[2] + eps) type = "increase";
    else if (v[2] < v[0] && v[1] <= v[0] + eps && v[1] >= v[2] - eps) type = "decrease";
    else type = "sequence";
    const bool signChange = kind == "spin" && ((low < -0.001 && high > 0.001));
    if (signChange) type = "sign_change";
    const auto key = kind + "_" + type;
    auto sentence = templates[key].toString();
    // Old user configs remain intact; use new built-in defaults for missing keys.
    if (sentence.isEmpty()) {
        const QString subject = kind == "spin" ? "{label}의 spin density" : "{label}";
        const QString value = "{v0}{unit}, {v1}{unit}, {v2}{unit}";
        if (type == "same") sentence = subject + "는 세 구조에서 모두 {v0}{unit}로 나타났습니다.";
        else if (type == "small") sentence = subject + "는 {min}–{max}{unit} 범위로, 세 구조 사이의 변화는 크지 않았습니다.";
        else if (type == "ts_peak") sentence = subject + "는 {s1}에서 {v1}{unit}까지 증가하였으나, {s2}에서는 {v2}{unit}로 다시 감소하였습니다.";
        else if (type == "ts_valley") sentence = subject + "는 {s1}에서 {v1}{unit}까지 감소하였으나, {s2}에서는 {v2}{unit}로 다시 증가하였습니다.";
        else if (type == "increase" || type == "decrease") sentence = subject + "는 {s0}, {s1}, {s2}에서 각각 " + value + (type == "increase" ? "로 증가하였습니다." : "로 감소하였습니다.");
        else if (type == "sign_change") sentence = subject + "는 {s0}, {s1}, {s2}에서 각각 " + value + "로 나타났으며, 경로 중 부호가 바뀌었습니다.";
        else sentence = subject + "는 {s0}, {s1}, {s2}에서 각각 " + value + "로 나타났습니다.";
    }
    auto label = row[0].toString();
    label.remove(QRegularExpression("\\s*\\((Å|°)\\)$"));
    if (kind == "geometry") label += atomCount == 2 ? " 거리" : atomCount == 3 ? " 각도" : " 이면각";
    sentence.replace("{label}", label).replace("{unit}", kind == "spin" ? "" : atomCount == 2 ? " Å" : "°")
        .replace("{min}", reportNumber(*std::min_element(raw, raw + 3)))
        .replace("{max}", reportNumber(*std::max_element(raw, raw + 3)));
    for (int i = 0; i < 3; ++i)
        sentence.replace(QString("{v%1}").arg(i), shown[i]).replace(QString("{s%1}").arg(i), headers[i + 1].toString());
    for (int i = 0; i < 3; ++i) {
        const auto particle = kind == "spin" && QString("036").contains(shown[i].back()) ? "으로" : "로";
        sentence.replace(QString("{p%1}").arg(i), particle);
        if (kind == "spin") sentence.replace(shown[i] + "로", shown[i] + particle);
    }
    // For periodic angles, wrapped min/max would misrepresent a small crossing at 180 degrees.
    if (kind == "geometry" && atomCount == 4 && type == "small") {
        sentence = QString("%1은 %2, %3, %4에서 각각 %5°, %6°, %7°로 나타났으며, 주기성을 고려한 변화 폭은 %8°입니다.")
            .arg(label, headers[1].toString(), headers[2].toString(), headers[3].toString(), shown[0], shown[1], shown[2], reportNumber(high - low));
    }
    if (sentence.contains(QRegularExpression("\\{[^{}]+\\}|~~"))) return {};
    QJsonArray sequence, classification;
    for (int i = 0; i < 3; ++i) { sequence << raw[i]; classification << v[i]; }
    return {{"template_key", kind == "spin" ? "spin_change" : "geometry_change"},
             {"rule", key}, {"text", sentence}, {"raw_values", sequence}, {"classification_values", classification},
             {"sign_change", signChange}, {"table", kind + "_table"}, {"label", row[0]},
             {"highlight", high - low >= threshold}, {"template_status", "new_draft_in_user_style"}};
}
void rebuildNarratives(QJsonObject &group, const QJsonObject &config) {
    QJsonArray paragraphs;
    for (auto v : group["paragraphs"].toArray())
        if (v.toObject()["template_key"] == "energy") paragraphs << v;
    const auto rule = config["reaction_rules"].toObject()[group["section"].toString()].toObject();
    const bool verified = group["atom_mapping_valid"].toBool(true) && (group["atom_mapping_confirmed"].toBool() || rule["atom_mapping_confirmed"].toBool());
    group["narrative_review"] = verified ? "atom_mapping_confirmed" : "atom_mapping_confirmation_required";
    const auto phrases = loadConfig(config["phrases_file"].toString());
    if (verified) for (const QString &kind : {QString("spin"), QString("geometry")}) {
        auto table = group[kind + "_table"].toObject();
        const auto data = table["rows"].toArray(), atomIds = table["atom_ids"].toArray();
        QJsonArray observations;
        for (int i = 0; i < data.size(); ++i) {
            auto row = data[i].toArray();
            if (row.isEmpty() || row[0] == "Rest" || row[0] == "Sum") continue;
            int n = kind == "spin" ? 1 : i < atomIds.size() ? atomIds[i].toArray().size() : 2;
            auto p = describeChange(row, table["headers"].toArray(), kind, n,
                kind == "spin" ? config["spin_change_threshold"].toDouble(0.2) : n == 2 ?
                config["distance_change_threshold_angstrom"].toDouble(0.15) : config["angle_change_threshold_degrees"].toDouble(5), phrases);
            if (group["kind"] == "spin_optimization") {
                // Multiplicities are alternatives, not successive reaction steps.
                p = {};
                const auto headers = table["headers"].toArray();
                QStringList states, values;
                QJsonArray raw;
                double low = INFINITY, high = -INFINITY;
                for (int c = 1; c < row.size(); ++c) {
                    if (!row[c].isDouble() || !std::isfinite(row[c].toDouble())) break;
                    states << headers[c].toString(); values << reportNumber(row[c].toDouble());
                    raw << row[c]; low = std::min(low, row[c].toDouble()); high = std::max(high, row[c].toDouble());
                }
                if (raw.size() == row.size() - 1 && !raw.isEmpty()) {
                    const auto unit = kind == "spin" ? "" : n == 2 ? " Å" : "°";
                    p = {{"template_key", kind == "spin" ? "spin_change" : "geometry_change"},
                        {"rule", kind + "_states"}, {"label", row[0]}, {"raw_values", raw},
                        {"highlight", high - low >= (kind == "spin" ? config["spin_change_threshold"].toDouble(0.2) :
                            n == 2 ? config["distance_change_threshold_angstrom"].toDouble(0.15) : config["angle_change_threshold_degrees"].toDouble(5))},
                        {"text", row[0].toString() + (kind == "spin" ? "의 spin density는 " : " 값은 ") +
                            states.join(", ") + "에서 각각 " + values.join(", ") + unit + "로 나타났습니다."}};
                }
            }
            if (p.isEmpty()) continue;
            // Always describe metal sites; other sites need a review-level change.
            const bool isMetal = QRegularExpression("^(Co|Mn|Fe|Ni|Cu|Cr|V|Ti|Mo|Ru|Ir|Rh|Pd|Pt|Zn)[0-9]").match(row[0].toString()).hasMatch();
            if (!p["highlight"].toBool() && !(kind == "spin" && isMetal)) continue;
            p["table_row"] = i; p["metal"] = isMetal;
            p["geometry_order"] = n;
            if (kind == "geometry") p["atom_ids"] = atomIds[i];
            observations << p;
        }
        // Adjacent observations with the same monotonic trend share a sentence;
        // retain every original observation for provenance and avoid random rephrasing.
        for (int i = 0; i < observations.size(); ++i) {
            auto first = observations[i].toObject();
            const auto ruleKey = first["rule"].toString();
            const bool monotonic = ruleKey.endsWith("_increase") || ruleKey.endsWith("_decrease");
            const bool tsLocal = kind == "geometry" && (ruleKey.endsWith("_ts_peak") || ruleKey.endsWith("_ts_valley"));
            QJsonArray parts{first};
            while ((monotonic || tsLocal) && !(kind == "spin" && first["metal"].toBool()) && parts.size() < (kind == "spin" ? 3 : 4) && i + 1 < observations.size()) {
                auto next = observations[i + 1].toObject();
                if (next["rule"] != first["rule"] || (kind == "spin" && next["metal"].toBool()) || next["geometry_order"] != first["geometry_order"]) break;
                parts << next; ++i;
            }
            if (parts.size() == 1) { paragraphs << first; continue; }
            QStringList labels, values;
            const auto n = first["geometry_order"].toInt();
            const auto unit = kind == "spin" ? "" : n == 2 ? " Å" : "°";
            for (auto part : parts) {
                auto p = part.toObject(); auto label = p["label"].toString(); label.remove(QRegularExpression("\\s*\\((Å|°)\\)$"));
                labels << label; QStringList sequence;
                for (auto v : p["raw_values"].toArray()) sequence << reportNumber(v.toDouble());
                values << "(" + sequence.join(" → ") + ")" + unit;
            }
            auto sentence = phrases[kind + "_combined"].toString(kind == "spin" ?
                "{labels}의 spin density는 {states} 순서로 각각 {values}로 {direction}." :
                "{labels} {metric}는 {states} 순서로 각각 {values}로 {direction}.");
            auto headers = table["headers"].toArray();
            auto naturalList = [](QStringList list) {
                if (list.size() < 2) return list.join("");
                const auto last = list.takeLast();
                return list.join(", ") + " 및 " + last;
            };
            if (tsLocal) {
                QStringList middle, endpoint;
                for (auto part : parts) {
                    auto values = part.toObject()["raw_values"].toArray();
                    middle << reportNumber(values[1].toDouble()) + unit;
                    endpoint << reportNumber(values[2].toDouble()) + unit;
                }
                const bool peak = ruleKey.endsWith("_ts_peak");
                sentence = phrases[peak ? "geometry_combined_ts_peak" : "geometry_combined_ts_valley"].toString(
                    peak ? "{labels} {metric}는 {s1}에서 각각 {middle}까지 증가하였으나, {s2}에서는 {endpoint}로 다시 감소하였습니다." :
                           "{labels} {metric}는 {s1}에서 각각 {middle}까지 감소하였으나, {s2}에서는 {endpoint}로 다시 증가하였습니다.");
                sentence.replace("{s1}", headers[2].toString()).replace("{s2}", headers[3].toString())
                    .replace("{middle}", middle.join(", ")).replace("{endpoint}", endpoint.join(", "));
            }
            sentence.replace("{labels}", naturalList(labels)).replace("{values}", naturalList(values))
                .replace("{states}", headers[1].toString() + " → " + headers[2].toString() + " → " + headers[3].toString())
                .replace("{metric}", n == 2 ? "거리" : n == 3 ? "각도" : "이면각")
                .replace("{direction}", ruleKey.endsWith("_increase") ? "증가하였습니다" : "감소하였습니다");
            if (sentence.contains(QRegularExpression("\\{[^{}]+\\}|~~"))) { for (auto p : parts) paragraphs << p; continue; }
            first["text"] = sentence; first["observations"] = parts;
            first["rule"] = ruleKey + "_combined"; paragraphs << first;
        }
    }
    group["paragraphs"] = paragraphs;
}
}
