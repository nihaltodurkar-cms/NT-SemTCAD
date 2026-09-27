#include "anneal_step_editor.hpp"

#include <QDoubleSpinBox>
#include <QFormLayout>

namespace tcad::desktop {

AnnealStepEditor::AnnealStepEditor(QWidget* parent) : QWidget(parent) {
    auto* form = new QFormLayout(this);
    temperature_c_ = new QDoubleSpinBox(this);
    temperature_c_->setObjectName("annealTemperatureC");
    temperature_c_->setDecimals(2);
    temperature_c_->setRange(0.0, 5000.0);
    form->addRow(tr("Temperature [C]"), temperature_c_);

    time_s_ = new QDoubleSpinBox(this);
    time_s_->setObjectName("annealTimeS");
    time_s_->setDecimals(3);
    time_s_->setRange(0.0, 1e9);
    form->addRow(tr("Time [s]"), time_s_);

    connect(temperature_c_, &QDoubleSpinBox::editingFinished, this, [this] {
        if (!loading_) writeBack();
    });
    connect(time_s_, &QDoubleSpinBox::editingFinished, this, [this] {
        if (!loading_) writeBack();
    });
    setStep(nullptr, QString());
}

void AnnealStepEditor::setStep(ProcessFlowDocument* doc, const QString& stepId) {
    doc_ = doc;
    step_id_ = stepId;
    const bool on = doc_ != nullptr && !step_id_.isEmpty();
    temperature_c_->setEnabled(on);
    time_s_->setEnabled(on);
    refresh();
}

void AnnealStepEditor::refresh() {
    loading_ = true;
    if (doc_ && !step_id_.isEmpty()) {
        for (std::size_t i = 0; i < doc_->step_count(); ++i) {
            const ProcessStepData s = doc_->step(i);
            if (s.id == step_id_.toStdString()) {
                temperature_c_->setValue(s.parameters.value("temperature_C", 0.0));
                time_s_->setValue(s.parameters.value("time_s", 0.0));
                break;
            }
        }
    }
    loading_ = false;
}

void AnnealStepEditor::writeBack() {
    if (!doc_ || step_id_.isEmpty()) return;
    nlohmann::ordered_json params;
    params["temperature_C"] = temperature_c_->value();
    params["time_s"] = time_s_->value();
    doc_->set_step_parameters(step_id_.toStdString(), params);
    emit stepEdited(step_id_);
}

}  // namespace tcad::desktop
