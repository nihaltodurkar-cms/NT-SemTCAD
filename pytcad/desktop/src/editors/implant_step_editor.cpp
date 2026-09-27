#include "implant_step_editor.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>

#include <initializer_list>

namespace tcad::desktop {
namespace {
constexpr double kCmPerUm = 1e-4;
}

ImplantStepEditor::ImplantStepEditor(QWidget* parent) : QWidget(parent) {
    form_ = new QFormLayout(this);
    species_ = new QComboBox(this);
    species_->setObjectName("implantSpecies");
    species_->addItems({"B", "P", "As"});
    form_->addRow(tr("Species"), species_);

    energy_kev_ = new QDoubleSpinBox(this);
    energy_kev_->setObjectName("implantEnergyKeV");
    energy_kev_->setDecimals(3);
    energy_kev_->setRange(0.0, 1e6);
    form_->addRow(tr("Energy [keV]"), energy_kev_);

    dose_cm2_ = new QDoubleSpinBox(this);
    dose_cm2_->setObjectName("implantDoseCm2");
    dose_cm2_->setDecimals(0);
    dose_cm2_->setRange(0.0, 1e21);
    dose_cm2_->setSingleStep(1e12);
    form_->addRow(tr("Dose [cm^-2]"), dose_cm2_);

    tilt_deg_ = new QDoubleSpinBox(this);
    tilt_deg_->setObjectName("implantTiltDeg");
    tilt_deg_->setDecimals(2);
    tilt_deg_->setRange(0.0, 89.999);
    form_->addRow(tr("Tilt [deg]"), tilt_deg_);

    window_enabled_ = new QCheckBox(tr("Restrict to window"), this);
    window_enabled_->setObjectName("implantWindowEnabled");
    form_->addRow(window_enabled_);

    window_from_um_ = new QDoubleSpinBox(this);
    window_from_um_->setObjectName("implantWindowFromUm");
    window_from_um_->setDecimals(4);
    window_from_um_->setRange(0.0, 1e6);
    form_->addRow(tr("Window from [um]"), window_from_um_);

    window_to_um_ = new QDoubleSpinBox(this);
    window_to_um_->setObjectName("implantWindowToUm");
    window_to_um_->setDecimals(4);
    window_to_um_->setRange(0.0, 1e6);
    form_->addRow(tr("Window to [um]"), window_to_um_);

    connect(species_, &QComboBox::currentTextChanged, this, [this](const QString&) {
        if (!loading_) writeBack();
    });
    const std::initializer_list<QDoubleSpinBox*> numFields = {energy_kev_, dose_cm2_, tilt_deg_,
                                                              window_from_um_, window_to_um_};
    for (QDoubleSpinBox* box : numFields)
        connect(box, &QDoubleSpinBox::editingFinished, this, [this] {
            if (!loading_) writeBack();
        });
    connect(window_enabled_, &QCheckBox::toggled, this, [this](bool) {
        if (loading_) return;
        updateWindowRowVisibility();
        writeBack();
    });
    setStep(nullptr, QString());
}

void ImplantStepEditor::setStep(ProcessFlowDocument* doc, const QString& stepId) {
    doc_ = doc;
    step_id_ = stepId;
    const bool on = doc_ != nullptr && !step_id_.isEmpty();
    const std::initializer_list<QWidget*> fields = {species_, energy_kev_, dose_cm2_, tilt_deg_,
                                                    window_enabled_, window_from_um_, window_to_um_};
    for (QWidget* w : fields) w->setEnabled(on);
    refresh();
}

void ImplantStepEditor::refresh() {
    loading_ = true;
    if (doc_ && !step_id_.isEmpty()) {
        for (std::size_t i = 0; i < doc_->step_count(); ++i) {
            const ProcessStepData s = doc_->step(i);
            if (s.id == step_id_.toStdString()) {
                species_->setCurrentText(QString::fromStdString(s.parameters.value("species", std::string("B"))));
                energy_kev_->setValue(s.parameters.value("energy_keV", 0.0));
                dose_cm2_->setValue(s.parameters.value("dose_cm2", 0.0));
                tilt_deg_->setValue(s.parameters.value("tilt_deg", 0.0));
                const bool has_window = s.parameters.contains("x_range_cm") &&
                                        !s.parameters.at("x_range_cm").is_null();
                window_enabled_->setChecked(has_window);
                if (has_window) {
                    const auto& range = s.parameters.at("x_range_cm");
                    window_from_um_->setValue(range.at(0).get<double>() / kCmPerUm);
                    window_to_um_->setValue(range.at(1).get<double>() / kCmPerUm);
                } else {
                    window_from_um_->setValue(0.0);
                    window_to_um_->setValue(0.0);
                }
                break;
            }
        }
    }
    updateWindowRowVisibility();
    loading_ = false;
}

void ImplantStepEditor::updateWindowRowVisibility() {
    form_->setRowVisible(window_from_um_, window_enabled_->isChecked());
    form_->setRowVisible(window_to_um_, window_enabled_->isChecked());
}

void ImplantStepEditor::writeBack() {
    if (!doc_ || step_id_.isEmpty()) return;
    nlohmann::ordered_json params;
    params["species"] = species_->currentText().toStdString();
    params["energy_keV"] = energy_kev_->value();
    params["dose_cm2"] = dose_cm2_->value();
    params["tilt_deg"] = tilt_deg_->value();
    if (window_enabled_->isChecked())
        params["x_range_cm"] = {window_from_um_->value() * kCmPerUm, window_to_um_->value() * kCmPerUm};
    doc_->set_step_parameters(step_id_.toStdString(), params);
    emit stepEdited(step_id_);
}

}  // namespace tcad::desktop
