// SubstrateStepEditor: length_cm, background_doping_cm3, mesh.{h_min_cm,
// h_max_cm, ratio} (NATIVE-DESKTOP-PLAN.md section 20.4 S5 -- QML parity
// target: SubstrateEditor.qml, read directly before writing this).
#pragma once

#include "document/process_document.hpp"

#include <QWidget>

class QDoubleSpinBox;

namespace tcad::desktop {

class SubstrateStepEditor : public QWidget {
    Q_OBJECT

public:
    explicit SubstrateStepEditor(QWidget* parent = nullptr);

    void setStep(ProcessFlowDocument* doc, const QString& stepId);
    void refresh();

signals:
    void stepEdited(const QString& stepId);

private:
    void writeBack();

    ProcessFlowDocument* doc_ = nullptr;
    QString step_id_;
    bool loading_ = false;

    QDoubleSpinBox* length_cm_ = nullptr;
    QDoubleSpinBox* background_doping_ = nullptr;
    QDoubleSpinBox* h_min_ = nullptr;
    QDoubleSpinBox* h_max_ = nullptr;
    QDoubleSpinBox* ratio_ = nullptr;
};

}  // namespace tcad::desktop
