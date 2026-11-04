#include "Pipeline.h"
#include "Archive.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <future>
#include <stdexcept>

namespace youngkly {
namespace {
QString expandPath(QString p) { return p.startsWith("~/") ? QDir::homePath() + p.mid(1) : p; }
QString sha(const QByteArray &b) {
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex());
}
QByteArray remote(const QJsonObject &server, const QString &command, int timeout) {
    QStringList args{"-o", "BatchMode=yes", "-o", "ConnectTimeout=10", "-o", "StrictHostKeyChecking=yes"};
    for (auto a : server["ssh_args"].toArray()) args << expandPath(a.toString());
    args << server["destination"].toString() << command;
    QProcess p; p.start("ssh", args);
    if (!p.waitForStarted(5000)) throw std::runtime_error("Cannot start SSH");
    if (!p.waitForFinished(timeout)) { p.kill(); p.waitForFinished(); throw std::runtime_error("SSH timeout"); }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode())
        throw std::runtime_error(QString::fromUtf8(p.readAllStandardError()).left(1000).toStdString());
    return p.readAllStandardOutput();
}
QMap<QString, QJsonObject> metadata(const QJsonObject &server, const QStringList &paths, int &sshCount) {
    QMap<QString, QJsonObject> result;
    if (paths.isEmpty()) return result;
    if (server.contains("local_root")) {
        for (const auto &path : paths) {
            QFileInfo f(path);
            if (!f.exists() || !f.isFile()) throw std::runtime_error("Source disappeared during transfer");
            result[path] = {{"size", double(f.size())}, {"mtime", f.lastModified().toUTC().toString(Qt::ISODateWithMs)}};
        }
    } else {
        QString command = "stat --printf='%n\\0%s\\0%y\\0' --";
        for (const auto &path : paths) command += " " + shellQuote(path);
        ++sshCount;
        auto parts = remote(server, command, 60000).split('\0');
        for (int i = 0; i + 2 < parts.size(); i += 3) {
            bool ok; const auto size = parts[i + 1].toLongLong(&ok);
            if (!ok) throw std::runtime_error("Invalid source metadata");
            result[QString::fromUtf8(parts[i])] = {{"size", double(size)}, {"mtime", QString::fromUtf8(parts[i + 2])}};
        }
        if (result.size() != paths.size()) throw std::runtime_error("Incomplete source metadata");
    }
    return result;
}
}
QJsonObject resolveFiles(QJsonObject plan, const QJsonObject &config, const QString &cache) {
    QDir().mkpath(cache);
    const auto manifestFile = QDir(cache).filePath("manifest-v1.json");
    QJsonObject manifest;
    if (QFileInfo::exists(manifestFile)) {
        try { manifest = QJsonDocument::fromJson(readFile(manifestFile)).object(); } catch (...) {}
    }
    auto rows = plan["rows"].toArray();
    QSet<QString> pending;
    for (auto value : rows) {
        auto row = value.toObject();
        if (row["kind"] != "output") continue;
        pending.insert(row["filename"].toString());
        if (!row["bb_filename"].toString().isEmpty()) pending.insert(row["bb_filename"].toString());
    }
    QMap<QString, QSet<QString>> hints;
    for (const auto &value : rows) {
        const auto row = value.toObject();
        QString text;
        for (const auto &cell : row["cells"].toObject()) text += " " + cell.toObject()["value"].toString();
        QSet<QString> servers;
        for (const auto &v : config["servers"].toArray()) {
            const auto name = v.toObject()["name"].toString();
            if (QRegularExpression("\\b" + QRegularExpression::escape(name) + "\\b", QRegularExpression::CaseInsensitiveOption).match(text).hasMatch() ||
                text.contains(name == "lion" ? "라이온" : name == "dumbo" ? "덤보" : "타키온")) servers.insert(name);
        }
        if (!servers.isEmpty()) for (const auto &key : {"filename", "bb_filename"}) hints[row[key].toString()].unite(servers);
    }
    const int total = pending.size();
    int sshCount = 0, hits = 0, downloads = 0, completed = 0;
    qint64 bytes = 0, searchMs = 0, metadataMs = 0, downloadMs = 0;
    const int concurrency = qBound(1, config["download_concurrency"].toInt(3), 8);
    const bool force = config["force_refresh"].toBool();
    QMap<QString, QJsonObject> files;
    QJsonArray audit;
    QSet<QString> blocked;
    for (auto value : config["servers"].toArray()) {
        if (pending.isEmpty()) break;
        auto server = value.toObject();
        auto names = pending.values();
        names.erase(std::remove_if(names.begin(), names.end(), [&](const QString &name) {
            return blocked.contains(name) || (hints.contains(name) && !hints[name].contains(server["name"].toString()));
        }), names.end());
        if (names.isEmpty()) continue;
        std::sort(names.begin(), names.end());
        QMap<QString, QStringList> matches;
        QElapsedTimer timer; timer.start();
        QString error;
        try {
            if (server.contains("local_root")) {
                const auto root = expandPath(server["local_root"].toString());
                if (!QDir(root).exists()) throw std::runtime_error("Local root does not exist");
                QStringList stack{root};
                while (!stack.isEmpty()) {
                    QDir dir(stack.takeLast());
                    for (auto f : dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
                        if (f.isDir()) stack << f.absoluteFilePath();
                        else if (names.contains(f.fileName())) matches[f.fileName()] << f.absoluteFilePath();
                    }
                }
            } else {
                if (server["roots"].toArray().isEmpty()) throw std::runtime_error("Server has no roots");
                QString command = "find ";
                for (auto root : server["roots"].toArray()) command += shellQuote(root.toString()) + " ";
                command += "-type f \\( ";
                for (int i = 0; i < names.size(); ++i) {
                    if (i) command += " -o ";
                    command += "-name " + shellQuote(names[i]);
                }
                command += " \\) -printf '%f\\0%p\\0'";
                ++sshCount;
                auto parts = remote(server, command, config["search_timeout_ms"].toInt(60000)).split('\0');
                for (int i = 0; i + 1 < parts.size(); i += 2)
                    if (!parts[i].isEmpty()) matches[QString::fromUtf8(parts[i])] << QString::fromUtf8(parts[i + 1]);
            }
        } catch (const std::exception &e) { error = QString::fromUtf8(e.what()); }
        searchMs += timer.elapsed();
        QJsonArray requested; for (auto n : names) requested << n;
        audit << QJsonObject{{"server", server["name"]}, {"requested", requested},
                             {"status", error.isEmpty() ? "searched" : "search_error"}, {"error", error}};
        if (!error.isEmpty()) { for (const auto &name : names) blocked.insert(name); continue; }
        QStringList uniquePaths, uniqueNames;
        for (const auto &name : names) {
            auto paths = matches.value(name); paths.removeDuplicates(); std::sort(paths.begin(), paths.end());
            if (paths.isEmpty()) continue;
            pending.remove(name);
            QJsonArray candidates; for (auto p : paths) candidates << p;
            files[name] = {{"server", server["name"]}, {"candidates", candidates},
                            {"status", paths.size() == 1 ? "found" : "ambiguous"}};
            if (paths.size() == 1) { uniqueNames << name; uniquePaths << paths[0]; }
        }
        QMap<QString, QJsonObject> before;
        timer.restart();
        try { before = metadata(server, uniquePaths, sshCount); }
        catch (const std::exception &e) {
            for (auto name : uniqueNames) { files[name]["status"] = "download_error"; files[name]["error"] = e.what(); }
            metadataMs += timer.elapsed(); continue;
        }
        metadataMs += timer.elapsed();
        QStringList toGet;
        for (int i = 0; i < uniqueNames.size(); ++i) {
            auto &source = files[uniqueNames[i]];
            const auto path = uniquePaths[i];
            // Keep basename paired with primary in a directory unique to source identity.
            const auto identity = server["name"].toString() + ":" + server["destination"].toString() + ":" +
                                  (server.contains("local_root") ? expandPath(server["local_root"].toString()) : QString());
            const auto directory = QDir(cache).filePath(sha((identity + ":" + QFileInfo(path).path()).toUtf8()));
            QDir().mkpath(directory);
            const auto local = QDir(directory).filePath(QFileInfo(path).fileName());
            const auto key = sha((identity + ":" + QDir::cleanPath(path)).toUtf8());
            source["path"] = path; source["local_path"] = QFileInfo(local).absoluteFilePath();
            source["cache_key"] = key; source["metadata"] = before[path];
            auto entry = manifest[key].toObject();
            bool hit = !force && entry["metadata"].toObject() == before[path] && QFileInfo::exists(local);
            if (hit) {
                try { hit = QFileInfo(local).size() == qint64(before[path]["size"].toDouble()) &&
                            sha(readFile(local)) == entry["sha256"].toString(); } catch (...) { hit = false; }
            }
            if (hit) { source["sha256"] = entry["sha256"]; source["cache_hit"] = true; ++hits; ++completed; }
            else toGet << uniqueNames[i];
        }
        for (int first = 0; first < toGet.size(); first += concurrency) {
            QStringList batchPaths;
            std::vector<std::future<QByteArray>> futures;
            const int count = qMin(concurrency, int(toGet.size()) - first);
            timer.restart();
            for (int j = 0; j < count; ++j) {
                const auto path = files[toGet[first + j]]["path"].toString(); batchPaths << path;
                if (!server.contains("local_root")) ++sshCount;
                futures.push_back(std::async(std::launch::async, [server, path] {
                    return server.contains("local_root") ? readFile(path) : remote(server, "cat -- " + shellQuote(path), 60000);
                }));
            }
            QVector<QByteArray> received(count); QStringList failures;
            for (int j = 0; j < count; ++j) {
                try { received[j] = futures[j].get(); failures << QString(); ++downloads; bytes += received[j].size(); }
                catch (const std::exception &e) { failures << QString::fromUtf8(e.what()); }
            }
            downloadMs += timer.elapsed(); timer.restart();
            QMap<QString, QJsonObject> after;
            QString metaError;
            try { after = metadata(server, batchPaths, sshCount); }
            catch (const std::exception &e) { metaError = QString::fromUtf8(e.what()); }
            metadataMs += timer.elapsed();
            for (int j = 0; j < count; ++j) {
                auto &source = files[toGet[first + j]]; const auto path = batchPaths[j];
                try {
                    if (!failures[j].isEmpty()) throw std::runtime_error(failures[j].toStdString());
                    if (!metaError.isEmpty()) throw std::runtime_error(metaError.toStdString());
                    if (before[path] != after[path] || qint64(before[path]["size"].toDouble()) != received[j].size())
                        throw std::runtime_error("Source changed during transfer; cache not committed");
                    source["sha256"] = sha(received[j]); source["cache_hit"] = false;
                    writeFile(source["local_path"].toString(), received[j]); // QSaveFile atomic commit.
                    manifest[source["cache_key"].toString()] = QJsonObject{{"server", server["name"]},
                        {"path", path}, {"metadata", after[path]}, {"sha256", source["sha256"]}};
                } catch (const std::exception &e) {
                    source["status"] = "download_error"; source["error"] = QString::fromUtf8(e.what());
                    source.remove("local_path"); // Never analyze stale or partial data on failure.
                }
                ++completed;
                QTextStream(stdout) << "Download: " << completed << "/" << total << Qt::endl;
            }
        }
    }
    writeFile(manifestFile, QJsonDocument(manifest).toJson());
    for (int i = 0; i < rows.size(); ++i) {
        auto row = rows[i].toObject();
        if (row["kind"] == "output") {
            for (auto pair : {qMakePair(QString("filename"), QString("primary_source")),
                              qMakePair(QString("bb_filename"), QString("bb_source"))}) {
                auto name = row[pair.first].toString();
                row[pair.second] = files.contains(name) ? files[name] : QJsonObject{{"status",
                    audit.isEmpty() || audit.last().toObject()["status"] == "search_error" ? "unresolved" : "missing"}};
            }
        }
        rows[i] = row;
    }
    auto metrics = plan["metrics"].toObject();
    metrics["search_ms"] = double(searchMs); metrics["metadata_ms"] = double(metadataMs);
    metrics["download_ms"] = double(downloadMs); metrics["ssh_processes"] = sshCount;
    metrics["file_cache_hits"] = hits; metrics["downloads"] = downloads;
    metrics["download_bytes"] = double(bytes); metrics["requested_files"] = total;
    plan["metrics"] = metrics; plan["rows"] = rows; plan["search_audit"] = audit;
    QTextStream(stdout) << "File metrics: " << QJsonDocument(metrics).toJson(QJsonDocument::Compact) << Qt::endl;
    return plan;
}
}
