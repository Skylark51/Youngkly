#include "RenderDialog.h"
#include "animation/AnimationController.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDialogButtonBox>
#include <cmath>
#include <QSignalBlocker>
#include <stdexcept>
using namespace youngcraft;
RenderDialog::RenderDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Youngkly — 분자 뷰 선택");
    setWindowFlags(windowFlags() | Qt::WindowMinMaxButtonsHint);
    resize(1100, 850);
    auto *layout = new QVBoxLayout(this);
    description_ = new QLabel;
    description_->setWordWrap(true);
    layout->addWidget(description_);
    auto *help = new QLabel("오른쪽 드래그: 회전   ·   가운데 드래그: 이동   ·   휠: 확대/축소");
    layout->addWidget(help);
    view_ = new MoleculeView;
    view_->setMinimumSize(500, 400);
    auto *views = new QHBoxLayout;
    views_ = views;
    companions_ = {new MoleculeView, new MoleculeView};
    for (auto *other : companions_) { other->setMinimumSize(280, 330); other->hide(); }
    views->addWidget(companions_[0], 1); views->addWidget(view_, 1); views->addWidget(companions_[1], 1);
    layout->addLayout(views, 1);
    mapping_ = new QCheckBox("세 구조의 같은 atom ID가 같은 원자임을 확인했습니다");
    mapping_->setObjectName("mappingConfirmed"); mapping_->hide(); layout->addWidget(mapping_);
    auto *options = new QHBoxLayout;
    hide_ = new QCheckBox("C에 붙은 수소 숨기기");
    hide_->setObjectName("hideCarbonHydrogen");
    hide_->setChecked(true);
    activation_ = new QCheckBox("C–H activation (수소 유지)");
    activation_->setObjectName("chActivation");
    shadow_ = new QCheckBox("그림자");
    shadow_->setObjectName("shadows");
    shadow_->setChecked(true);
    motion_ = new QCheckBox("TS 진동 미리보기");
    options->addWidget(hide_);
    options->addWidget(activation_);
    options->addWidget(shadow_);
    options->addWidget(motion_);
    layout->addLayout(options);
    auto *light = new QHBoxLayout;
    light->addWidget(new QLabel("밝기"));
    brightness_ = new QSlider(Qt::Horizontal);
    brightness_->setRange(70, 180);
    brightness_->setValue(135);
    light->addWidget(brightness_);
    light->addWidget(new QLabel("그림자 강도"));
    shadowStrength_ = new QSlider(Qt::Horizontal);
    shadowStrength_->setRange(0, 60);
    shadowStrength_->setValue(24);
    light->addWidget(shadowStrength_);
    layout->addLayout(light);
    auto *buttons = new QHBoxLayout;
    auto *reset = new QPushButton("뷰 초기화");
    auto *cancel = new QPushButton("취소");
    auto *save = new QPushButton("이 뷰로 저장 → 다음 구조");
    save_ = save;
    save->setDefault(true);
    buttons->addWidget(reset);
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(save);
    layout->addLayout(buttons);
    connect(reset, &QPushButton::clicked, this, [this] {
        if (!group_.isEmpty()) {
            auto state = view_->property("groupDefaultCamera").toJsonObject(); view_->applyCameraState(state);
        } else view_->bestView();
    });
    connect(mapping_, &QCheckBox::toggled, save, &QPushButton::setEnabled);
    // Synchronize only when the reference camera changes, without polling.
    connect(view_, &MoleculeView::cameraChanged, this, [this] {
        if (group_.isEmpty()) return;
        const int reference = group_.size() > 1 ? 1 : 0;
        for (int j = 0; j < group_.size() - 1; ++j) {
            auto *other = companions_[j]; const auto &m = group_[j < reference ? j : j + 1];
            other->setStyle(view_->style()); other->applyCameraState(view_->reportCameraState());
            QVector<bool> shown(m.atoms.size(), true);
            if (carbonHydrogenHidden()) for (const auto &b : m.bonds) {
                if (m.atoms[b.first].atomicNumber == 1 && m.atoms[b.second].atomicNumber == 6) shown[b.first] = false;
                if (m.atoms[b.second].atomicNumber == 1 && m.atoms[b.first].atomicNumber == 6) shown[b.second] = false;
            }
            other->setAtomVisibility(shown);
        }
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, &QDialog::accept);
    connect(hide_, &QCheckBox::toggled, this, [this] { visibility(); });
    connect(activation_, &QCheckBox::toggled, this, [this](bool on) {
        hide_->setEnabled(!on);
        hide_->setChecked(!on);
        visibility();
    });
    connect(shadow_, &QCheckBox::toggled, this, [this] { lighting(); });
    connect(brightness_, &QSlider::valueChanged, this, [this] { lighting(); });
    connect(shadowStrength_, &QSlider::valueChanged, this, [this] { lighting(); });
    timer_.setInterval(60);
    connect(&timer_, &QTimer::timeout, this, [this] {
        QVector<QVector3D> base;
        for (const auto &atom : molecule_.atoms)
            base << atom.position;
        view_->setCoordinates(AnimationController::coordinatesAtPhase(
            base, mode_, 0.5, 2 * std::acos(-1.0) * (frame_++ % 32) / 32.0));
        view_->rebuildBonds(view_->style().recountCutoffScale, true, true);
    });
    connect(motion_, &QCheckBox::toggled, this, [this](bool on) {
        if (on)
            timer_.start();
        else
            stopPreview();
    });
}
void RenderDialog::configure(const Molecule &m, const QString &description, bool activation,
                             const FrequencyMode *mode) {
    stopPreview();
    molecule_ = m;
    description_->setText(description);
    view_->setMolecule(m);
    activation_->setChecked(activation);
    hide_->setEnabled(!activation);
    hide_->setChecked(!activation);
    motion_->setEnabled(mode != nullptr);
    if (mode)
        mode_ = *mode;
    lighting();
    visibility();
    view_->bestView();
}
void RenderDialog::visibility() {
    if (molecule_.atoms.isEmpty())
        return;
    QVector<bool> shown(molecule_.atoms.size(), true);
    if (carbonHydrogenHidden())
        for (const auto &b : molecule_.bonds) {
            const auto za = molecule_.atoms[b.first].atomicNumber,
                       zb = molecule_.atoms[b.second].atomicNumber;
            if (za == 1 && zb == 6)
                shown[b.first] = false;
            if (zb == 1 && za == 6)
                shown[b.second] = false;
        }
    view_->setAtomVisibility(shown);
}
void RenderDialog::stopPreview() {
    timer_.stop();
    motion_->setChecked(false);
    if (molecule_.atoms.isEmpty())
        return;
    QVector<QVector3D> base;
    for (const auto &atom : molecule_.atoms)
        base << atom.position;
    view_->setCoordinates(base);
    view_->rebuildBonds(view_->style().recountCutoffScale, true, true);
}
void RenderDialog::lighting() {
    auto style = StyleManager::preset("Youngcraft Studio");
    if (!group_.isEmpty()) style.perspective = false;
    const float brightnessScale = brightness_->value() / 135.0f;
    style.ambientIntensity *= brightnessScale;
    style.diffuseIntensity *= brightnessScale;
    style.shadows = shadow_->isChecked();
    style.shadowStrength = shadowStrength_->value() / 100.0f;
    view_->setStyle(style);
}

