#include "gate_list_widget.hpp"

#include <QListWidget>
#include <QVBoxLayout>

namespace tcad::desktop {
namespace {
constexpr int kIdRole = Qt::UserRole + 1;
}

GateListWidget::GateListWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    list_ = new QListWidget(this);
    list_->setObjectName("gateList");
    layout->addWidget(list_);
    connect(list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem*, QListWidgetItem*) {
        emit selectionChanged(selectedGateId());
    });
    setDocument(nullptr);
}

void GateListWidget::setDocument(StructureDocument* doc) {
    doc_ = doc;
    list_->setEnabled(doc_ != nullptr);
    refresh();
}

void GateListWidget::refresh() {
    const QString keep = selectedGateId();
    list_->blockSignals(true);
    list_->clear();
    if (doc_) {
        for (std::size_t i = 0; i < doc_->gate_count(); ++i) {
            const GateData g = doc_->gate(i);
            auto* item = new QListWidgetItem(QString::fromStdString(g.name), list_);
            item->setData(kIdRole, QString::fromStdString(g.id));
        }
    }
    list_->blockSignals(false);
    if (!keep.isEmpty()) selectGate(keep);
    emit selectionChanged(selectedGateId());
}

QString GateListWidget::selectedGateId() const {
    const QListWidgetItem* item = list_->currentItem();
    return item ? item->data(kIdRole).toString() : QString();
}

void GateListWidget::selectGate(const QString& id) {
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->data(kIdRole).toString() == id) {
            list_->setCurrentRow(i);
            return;
        }
    }
    list_->setCurrentRow(-1);
}

}  // namespace tcad::desktop
