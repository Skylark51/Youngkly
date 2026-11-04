#pragma once
#include "render/MoleculeView.h"
#include <QDialog>
#include <QCheckBox>
#include <QSlider>
#include <QLabel>
#include <QTimer>
#include <QPushButton>
#include <QHBoxLayout>
class RenderDialog : public QDialog {
  public:
    explicit RenderDialog(QWidget *parent = nullptr);
    void configure(const youngcraft::Molecule &molecule, const QString &description, bool chActivation,
                   const youngcraft::FrequencyMode *mode = nullptr);
    youngcraft::MoleculeView *view() const {
        return view_;
    }
    bool carbonHydrogenHidden() const {
        return hide_->isChecked() && !activation_->isChecked();
    }
    bool chActivation() const {
        return activation_->isChecked();
    }
    void stopPreview();
    void configureGroup(const QVector<youngcraft::Molecule> &molecules, const QStringList &labels,
                        const QString &description, bool chActivation,
                        const youngcraft::FrequencyMode *mode, bool mappingConfirmed);
    void restoreOptions(bool hideCH, bool activation, bool shadows, int brightness, int strength);
    bool mappingConfirmed() const { return mapping_->isChecked(); }
    QJsonObject optionsState() const {
        return {{"hide_ch", carbonHydrogenHidden()}, {"activation", chActivation()},
                {"shadows", shadow_->isChecked()}, {"brightness", brightness_->value()},
                {"shadow_strength", shadowStrength_->value()}};
    }

  private:
    void visibility();
    void lighting();
    youngcraft::MoleculeView *view_;
    QLabel *description_;
    QCheckBox *hide_, *activation_, *shadow_, *motion_;
    QSlider *brightness_, *shadowStrength_;
    youngcraft::Molecule molecule_;
    youngcraft::FrequencyMode mode_;
    QTimer timer_;
    int frame_{};
    QVector<youngcraft::MoleculeView *> companions_;
    QHBoxLayout *views_;
    QVector<youngcraft::Molecule> group_;
    QCheckBox *mapping_;
    QPushButton *save_;
    QTimer sync_;
};
