#include "Archive.h"
#include "Alignment.h"
#include "AtomMapping.h"
#include "CaptureCrop.h"
#include "Narrative.h"
#include "Workspace.h"
#include "RenderDialog.h"
#include "animation/AnimationController.h"
#include "export/ImageExporter.h"
#include "io/GaussianParser.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QImageReader>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSurfaceFormat>
#include <QTextStream>
#include <QTimer>
#include <QFont>
#include <QFileInfo>
#include <QSet>
#include <QLineEdit>
#include <cmath>
#include <stdexcept>
using namespace youngcraft;
namespace {
QVector<QVector3D> coordinates(const Molecule &m) {
    QVector<QVector3D> v; for (auto a : m.atoms) v << a.position; return v;
}
void visibility(MoleculeView &view, const Molecule &m, bool hide) {
    QVector<bool> shown(m.atoms.size(), true);
    if (hide) for (auto b : m.bonds) {
        if (m.atoms[b.first].atomicNumber == 1 && m.atoms[b.second].atomicNumber == 6) shown[b.first] = false;
        if (m.atoms[b.second].atomicNumber == 1 && m.atoms[b.first].atomicNumber == 6) shown[b.second] = false;
    }
    view.setAtomVisibility(shown);
}
}
int main(int argc, char **argv) {
    QSurfaceFormat f; f.setVersion(3, 3); f.setProfile(QSurfaceFormat::CoreProfile);
    f.setDepthBufferSize(24); f.setAlphaBufferSize(8); f.setSamples(4); QSurfaceFormat::setDefaultFormat(f);
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false); app.setFont(QFont("맑은 고딕", 10));
    app.setOrganizationName("Youngkly"); app.setApplicationName("BatchRender");
    auto args = app.arguments();
    if (args.size() < 4) { QTextStream(stderr) << "Usage: youngkly_render MODEL CONFIG OUTPUT_DIR [--batch]\n"; return 2; }
    int code = 0; RenderDialog editor; editor.show(); auto &view = *editor.view();
    QTimer::singleShot(300, &app, [&] {
        try {
            QElapsedTimer timer; timer.start();
            if (!view.isValid()) throw std::runtime_error("OpenGL context unavailable");
            auto model = QJsonDocument::fromJson(youngkly::readFile(args[1])).object();
            auto config = QJsonDocument::fromJson(youngkly::readFile(args[2])).object();
            const bool smoke = args.contains("--editor-smoke-test");
            const bool interactive = !args.contains("--batch") && (config["interactive_rendering"].toBool(true) || smoke);
            auto sections = youngkly::reportSections(model, config);
            bool pending = false;
            for (int si = 0; si < sections.size(); ++si) {
                auto section = sections[si].toObject(); auto groups = section["analyses"].toArray();
                QMap<int, QJsonObject> rows; for (auto v : section["rows"].toArray()) rows[v.toObject()["sheet_row"].toInt()] = v.toObject();
                QDir out(QDir(args[3]).filePath(section["section_id"].toString("legacy"))); out.mkpath(".");
                for (int gi = 0; gi < groups.size(); ++gi) {
                    const auto sourceGroup = groups[gi].toObject();
                    if (sourceGroup["status"] == "missing_outputs") continue;
                    try {
                    auto group = sourceGroup;
                    group.remove("captures"); // Never retain captures from a different input/view.
                    const auto allRules = config["reaction_rules"].toObject();
                    const auto key = group["rule_key"].toString();
                    const auto rule = !key.isEmpty() && allRules.contains(key) ? allRules[key].toObject() :
                                      allRules[group["section"].toString()].toObject();
                    const auto ids = group["rows"].toArray();
                    const bool spinOptimization = group["kind"] == "spin_optimization";
                    if (ids.isEmpty() || (!spinOptimization && ids.size() != 3)) throw std::runtime_error("Invalid structure group");
                    const int structureCount = ids.size(), reference = structureCount > 1 ? 1 : 0;
                    QVector<Molecule> molecules; QVector<FrequencyMode> modes(structureCount); QStringList labels;
                    QByteArray inputKey = QJsonDocument(rule).toJson(QJsonDocument::Compact);
                    for (int i = 0; i < structureCount; ++i) {
                        auto row = rows[ids[i].toInt()]; const auto path = row["primary_source"].toObject()["local_path"].toString();
                        inputKey += QCryptographicHash::hash(youngkly::readFile(path), QCryptographicHash::Sha256);
                        auto parsed = GaussianParser::parse(path);
                        if (!parsed.success || parsed.data.molecule.atoms.isEmpty()) throw std::runtime_error("Cannot parse structure " + path.toStdString());
                        auto m = parsed.data.molecule; m.rebuildBonds(); molecules << m;
                        labels << (spinOptimization ? group["structure_labels"].toArray()[i].toString() : row["role"] == "transition_state" ? "TS" : row["role"] == "irc_forward" ? "IRC forward" : "IRC reverse");
                        if (!spinOptimization && row["role"] == "transition_state") {
                            int negative = 0;
                            for (auto mode : parsed.data.frequencies) if (mode.wavenumber < 0) { modes[i] = mode; ++negative; }
                            if (negative != 1 || modes[i].displacement.size() != m.atoms.size())
                                throw std::runtime_error("TS requires exactly one complete imaginary mode");
                        }
                    }
                    const auto fingerprint = QString::fromLatin1(QCryptographicHash::hash(inputKey + QJsonDocument(group["structure_labels"].toArray()).toJson() + "group-view-v3", QCryptographicHash::Sha256).toHex());
                    const auto savedPath = out.absoluteFilePath(QString("group-%1-view.json").arg(gi + 1));
                    QJsonObject saved;
                    if (QFileInfo::exists(savedPath)) saved = QJsonDocument::fromJson(youngkly::readFile(savedPath)).object();
                    if (saved["input_hash"] != fingerprint) saved = {};
                    const int count = molecules[reference].atoms.size();
                    QVector<QVector<int>> elements;
                    for (const auto &m : molecules) { QVector<int> e; for (auto a : m.atoms) e << a.atomicNumber; elements << e; }
                    const auto mapping = youngkly::atomCorrespondence(elements, rule["atom_mapping"].toArray());
                    if (!rule["atom_mapping"].toArray().isEmpty() && group["atom_mapping"].toArray() != youngkly::mappingJson(mapping))
                        throw std::runtime_error("Atom mapping changed; reanalyze the model before rendering");
                    for (int i = 0; i < structureCount; ++i) {
                        const auto original = molecules[i].atoms; const auto displacement = modes[i].displacement;
                        for (int a = 0; a < count; ++a) {
                            molecules[i].atoms[a] = original[mapping[i][a] - 1]; molecules[i].atoms[a].index = a + 1;
                            if (!displacement.isEmpty()) modes[i].displacement[a] = displacement[mapping[i][a] - 1];
                        }
                        molecules[i].bonds.clear(); molecules[i].rebuildBonds();
                    }
                    bool confirmed = rule["atom_mapping_confirmed"].toBool() || saved["mapping_confirmed"].toBool();
                    if (!interactive && !confirmed) {
                        group["render_status"] = "atom_mapping_confirmation_required"; groups[gi] = group; pending = true;
                        QTextStream(stdout) << "Mapping confirmation required: " << group["section"].toString() << Qt::endl; continue;
                    }
                    QJsonArray anchors = saved["anchors"].toArray();
                    if (anchors.isEmpty()) anchors = rule["alignment_anchor_atom_ids"].toArray();
                    if (anchors.isEmpty()) for (int a = 0; a < count; ++a) if (molecules[reference].atoms[a].atomicNumber > 1) anchors << a + 1;
                    if (interactive && !smoke) {
                        QStringList values; for (auto id : anchors) values << QString::number(id.toInt());
                        bool ok = false;
                        auto input = QInputDialog::getText(&editor, "공통 골격 정렬", "기준 구조의 anchor atom IDs (쉼표로 구분).\n같은 ID의 원자가 대응하는지 다음 화면에서 확인하세요.", QLineEdit::Normal, values.join(","), &ok);
                        if (!ok) throw std::runtime_error("Group alignment cancelled");
                        anchors = {}; QSet<int> used;
                        for (const auto &part : input.split(',', Qt::SkipEmptyParts)) {
                            bool valid; int id = part.trimmed().toInt(&valid);
                            if (!valid || id < 1 || id > count || used.contains(id)) throw std::runtime_error("Invalid or duplicate anchor atom ID");
                            used.insert(id); anchors << id;
                        }
                    }
                    QVector<QVector3D> target; for (auto id : anchors) {
                        if (id.toInt() < 1 || id.toInt() > count) throw std::runtime_error("Anchor out of range");
                        target << molecules[reference].atoms[id.toInt() - 1].position;
                    }
                    QJsonArray transforms, alignmentRmsd;
                    for (int i = 0; i < structureCount; ++i) {
                        QVector<QVector3D> source; for (auto id : anchors) source << molecules[i].atoms[id.toInt() - 1].position;
                        auto transform = youngkly::rigidAlignment(source, target);
                        double residual = 0.0;
                        for (int a = 0; a < source.size(); ++a)
                            residual += double((transform.map(source[a]) - target[a]).lengthSquared());
                        alignmentRmsd << std::sqrt(residual / qMax(1, source.size()));
                        for (auto &a : molecules[i].atoms) a.position = transform.map(a.position);
                        for (auto &d : modes[i].displacement) d = transform.mapVector(d);
                        molecules[i].rebuildBonds(); QJsonArray matrix;
                        for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) matrix << transform(r, c);
                        transforms << matrix;
                    }
                    const bool keepCH = rule["c_h_activation"].toBool();
                    editor.configureGroup(molecules, labels, youngkly::displayHeading(section["project_heading"].toString()) + " / " + group["section"].toString(), keepCH, spinOptimization ? nullptr : &modes[reference], interactive || confirmed);
                    app.processEvents();
                    // A bounding sphere includes all structures and all motion phases and remains safe under rotation.
                    QVector<QVector3D> all;
                    for (auto m : molecules) all += coordinates(m);
                    const auto base = coordinates(molecules[reference]);
                    if (!spinOptimization) for (int frame = 0; frame < 32; ++frame)
                        all += AnimationController::coordinatesAtPhase(base, modes[reference], 0.5, 2 * std::acos(-1.0) * frame / 32.0);
                    QVector3D lo = all[0], hi = all[0];
                    for (auto p : all) for (int d = 0; d < 3; ++d) { lo[d] = qMin(lo[d], p[d]); hi[d] = qMax(hi[d], p[d]); }
                    auto center = (lo + hi) / 2; float radius = 0;
                    for (auto p : all) radius = qMax(radius, (p - center).length());
                    radius += 1.8f;
                    const float aspect = float(view.width()) / qMax(1, view.height());
                    auto initial = view.reportCameraState();
                    initial["target"] = QJsonArray{center.x(), center.y(), center.z()}; initial["rotation"] = QJsonArray{1, 0, 0, 0};
                    initial["distance"] = radius * 4 + 10; initial["ortho_scale"] = radius * 1.12 / qMin(1.0f, aspect); initial["perspective"] = false;
                    view.setProperty("groupDefaultCamera", initial);
                    view.applyCameraState(initial);
                    if (!saved.isEmpty()) {
                        auto o = saved["options"].toObject();
                        editor.restoreOptions(o["hide_ch"].toBool(true), o["activation"].toBool(), o["shadows"].toBool(true), o["brightness"].toInt(135), o["shadow_strength"].toInt(24));
                        view.applyCameraState(saved["camera"].toObject());
                    }
                    if (interactive) {
                        if (smoke) QTimer::singleShot(300, &editor, [&] { editor.accept(); });
                        if (editor.exec() != QDialog::Accepted) throw std::runtime_error("Group view cancelled");
                        if (!editor.mappingConfirmed()) throw std::runtime_error("Atom mapping was not confirmed");
                    }
                    editor.stopPreview();
                    const auto camera = view.reportCameraState(), options = editor.optionsState(); const auto style = view.style();
                    const int stillEdge = qBound(512, config["render_image_long_edge_px"].toInt(3000), 8192);
                    const int motionEdge = qBound(256, config["render_animation_long_edge_px"].toInt(1280), 4096);
                    const auto stillSize = view.size().scaled(stillEdge, stillEdge, Qt::KeepAspectRatio);
                    const auto motionSize = view.size().scaled(motionEdge, motionEdge, Qt::KeepAspectRatio);
                    QVector<QImage> stills, frames, cropImages;
                    for (int i = 0; i < structureCount; ++i) {
                        view.setMolecule(molecules[i]); view.setStyle(style); view.applyCameraState(camera);
                        visibility(view, molecules[i], options["hide_ch"].toBool());
                        auto image = view.imageAtSize(stillSize, false, true);
                        if (image.isNull()) throw std::runtime_error("Still rendering failed");
                        stills << image; cropImages << image.scaled(motionSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                        if (!spinOptimization && i == reference) for (int frame = 0; frame < 32; ++frame) {
                            view.setCoordinates(AnimationController::coordinatesAtPhase(base, modes[reference], 0.5, 2 * std::acos(-1.0) * frame / 32.0));
                            view.rebuildBonds(style.recountCutoffScale, true, true);
                            auto motion = view.imageAtSize(motionSize, false, true);
                            if (motion.isNull()) throw std::runtime_error("Motion rendering failed");
                            frames << motion; cropImages << motion;
                        }
                    }
                    const auto crop = youngkly::captureCrop(cropImages);
                    const double sx = double(stillSize.width()) / motionSize.width(), sy = double(stillSize.height()) / motionSize.height();
                    const QRect stillCrop(QPoint(int(std::floor(crop.rect.x() * sx)), int(std::floor(crop.rect.y() * sy))),
                        QPoint(int(std::ceil((crop.rect.right() + 1) * sx)) - 1, int(std::ceil((crop.rect.bottom() + 1) * sy)) - 1));
                    QJsonArray captures;
                    for (int i = 0; i < structureCount; ++i) {
                        auto stem = out.absoluteFilePath(QString("group-%1-row-%2").arg(gi + 1).arg(ids[i].toInt()));
                        auto image = stills[i].copy(stillCrop.intersected(stills[i].rect())); image.setDotsPerMeterX(23622); image.setDotsPerMeterY(23622);
                        if (!image.save(stem + ".png")) throw std::runtime_error("PNG export failed");
                        QJsonObject capture{{"sheet_row", ids[i]}, {"png", stem + ".png"}, {"view", "shared_group_camera"},
                            {"camera", camera}, {"png_width_px", image.width()}, {"png_height_px", image.height()}, {"png_dpi", 600},
                            {"width_cm", config["image_width_cm"].toDouble(5)}, {"rendered_at_export_resolution", true},
                            {"carbon_hydrogen_hidden", options["hide_ch"]}, {"c_h_activation", options["activation"]},
                            {"crop", QJsonObject{{"x", crop.rect.x()}, {"y", crop.rect.y()}, {"width", crop.rect.width()}, {"height", crop.rect.height()},
                            {"original_width", motionSize.width()}, {"original_height", motionSize.height()}, {"group_union", true}, {"animation_union", !spinOptimization}}}};
                        if (!spinOptimization && i == reference) {
                            for (auto &frame : frames) frame = frame.copy(crop.rect);
                            QString error; auto gif = stem + ".gif";
                            if (!ImageExporter::saveGif(frames, gif, 60, false, true, &error)) throw std::runtime_error(error.toStdString());
                            QImageReader reader(gif); int decoded = 0; while (reader.canRead()) { if (reader.read().isNull()) throw std::runtime_error("GIF decode failed"); ++decoded; }
                            if (decoded != 32) throw std::runtime_error("GIF frame count mismatch");
                            capture["animation"] = gif; capture["frames"] = decoded; capture["bond_recount"] = true;
                            capture["imaginary_frequency_cm_1"] = modes[reference].wavenumber;
                            capture["animation_width_px"] = frames[0].width(); capture["animation_height_px"] = frames[0].height();
                        }
                        captures << capture;
                    }
                    QJsonObject state{{"input_hash", fingerprint}, {"camera", camera}, {"options", options}, {"anchors", anchors},
                        {"mapping_confirmed", true}, {"atom_mapping", youngkly::mappingJson(mapping)},
                        {"transforms", transforms}, {"alignment_anchor_rmsd_angstrom", alignmentRmsd}};
                    youngkly::writeFile(savedPath, QJsonDocument(state).toJson());
                    group["captures"] = captures; group["group_view"] = state; group["atom_mapping_confirmed"] = true; group["atom_mapping_valid"] = true;
                    group["render_status"] = "rendered"; youngkly::rebuildNarratives(group, config); groups[gi] = group;
                    QTextStream(stdout) << "Rendered group: " << gi + 1 << "/" << groups.size() << Qt::endl;
                    } catch (const std::exception &error) {
                        auto failed = sourceGroup;
                        failed.remove("captures");
                        failed["render_status"] = "render_error";
                        failed["render_error"] = QString::fromUtf8(error.what());
                        groups[gi] = failed;
                        pending = true;
                        QTextStream(stderr) << "Render group " << gi + 1 << ": " << error.what() << Qt::endl;
                    }
                }
                section["analyses"] = groups; sections[si] = section;
            }
            if (model.contains("sections")) model["sections"] = sections; else model["analyses"] = sections[0].toObject()["analyses"];
            auto metrics = model["metrics"].toObject(); metrics["render_ms"] = double(timer.elapsed()); model["metrics"] = metrics;
            youngkly::writeFile(args[1], QJsonDocument(model).toJson());
            QTextStream(stdout) << "Render ms: " << timer.elapsed() << Qt::endl;
            if (pending) code = 3;
        } catch (const std::exception &e) { QTextStream(stderr) << "Youngkly render: " << e.what() << "\n"; code = 1; }
        app.quit();
    });
    app.exec(); return code;
}
