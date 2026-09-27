#include "contact_list_widget.hpp"

#include <QListWidget>
#include <QVBoxLayout>

namespace tcad::desktop {
namespace {
constexpr int kIdRole = Qt::UserRole + 1;
}

ContactListWidget::ContactListWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    list_ = new QListWidget(this);
    list_->setObjectName("contactList");
    layout->addWidget(list_);
    connect(list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem*, QListWidgetItem*) {
        emit selectionChanged(selectedContactId());
    });
    setDocument(nullptr);
}

void ContactListWidget::setDocument(StructureDocument* doc) {
    doc_ = doc;
    list_->setEnabled(doc_ != nullptr);
    refresh();
}

void ContactListWidget::refresh() {
    const QString keep = selectedContactId();
    list_->blockSignals(true);
    list_->clear();
    if (doc_) {
        for (std::size_t i = 0; i < doc_->contact_count(); ++i) {
            const ContactData c = doc_->contact(i);
            auto* item = new QListWidgetItem(QString::fromStdString(c.name), list_);
            item->setData(kIdRole, QString::fromStdString(c.id));
        }
    }
    list_->blockSignals(false);
    if (!keep.isEmpty()) selectContact(keep);
    emit selectionChanged(selectedContactId());
}

QString ContactListWidget::selectedContactId() const {
    const QListWidgetItem* item = list_->currentItem();
    return item ? item->data(kIdRole).toString() : QString();
}

void ContactListWidget::selectContact(const QString& id) {
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->data(kIdRole).toString() == id) {
            list_->setCurrentRow(i);
            return;
        }
    }
    list_->setCurrentRow(-1);
}

}  // namespace tcad::desktop
