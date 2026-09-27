// MeshEditor: nx/ny/grading/h_min/h_max/ratio/nz fields over a
// MeshDocument (NATIVE-DESKTOP-PLAN.md section 20.4 S2 -- QML parity
// target: MeshEditor.qml's own field list). x_focus/y_focus/z_focus are
// not exposed here -- StructureDocument/MeshDocument's own lossless
// contract (P4 S1) means they still survive untouched on disk even
// though this editor never shows them; a focus-point editor is future
// work, not a P4 S2 gap.
//
// nz uses StructurePanel.qml's own convention for "no 3D": 0 means
// MeshDocument::set_nz(std::nullopt) (2D), matching how that panel
// already treats depth_cm == 0 as "clear back to 2D".
#pragma once

#include "document/mesh_document.hpp"

#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;

namespace tcad::desktop {

class MeshEditor : public QWidget {
    Q_OBJECT

public:
    explicit MeshEditor(QWidget* parent = nullptr);

    // Non-owning; caller keeps the document alive. Null disables every field.
    void setDocument(MeshDocument* doc);
    MeshDocument* document() const { return doc_; }
    // Re-reads the document into the fields (e.g. after an edit made
    // through another editor, such as the canvas changing nz).
    void refresh();

signals:
    void meshEdited();

private:
    void wireSignals();
    void setFieldsEnabled(bool on);

    MeshDocument* doc_ = nullptr;
    bool loading_ = false;  // true while refresh() is setting field values, to
                            // suppress the edit signal re-writing what was just read
    QSpinBox* nx_ = nullptr;
    QSpinBox* ny_ = nullptr;
    QComboBox* grading_ = nullptr;
    QDoubleSpinBox* h_min_ = nullptr;
    QDoubleSpinBox* h_max_ = nullptr;
    QDoubleSpinBox* ratio_ = nullptr;
    QSpinBox* nz_ = nullptr;
};

}  // namespace tcad::desktop
