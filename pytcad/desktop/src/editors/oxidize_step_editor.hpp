// OxidizeStepEditor: temperature_C, time_hours, ambient (dry/wet) --
// OxidizeEditor.qml's own fixed schema, and its own bookkeeping-only
// warning applies unchanged: this backend never touches the wafer's
// x-axis or doping profile from an oxidize step.
#pragma once

#include "document/process_document.hpp"

#include <QWidget>

class QComboBox;
class QDoubleSpinBox;

namespace tcad::desktop {

class OxidizeStepEditor : public QWidget {
    Q_OBJECT

public:
    explicit OxidizeStepEditor(QWidget* parent = nullptr);

    void setStep(ProcessFlowDocument* doc, const QString& stepId);
    void refresh();

signals:
    void stepEdited(const QString& stepId);

private:
    void writeBack();

    ProcessFlowDocument* doc_ = nullptr;
    QString step_id_;
    bool loading_ = false;

    QDoubleSpinBox* temperature_c_ = nullptr;
    QDoubleSpinBox* time_hours_ = nullptr;
    QComboBox* ambient_ = nullptr;
};

}  // namespace tcad::desktop
