// GateEditor: tox, voltage, Vfb mode/value (NATIVE-DESKTOP-PLAN.md
// section 20.4 S4 -- QML parity target: GateEditor.qml, read directly
// before writing this). Name is read-only; edge, gate_type are not
// edited by the reference GUI either, so neither is added here.
// tox is shown and entered in nm, matching GateEditor.qml's own
// `gateData.tox * 1e7` / `/1e7` conversion exactly, not left in cm.
#pragma once

#include "document/structure_document.hpp"

#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;

namespace tcad::desktop {

class GateEditor : public QWidget {
    Q_OBJECT

public:
    explicit GateEditor(QWidget* parent = nullptr);

    // Non-owning. An empty `gateId` (or doc == nullptr) clears/disables the form.
    void setGate(StructureDocument* doc, const QString& gateId);
    QString gateId() const { return gate_id_; }
    void refresh();

signals:
    void gateEdited(const QString& gateId);

private:
    void wireSignals();
    void writeBack();
    void updateVfbRowVisibility();

    StructureDocument* doc_ = nullptr;
    QString gate_id_;
    bool loading_ = false;

    QFormLayout* form_ = nullptr;
    QLabel* name_ = nullptr;
    QDoubleSpinBox* tox_nm_ = nullptr;
    QDoubleSpinBox* voltage_ = nullptr;
    QComboBox* vfb_mode_ = nullptr;
    QDoubleSpinBox* vfb_manual_ = nullptr;
};

}  // namespace tcad::desktop
