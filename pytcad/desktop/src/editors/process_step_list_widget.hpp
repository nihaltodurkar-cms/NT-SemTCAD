// ProcessStepListWidget: add/remove/reorder/duplicate/toggle process
// steps (NATIVE-DESKTOP-PLAN.md section 20.4 S5 -- QML parity target:
// ProcessPanel.qml). The four fixed operations and their default
// parameter dicts match `ProcessPanel.qml::_defaultParams` exactly
// (checked directly, not re-derived):
//   substrate: {length_cm: 1e-3, background_doping_cm3: 1e15,
//               mesh: {h_min_cm: 1e-7, h_max_cm: 1e-5, ratio: 1.2}}
//   implant:   {species: "B", energy_keV: 30, dose_cm2: 1e13, tilt_deg: 7}
//   anneal:    {temperature_C: 1000, time_s: 30}
//   oxidize:   {temperature_C: 1000, time_hours: 0.5, ambient: "dry"}
// A new step's id is generated the same 8-lowercase-hex-character shape
// as `RegionListWidget::addRegion()` (QUuid here -- this widget links
// Qt Widgets, unlike `ProcessFlowDocument` itself).
#pragma once

#include "document/process_document.hpp"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QListWidget;
class QPushButton;

namespace tcad::desktop {

class ProcessStepListWidget : public QWidget {
    Q_OBJECT

public:
    explicit ProcessStepListWidget(QWidget* parent = nullptr);

    void setDocument(ProcessFlowDocument* doc);
    ProcessFlowDocument* document() const { return doc_; }
    void refresh();

    QString selectedStepId() const;
    void selectStep(const QString& id);

signals:
    void selectionChanged(const QString& stepId);
    void stepsChanged();  // add/remove/reorder/duplicate/toggle happened

private:
    void addStep();
    void removeSelected();
    void moveSelected(int offset);
    void duplicateSelected();
    void toggleSelectedEnabled();
    void onSelectionChanged();

    ProcessFlowDocument* doc_ = nullptr;
    QListWidget* list_ = nullptr;
    QComboBox* operation_ = nullptr;
    QPushButton* add_ = nullptr;
    QPushButton* remove_ = nullptr;
    QPushButton* up_ = nullptr;
    QPushButton* down_ = nullptr;
    QPushButton* duplicate_ = nullptr;
    QCheckBox* enabled_ = nullptr;
};

}  // namespace tcad::desktop
