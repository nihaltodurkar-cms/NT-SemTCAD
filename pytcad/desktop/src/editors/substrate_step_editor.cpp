#include "substrate_step_editor.hpp"

#include <QDoubleSpinBox>
#include <QFormLayout>

#include <initializer_list>

namespace tcad::desktop {

SubstrateStepEditor::SubstrateStepEditor(QWidget* parent) : QWidget(parent) {
    auto* form = new QFormLayout(this);
    length_cm_ = new QDoubleSpinBox(this);
    length_cm_->setObjectName("substrateLengthCm");
    length_cm_->setDecimals(9);
    length_cm_->setRange(0.0, 1.0);
    form->addRow(tr("Length [cm]"), length_cm_);

    background_doping_ = new QDoubleSpinBox(this);
    background_doping_->setObjectName("substrateBackgroundDoping");
    background_doping_->setDecimals(0);
    background_doping_->setRange(-1e21, 1e21);
    background_doping_->setSingleStep(1e14);
    form->addRow(tr("Bg doping [cm^-3]"), background_doping_);

    h_min_ = new QDoubleSpinBox(this);
    h_min_->setObjectName("substrateHMinCm");
    h_min_->setDecimals(9);
    h_min_->setRange(0.0, 1.0);
    form->addRow(tr("Mesh h_min [cm]"), h_min_);

    h_max_ = new QDoubleSpinBox(this);
    h_max_->setObjectName("substrateHMaxCm");
    h_max_->setDecimals(9);
    h_max_->setRange(0.0, 1.0);
    form->addRow(tr("Mesh h_max [cm]"), h_max_);

    ratio_ = new QDoubleSpinBox(this);
    ratio_->setObjectName("substrateRatio");
    ratio_->setDecimals(3);
    ratio_->setRange(1.001, 3.0);
    form->addRow(tr("Mesh ratio"), ratio_);

    const std::initializer_list<QDoubleSpinBox*> fields = {length_cm_, background_doping_, h_min_,
                                                            h_max_, ratio_};
    for (QDoubleSpinBox* box : fields)
        connect(box, &QDoubleSpinBox::editingFinished, this, [this] {
            if (!loading_) writeBack();
        });
    setStep(nullptr, QString());
}

void SubstrateStepEditor::setStep(ProcessFlowDocument* doc, const QString& stepId) {
    doc_ = doc;
    step_id_ = stepId;
    const bool on = doc_ != nullptr && !step_id_.isEmpty();
    const std::initializer_list<QWidget*> fields = {length_cm_, background_doping_, h_min_, h_max_, ratio_};
    for (QWidget* w : fields) w->setEnabled(on);
    refresh();
}

void SubstrateStepEditor::refresh() {
    loading_ = true;
    if (doc_ && !step_id_.isEmpty()) {
        for (std::size_t i = 0; i < doc_->step_count(); ++i) {
            const ProcessStepData s = doc_->step(i);
            if (s.id == step_id_.toStdString()) {
                length_cm_->setValue(s.parameters.value("length_cm", 0.0));
                background_doping_->setValue(s.parameters.value("background_doping_cm3", 0.0));
                const auto& mesh = s.parameters.value("mesh", nlohmann::ordered_json::object());
                h_min_->setValue(mesh.value("h_min_cm", 0.0));
                h_max_->setValue(mesh.value("h_max_cm", 0.0));
                ratio_->setValue(mesh.value("ratio", 1.15));
                break;
            }
        }
    }
    loading_ = false;
}

void SubstrateStepEditor::writeBack() {
    if (!doc_ || step_id_.isEmpty()) return;
    nlohmann::ordered_json params;
    params["length_cm"] = length_cm_->value();
    params["background_doping_cm3"] = background_doping_->value();
    params["mesh"] = {{"h_min_cm", h_min_->value()}, {"h_max_cm", h_max_->value()}, {"ratio", ratio_->value()}};
    doc_->set_step_parameters(step_id_.toStdString(), params);
    emit stepEdited(step_id_);
}

}  // namespace tcad::desktop
