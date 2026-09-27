#include "gate_editor.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>

#include <initializer_list>

namespace tcad::desktop {
namespace {
constexpr double kCmPerNm = 1e-7;
}

GateEditor::GateEditor(QWidget* parent) : QWidget(parent) {
    form_ = new QFormLayout(this);
    name_ = new QLabel(this);
    name_->setObjectName("gateName");
    form_->addRow(tr("Name"), name_);

    tox_nm_ = new QDoubleSpinBox(this);
    tox_nm_->setObjectName("gateToxNm");
    tox_nm_->setDecimals(3);
    tox_nm_->setRange(0.0, 1e6);
    form_->addRow(tr("tox [nm]"), tox_nm_);

    voltage_ = new QDoubleSpinBox(this);
    voltage_->setObjectName("gateVoltage");
    voltage_->setDecimals(6);
    voltage_->setRange(-1e6, 1e6);
    form_->addRow(tr("V [V]"), voltage_);

    vfb_mode_ = new QComboBox(this);
    vfb_mode_->setObjectName("gateVfbMode");
    vfb_mode_->addItems({"computed", "manual"});
    form_->addRow(tr("Vfb mode"), vfb_mode_);

    vfb_manual_ = new QDoubleSpinBox(this);
    vfb_manual_->setObjectName("gateVfbManual");
    vfb_manual_->setDecimals(6);
    vfb_manual_->setRange(-1e6, 1e6);
    form_->addRow(tr("Vfb [V]"), vfb_manual_);

    wireSignals();
    setGate(nullptr, QString());
}

void GateEditor::wireSignals() {
    connect(tox_nm_, &QDoubleSpinBox::editingFinished, this, [this] {
        if (!loading_) writeBack();
    });
    connect(voltage_, &QDoubleSpinBox::editingFinished, this, [this] {
        if (!loading_) writeBack();
    });
    connect(vfb_manual_, &QDoubleSpinBox::editingFinished, this, [this] {
        if (!loading_) writeBack();
    });
    connect(vfb_mode_, &QComboBox::currentTextChanged, this, [this](const QString&) {
        if (loading_) return;
        updateVfbRowVisibility();
        writeBack();
    });
}

void GateEditor::setGate(StructureDocument* doc, const QString& gateId) {
    doc_ = doc;
    gate_id_ = gateId;
    const bool on = doc_ != nullptr && !gate_id_.isEmpty();
    const std::initializer_list<QWidget*> fields = {tox_nm_, voltage_, vfb_mode_, vfb_manual_};
    for (QWidget* w : fields) w->setEnabled(on);
    refresh();
}

void GateEditor::refresh() {
    loading_ = true;
    if (doc_ && !gate_id_.isEmpty()) {
        for (std::size_t i = 0; i < doc_->gate_count(); ++i) {
            const GateData g = doc_->gate(i);
            if (g.id == gate_id_.toStdString()) {
                name_->setText(QString::fromStdString(g.name));
                tox_nm_->setValue(g.tox_cm / kCmPerNm);
                voltage_->setValue(g.V);
                vfb_mode_->setCurrentText(QString::fromStdString(g.vfb_mode));
                vfb_manual_->setValue(g.vfb_manual.value_or(0.0));
                break;
            }
        }
    } else {
        name_->clear();
        tox_nm_->setValue(0.0);
        voltage_->setValue(0.0);
        vfb_manual_->setValue(0.0);
    }
    updateVfbRowVisibility();
    loading_ = false;
}

void GateEditor::updateVfbRowVisibility() {
    form_->setRowVisible(vfb_manual_, vfb_mode_->currentText() == "manual");
}

void GateEditor::writeBack() {
    if (!doc_ || gate_id_.isEmpty()) return;
    for (std::size_t i = 0; i < doc_->gate_count(); ++i) {
        GateData g = doc_->gate(i);
        if (g.id == gate_id_.toStdString()) {
            g.tox_cm = tox_nm_->value() * kCmPerNm;
            g.V = voltage_->value();
            g.vfb_mode = vfb_mode_->currentText().toStdString();
            // Always carried along, even in "computed" mode (harmless --
            // GateModel.vfb_manual is only READ when vfb_mode == "manual",
            // per its own docstring): simpler than QML's own None-vs-0.0
            // distinction for a value that is ignored either way, and
            // means an edit to any OTHER field round-trips whatever
            // value was already loaded rather than needing special
            // handling to preserve it untouched.
            g.vfb_manual = vfb_manual_->value();
            doc_->set_gate(gate_id_.toStdString(), g);
            emit gateEdited(gate_id_);
            return;
        }
    }
}

}  // namespace tcad::desktop
