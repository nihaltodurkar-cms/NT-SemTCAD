// DopingEditor: the selected region's full field set (NATIVE-DESKTOP-
// PLAN.md section 20.4 S3 -- QML parity target: DopingEditor.qml).
// Material is read-only, same "read-only in v0.2" rule StructurePanel.qml
// states directly (Device2D takes exactly one material for the whole
// domain). z_min/z_max only apply (and are only shown, via
// QFormLayout::setRowVisible) when the structure has a depth_cm -- the
// same both-or-neither convention RegionSpec.z_min/z_max already uses.
// The four profile_* fields and profile_high_side are shown only when
// doping_profile != "uniform" (RegionSpec's own documented precondition).
#pragma once

#include "document/structure_document.hpp"

#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;

namespace tcad::desktop {

class DopingEditor : public QWidget {
    Q_OBJECT

public:
    explicit DopingEditor(QWidget* parent = nullptr);

    // Non-owning. An empty `regionId` (or doc == nullptr) clears/disables the form.
    void setRegion(StructureDocument* doc, const QString& regionId);
    QString regionId() const { return region_id_; }
    // Re-reads the current region's fields (e.g. after the canvas
    // dragged it) without changing which region is being edited.
    void refresh();

signals:
    void regionEdited(const QString& regionId);

private:
    void wireSignals();
    void writeBack();
    void updateRowVisibility();
    void setFieldsEnabled(bool on);

    StructureDocument* doc_ = nullptr;
    QString region_id_;
    bool loading_ = false;
    bool is3d_ = false;

    QFormLayout* form_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLabel* material_ = nullptr;
    QDoubleSpinBox* x_min_ = nullptr;
    QDoubleSpinBox* x_max_ = nullptr;
    QDoubleSpinBox* y_min_ = nullptr;
    QDoubleSpinBox* y_max_ = nullptr;
    QDoubleSpinBox* z_min_ = nullptr;
    QDoubleSpinBox* z_max_ = nullptr;
    QDoubleSpinBox* net_doping_ = nullptr;
    QComboBox* profile_ = nullptr;
    QDoubleSpinBox* peak_ = nullptr;
    QDoubleSpinBox* sigma_y_ = nullptr;
    QDoubleSpinBox* sigma_lat_ = nullptr;
    QDoubleSpinBox* edge_x_ = nullptr;
    QComboBox* high_side_ = nullptr;
};

}  // namespace tcad::desktop
