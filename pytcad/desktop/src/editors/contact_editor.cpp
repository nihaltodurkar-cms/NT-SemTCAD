#include "contact_editor.hpp"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>

namespace tcad::desktop {

ContactEditor::ContactEditor(QWidget* parent) : QWidget(parent) {
    auto* form = new QFormLayout(this);
    name_ = new QLabel(this);
    name_->setObjectName("contactName");
    form->addRow(tr("Name"), name_);
    edge_ = new QLabel(this);
    edge_->setObjectName("contactEdge");
    form->addRow(tr("Edge"), edge_);
    voltage_ = new QDoubleSpinBox(this);
    voltage_->setObjectName("contactVoltage");
    voltage_->setDecimals(6);
    voltage_->setRange(-1e6, 1e6);
    form->addRow(tr("V [V]"), voltage_);

    connect(voltage_, &QDoubleSpinBox::editingFinished, this, [this] {
        if (!loading_) writeBack();
    });
    setContact(nullptr, QString());
}

void ContactEditor::setContact(StructureDocument* doc, const QString& contactId) {
    doc_ = doc;
    contact_id_ = contactId;
    const bool on = doc_ != nullptr && !contact_id_.isEmpty();
    voltage_->setEnabled(on);
    refresh();
}

void ContactEditor::refresh() {
    loading_ = true;
    if (doc_ && !contact_id_.isEmpty()) {
        for (std::size_t i = 0; i < doc_->contact_count(); ++i) {
            const ContactData c = doc_->contact(i);
            if (c.id == contact_id_.toStdString()) {
                name_->setText(QString::fromStdString(c.name));
                edge_->setText(QString::fromStdString(c.boundary.edge));
                voltage_->setValue(c.V);
                break;
            }
        }
    } else {
        name_->clear();
        edge_->clear();
        voltage_->setValue(0.0);
    }
    loading_ = false;
}

void ContactEditor::writeBack() {
    if (!doc_ || contact_id_.isEmpty()) return;
    for (std::size_t i = 0; i < doc_->contact_count(); ++i) {
        ContactData c = doc_->contact(i);
        if (c.id == contact_id_.toStdString()) {
            c.V = voltage_->value();
            doc_->set_contact(contact_id_.toStdString(), c);
            emit contactEdited(contact_id_);
            return;
        }
    }
}

}  // namespace tcad::desktop
