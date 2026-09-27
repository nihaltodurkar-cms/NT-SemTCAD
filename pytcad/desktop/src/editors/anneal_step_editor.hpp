// AnnealStepEditor: temperature_C, time_s only -- no species field
// (AnnealEditor.qml's own comment: anneal diffuses whatever dopant is
// already present, it doesn't introduce a new one).
#pragma once

#include "document/process_document.hpp"

#include <QWidget>

class QDoubleSpinBox;

namespace tcad::desktop {

class AnnealStepEditor : public QWidget {
    Q_OBJECT

public:
    explicit AnnealStepEditor(QWidget* parent = nullptr);

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
    QDoubleSpinBox* time_s_ = nullptr;
};

}  // namespace tcad::desktop
