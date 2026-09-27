#include "oxidize_step_editor.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>

namespace tcad::desktop {

OxidizeStepEditor::OxidizeStepEditor(QWidget* parent) : QWidget(parent) {
    auto* form = new QFormLayout(this);
    temperature_c_ = new QDoubleSpinBox(this);
    temperature_c_->setObjectName("oxidizeTemperatureC");
    temperature_c_->setDecimals(2);
    temperature_c_->setRange(0.0, 5000.0);
    form->addRow(tr("Temperature [C]"), temperature_c_);

    time_hours_ = new QDoubleSpinBox(this);
    time_hours_->setObjectName("oxidizeTimeHours");
    time_hours_->setDecimals(4);
    time_hours_->setRange(0.0, 1e6);
    form->addRow(tr("Time [hours]"), time_hours_);

    ambient_ = new QComboBox(this);
    ambient_->setObjectName("oxidizeAmbient");
    ambient_->addItems({"dry", "wet"});
    form->addRow(tr("Ambient"), ambient_);

    connect(temperature_c_, &QDoubleSpinBox::editingFinished, this, [this] {
        if (!loading_) writeBack();
    });
    connect(time_hours_, &QDoubleSpinBox::editingFinished, this, [this] {
        if (!loading_) writeBack();
    });
    connect(ambient_, &QComboBox::currentTextChanged, this, [this](const QString&) {
        if (!loading_) writeBack();
    });
    setStep(nullptr, QString());
}

void OxidizeStepEditor::setStep(ProcessFlowDocument* doc, const QString& stepId) {
    doc_ = doc;
    step_id_ = stepId;
    const bool on = doc_ != nullptr && !step_id_.isEmpty();
    temperature_c_->setEnabled(on);
    time_hours_->setEnabled(on);
    ambient_->setEnabled(on);
    refresh();
}

void OxidizeStepEditor::refresh() {
    loading_ = true;
    if (doc_ && !step_id_.isEmpty()) {
        for (std::size_t i = 0; i < doc_->step_count(); ++i) {
            const ProcessStepData s = doc_->step(i);
            if (s.id == step_id_.toStdString()) {
                temperature_c_->setValue(s.parameters.value("temperature_C", 0.0));
                time_hours_->setValue(s.parameters.value("time_hours", 0.0));
                ambient_->setCurrentText(QString::fromStdString(s.parameters.value("ambient", std::string("dry"))));
                break;
            }
        }
    }
    loading_ = false;
}

void OxidizeStepEditor::writeBack() {
    if (!doc_ || step_id_.isEmpty()) return;
    nlohmann::ordered_json params;
    params["temperature_C"] = temperature_c_->value();
    params["time_hours"] = time_hours_->value();
    params["ambient"] = ambient_->currentText().toStdString();
    doc_->set_step_parameters(step_id_.toStdString(), params);
    emit stepEdited(step_id_);
}

}  // namespace tcad::desktop
