#include "RenderDialog.h"
#include <QApplication>
#include <QSurfaceFormat>
#include <QTimer>
#include <QEventLoop>
#include <QMouseEvent>
#include <QTextStream>
#include <stdexcept>
using namespace youngcraft;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main(int argc, char **argv) {
    QSurfaceFormat f;
    f.setVersion(3, 3);
    f.setProfile(QSurfaceFormat::CoreProfile);
    f.setDepthBufferSize(24);
    f.setSamples(4);
    QSurfaceFormat::setDefaultFormat(f);
    QApplication app(argc, argv);
    RenderDialog editor;
    editor.show();
    int code = 0;
    QTimer::singleShot(300, &app, [&] {
        try {
            Molecule m;
            for (const auto &spec : QVector<QPair<int, QVector3D>>{
                     {6, {0, 0, 0}}, {1, {1.05, 0, 0}}, {8, {0, 1.3, 0}}, {1, {0, 1.3, 0.95}}}) {
                Atom a;
                a.atomicNumber = spec.first;
                a.position = spec.second;
                m.atoms << a;
            }
            m.rebuildBonds();
            editor.configure(m, "Hydrogen control regression", false);
            const auto studio = StyleManager::preset("Youngcraft Studio");
            const auto appliedStyle = editor.view()->style();
            check(appliedStyle.elementColors == studio.elementColors &&
                      appliedStyle.elementRadiusScales == studio.elementRadiusScales &&
                      appliedStyle.atomScale == studio.atomScale && appliedStyle.bondRadius == studio.bondRadius &&
                      appliedStyle.ambientIntensity == studio.ambientIntensity &&
                      appliedStyle.diffuseIntensity == studio.diffuseIntensity &&
                      appliedStyle.atomSheenStrength == studio.atomSheenStrength,
                  "Youngkly and Youngcraft use the same Studio preset");
            auto *hide = editor.findChild<QCheckBox *>("hideCarbonHydrogen");
            auto *activation = editor.findChild<QCheckBox *>("chActivation");
            check(editor.view()->isValid(), "OpenGL");
            check(!editor.view()->molecule().atoms[1].visible && editor.view()->molecule().atoms[3].visible,
                  "Hide C-H while retaining O-H");
            hide->setChecked(false);
            check(editor.view()->molecule().atoms[1].visible, "Manual hydrogen toggle");
            activation->setChecked(true);
            check(!hide->isEnabled() && editor.view()->molecule().atoms[1].visible,
                  "C-H activation retains hydrogens");
            activation->setChecked(false);
            check(!editor.view()->molecule().atoms[1].visible, "Default hide restored");
            const auto before = editor.view()->image(false, true);
            const QPointF start(30, 30), end(130, 100);
            QMouseEvent press(QEvent::MouseButtonPress, start, editor.view()->mapToGlobal(start.toPoint()),
                              Qt::RightButton, Qt::RightButton, Qt::NoModifier);
            QMouseEvent move(QEvent::MouseMove, end, editor.view()->mapToGlobal(end.toPoint()), Qt::NoButton,
                             Qt::RightButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, end, editor.view()->mapToGlobal(end.toPoint()),
                                Qt::RightButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(editor.view(), &press);
            QApplication::sendEvent(editor.view(), &move);
            QApplication::sendEvent(editor.view(), &release);
            const auto rotated = editor.view()->image(false, true);
            check(before != rotated, "User rotation affects capture");
            editor.stopPreview();
            check(editor.view()->image(false, true) == rotated, "Capture preserves user-selected camera");
            const auto cameraBeforeExport = editor.view()->image(false, true);
            const auto smallExport = editor.view()->imageAtSize(QSize(400, 300), true, true);
            const auto largeExport = editor.view()->imageAtSize(QSize(1600, 1200), true, true);
            check(largeExport.size() == QSize(1600, 1200), "Export uses requested raster size");
            check(largeExport != smallExport.scaled(largeExport.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation),
                  "High-resolution export rerenders geometry instead of enlarging a bitmap");
            check(largeExport.dotsPerMeterX() == qRound(600.0 / 0.0254), "Publication DPI metadata");
            int transparentPixels = 0, coveredPixels = 0, edgePixels = 0, saturatedRedEdges = 0;
            for (int y = 0; y < largeExport.height(); ++y)
                for (int x = 0; x < largeExport.width(); ++x) {
                    const auto pixel = largeExport.pixel(x, y);
                    if (qAlpha(pixel) == 0) ++transparentPixels;
                    else if (qAlpha(pixel) == 255) ++coveredPixels;
                    else {
                        ++edgePixels;
                        if (qRed(pixel) > 180 && qRed(pixel) > qBlue(pixel) + 100) ++saturatedRedEdges;
                    }
                }
            check(transparentPixels > 1000 && coveredPixels > 1000 && edgePixels > 100,
                  "Transparent export retains smooth partial-coverage edges");
            check(saturatedRedEdges > 0, "Transparent edges retain straight-alpha color");
            check(editor.view()->image(false, true) == cameraBeforeExport, "Offscreen export preserves live camera and viewport");
            auto labelledStyle = editor.view()->style();
            labelledStyle.showLabels = true;
            editor.view()->setStyle(labelledStyle);
            const auto labelledExport = editor.view()->imageAtSize(QSize(1600, 1200), true, true);
            check(labelledExport != largeExport, "Labels render into the high-resolution framebuffer");
            check(editor.view()->imageAtSize(QSize()).isNull(), "Reject invalid export size");
            labelledStyle.showLabels = false;
            editor.view()->setStyle(labelledStyle);
            auto *shadows = editor.findChild<QCheckBox *>("shadows");
            check(shadows->isChecked() && editor.view()->style().shadows, "Shadows default enabled");
            shadows->setChecked(false);
            check(!editor.view()->style().shadows, "Shadow toggle");
            const auto savedCamera = editor.view()->reportCameraState();
            const auto savedImage = editor.view()->image(false, true);
            editor.view()->bestView();
            check(editor.view()->applyCameraState(savedCamera), "Camera API accepts serialized state");
            check(editor.view()->image(false, true) == savedImage, "Camera serialization preserves rendered view");
            auto badCamera = savedCamera; badCamera["ortho_scale"] = -1;
            check(!editor.view()->applyCameraState(badCamera) && editor.view()->reportCameraState() == savedCamera, "Reject invalid camera without mutation");
            editor.configureGroup({m, m, m}, {"IRC forward", "TS", "IRC reverse"}, "Group view", false, nullptr, false);
            editor.configureGroup({m}, {"Multiplicity 1"}, "Single optimization", false, nullptr, true);
            editor.configureGroup({m, m}, {"Multiplicity 2", "Multiplicity 4"}, "Spin states", false, nullptr, true);
            editor.configureGroup({m, m, m, m}, {"Multiplicity 1", "Multiplicity 3", "Multiplicity 5", "Multiplicity 7"}, "Four spin states", false, nullptr, true);
            editor.configureGroup({m, m, m}, {"IRC forward", "TS", "IRC reverse"}, "Group view", false, nullptr, false);
            auto *mapping = editor.findChild<QCheckBox *>("mappingConfirmed");
            check(mapping && !mapping->isChecked(), "Group mapping requires confirmation");
            mapping->setChecked(true);
            check(editor.mappingConfirmed(), "Group mapping confirmation");
            auto state = editor.view()->reportCameraState(); state["ortho_scale"] = 5; state["perspective"] = false;
            editor.view()->applyCameraState(state);
            QEventLoop syncLoop; QTimer::singleShot(120, &syncLoop, &QEventLoop::quit); syncLoop.exec();
            for (auto *pane : editor.findChildren<MoleculeView *>())
                if (pane->isVisible()) check(pane->reportCameraState() == editor.view()->reportCameraState(), "Visible panes share the camera");
            QTextStream(stdout) << "Hydrogen visibility, C-H exception, manual view and lighting passed\n";
        } catch (const std::exception &e) {
            QTextStream(stderr) << e.what() << Qt::endl;
            code = 1;
        }
        app.quit();
    });
    app.exec();
    return code;
}