void RenderDialog::configureGroup(const QVector<Molecule> &molecules, const QStringList &labels,
                                  const QString &description, bool activation,
                                  const FrequencyMode *mode, bool confirmed) {
    if (molecules.isEmpty() || labels.size() != molecules.size())
        throw std::runtime_error("Group molecules and labels must be nonempty and match");
    // Camera changes during configure() must not access companion panes from the previous group.
    const QSignalBlocker cameraSignals(view_);
    group_ = molecules;
    view_->setMinimumSize(280, 330);
    resize(1250, 790);
    const int reference = molecules.size() > 1 ? 1 : 0;
    configure(molecules[reference], description + "\n시트 순서: " + labels.join(" | ") +
        "\n" + labels[reference] + "에서 회전·이동·확대하면 전체 구조에 함께 적용됩니다.", activation, mode);
    mapping_->setText("구조들의 같은 atom ID가 같은 원자임을 확인했습니다");
    mapping_->show(); mapping_->setChecked(confirmed); save_->setEnabled(confirmed);
    save_->setText("그룹 전체 적용 → 다음 경로");
    while (companions_.size() < molecules.size() - 1) {
        auto *other = new MoleculeView; other->setMinimumSize(180, 330);
        companions_ << other; views_->addWidget(other, 1);
    }
    for (int j = 0; j < companions_.size(); ++j) {
        if (j >= molecules.size() - 1) { companions_[j]->hide(); continue; }
        companions_[j]->setMolecule(molecules[j < reference ? j : j + 1]); companions_[j]->show();
    }
    for (auto *other : companions_) if (other->isVisible()) {
        other->setStyle(view_->style());
        other->applyCameraState(view_->reportCameraState());
    }
}
void RenderDialog::restoreOptions(bool hideCH, bool activation, bool shadows, int brightness, int strength) {
    activation_->setChecked(activation); hide_->setChecked(hideCH);
    shadow_->setChecked(shadows); brightness_->setValue(brightness); shadowStrength_->setValue(strength);
    lighting(); visibility();
}
