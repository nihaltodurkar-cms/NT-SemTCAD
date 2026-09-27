// ImplantStepEditor: species, energy_keV, dose_cm2, tilt_deg, plus the
// M6 OPTIONAL per-region window x_range_cm (ImplantEditor.qml's own
// schema, read directly before writing this). A "Restrict to window"
// checkbox stands in for QML's empty-From/To-means-no-key convention
// (a `QDoubleSpinBox` has no natural empty state) -- unchecked removes
// the `x_range_cm` key entirely, matching "absent == whole domain ==
// byte-identical to pre-M6 flows" exactly, just through a checkbox
// instead of two blank text fields.
#pragma once

#include "document/process_document.hpp"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;

namespace tcad::desktop {

class ImplantStepEditor : public QWidget {
    Q_OBJECT

public:
    explicit ImplantStepEditor(QWidget* parent = nullptr);

    void setStep(ProcessFlowDocument* doc, const QString& stepId);
    void refresh();

signals:
    void stepEdited(const QString& stepId);

private:
    void writeBack();
    void updateWindowRowVisibility();

    ProcessFlowDocument* doc_ = nullptr;
    QString step_id_;
    bool loading_ = false;

    QFormLayout* form_ = nullptr;
    QComboBox* species_ = nullptr;
    QDoubleSpinBox* energy_kev_ = nullptr;
    QDoubleSpinBox* dose_cm2_ = nullptr;
    QDoubleSpinBox* tilt_deg_ = nullptr;
    QCheckBox* window_enabled_ = nullptr;
    QDoubleSpinBox* window_from_um_ = nullptr;
    QDoubleSpinBox* window_to_um_ = nullptr;
};

}  // namespace tcad::desktop
