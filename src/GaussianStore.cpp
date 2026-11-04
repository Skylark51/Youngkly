#include "GaussianStore.h"
#include "Archive.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>

namespace youngkly {
namespace {
QString digest(const QByteArray &b) {
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex());
}
// This schema contains the raw fields used by Youngkly/SI energy and geometry analysis.
// Narrative/layout settings deliberately do not participate in this cache.
QJsonObject encode(const si::GaussianResult &r) {
    QJsonObject j{{"schema", 4}, {"success", r.success}, {"normal", r.normalTermination},
                  {"frequencies_present", r.frequenciesPresent},
                  {"basis_level", QString::fromStdString(r.basisLevel)},
                  {"spin_basis", QString::fromStdString(r.spinBasisLevel)},
                  {"spin_path", QString::fromStdWString(r.spinSourcePath.wstring())},
                  {"spin_fallback", r.spinFallback}, {"reason", QString::fromStdString(r.reason)}};
    auto put = [&](const char *k, std::optional<double> v) {
        j[k] = v ? QJsonValue(*v) : QJsonValue(QJsonValue::Null);
    };
    put("scf", r.scfEnergy); put("zpe", r.zeroPointCorrection);
    put("thermal", r.thermalCorrection); put("thermal_gibbs", r.thermalGibbsCorrection);
    put("enthalpy", r.enthalpy); put("gibbs", r.gibbsFreeEnergy);
    put("entropy", r.entropy); put("dispersion", r.dispersion);
    put("zpe_kcal", r.zeroPointKcal); put("thermal_kcal", r.thermalTotalKcal);
    QJsonArray xyz, spins, imaginary, warnings;
    for (const auto &a : r.finalGeometry)
        xyz << QJsonArray{a.atomicNumber, QString::fromStdString(a.symbol), a.xyz[0], a.xyz[1], a.xyz[2]};
    for (const auto &s : r.spinDensities)
        spins << QJsonArray{s.atomIndex, QString::fromStdString(s.atom), s.charge, s.spin};
    for (double v : r.imaginaryFrequencies) imaginary << v;
    for (const auto &w : r.warnings) warnings << QString::fromStdString(w);
    j["xyz"] = xyz; j["spins"] = spins; j["imaginary"] = imaginary; j["warnings"] = warnings;
    j["charge"] = r.charge ? QJsonValue(*r.charge) : QJsonValue(QJsonValue::Null);
    j["multiplicity"] = r.multiplicity ? QJsonValue(*r.multiplicity) : QJsonValue(QJsonValue::Null);
    j["method"] = QString::fromStdString(r.method);
    j["basis"] = QString::fromStdString(r.basis);
    j["route"] = QString::fromStdString(r.route);
    j["job_type"] = QString::fromStdString(r.jobType);
    put("s_squared_before", r.sSquaredBefore);
    put("s_squared_after", r.sSquaredAfter);
    return j;
}
si::GaussianResult decode(const QJsonObject &j, const QString &path) {
    si::GaussianResult r;
    r.path = std::filesystem::path(path.toStdWString());
    r.success = j["success"].toBool(); r.normalTermination = j["normal"].toBool();
    r.frequenciesPresent = j["frequencies_present"].toBool();
    r.basisLevel = j["basis_level"].toString().toStdString();
    r.spinBasisLevel = j["spin_basis"].toString().toStdString();
    r.spinSourcePath = std::filesystem::path(j["spin_path"].toString().toStdWString());
    r.spinFallback = j["spin_fallback"].toBool(); r.reason = j["reason"].toString().toStdString();
    if (j["charge"].isDouble()) r.charge = j["charge"].toInt();
    if (j["multiplicity"].isDouble()) r.multiplicity = j["multiplicity"].toInt();
    r.method = j["method"].toString().toStdString();
    r.basis = j["basis"].toString().toStdString();
    r.route = j["route"].toString().toStdString();
    r.jobType = j["job_type"].toString("unknown").toStdString();
    auto get = [&](const char *k) -> std::optional<double> {
        return j[k].isDouble() ? std::optional<double>(j[k].toDouble()) : std::nullopt;
    };
    r.scfEnergy = get("scf"); r.zeroPointCorrection = get("zpe");
    r.thermalCorrection = get("thermal"); r.thermalGibbsCorrection = get("thermal_gibbs");
    r.enthalpy = get("enthalpy"); r.gibbsFreeEnergy = get("gibbs");
    r.entropy = get("entropy"); r.dispersion = get("dispersion");
    r.sSquaredBefore = get("s_squared_before"); r.sSquaredAfter = get("s_squared_after");
    r.zeroPointKcal = get("zpe_kcal"); r.thermalTotalKcal = get("thermal_kcal");
    for (auto v : j["xyz"].toArray()) { auto a = v.toArray();
        r.finalGeometry.push_back({a[0].toInt(), a[1].toString().toStdString(),
                                   {a[2].toDouble(), a[3].toDouble(), a[4].toDouble()}}); }
    for (auto v : j["spins"].toArray()) { auto a = v.toArray();
        r.spinDensities.push_back({a[0].toInt(), a[1].toString().toStdString(), a[2].toDouble(), a[3].toDouble()}); }
    for (auto v : j["imaginary"].toArray()) r.imaginaryFrequencies.push_back(v.toDouble());
    for (auto v : j["warnings"].toArray()) r.warnings.push_back(v.toString().toStdString());
    return r;
}
}
si::GaussianResult cachedGaussian(const QString &path, const QString &cache, bool force, QJsonObject &metrics) {
    const auto native = std::filesystem::path(path.toStdWString());
    const auto paired = si::pairedSinglePointPath(native);
    // The upstream parser may read a paired file itself, so its content is part of the key too.
    const auto key = digest((QString("SI-raw-4:") + YOUNGKLY_PARSER_VERSION + ":" + digest(readFile(path)) + ":" +
        (paired.empty() ? QString("none") : digest(readFile(QString::fromStdWString(paired.wstring())))) +
        ":" + QFileInfo(path).absoluteFilePath()).toUtf8());
    const auto directory = QDir(cache).filePath("parsed");
    QDir().mkpath(directory);
    const auto file = QDir(directory).filePath(key + ".json");
    if (!force && QFileInfo::exists(file)) {
        try {
            auto j = QJsonDocument::fromJson(readFile(file)).object();
            if (j["schema"].toInt() == 4 && j["key"].toString() == key) {
                metrics["parse_cache_hits"] = metrics["parse_cache_hits"].toInt() + 1;
                return decode(j, path);
            }
        } catch (...) { /* A corrupt cache is rebuilt from the authoritative file. */ }
    }
    auto r = si::GaussianParser().parse(native);
    metrics["gaussian_parses"] = metrics["gaussian_parses"].toInt() + 1;
    auto j = encode(r); j["key"] = key;
    writeFile(file, QJsonDocument(j).toJson(QJsonDocument::Compact));
    return r;
}
}
