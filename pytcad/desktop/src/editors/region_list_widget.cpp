#include "region_list_widget.hpp"

#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QUuid>
#include <QVBoxLayout>

#include <initializer_list>

namespace tcad::desktop {
namespace {
constexpr int kIdRole = Qt::UserRole + 1;
}

RegionListWidget::RegionListWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    list_ = new QListWidget(this);
    list_->setObjectName("regionList");
    layout->addWidget(list_);

    auto* buttons = new QHBoxLayout();
    add_ = new QPushButton(tr("Add"), this);
    add_->setObjectName("addRegion");
    remove_ = new QPushButton(tr("Remove"), this);
    remove_->setObjectName("removeRegion");
    up_ = new QPushButton(tr("Up"), this);
    up_->setObjectName("moveRegionUp");
    down_ = new QPushButton(tr("Down"), this);
    down_->setObjectName("moveRegionDown");
    const std::initializer_list<QPushButton*> allButtons = {add_, remove_, up_, down_};
    for (QPushButton* b : allButtons) buttons->addWidget(b);
    layout->addLayout(buttons);

    connect(add_, &QPushButton::clicked, this, &RegionListWidget::addRegion);
    connect(remove_, &QPushButton::clicked, this, &RegionListWidget::removeSelected);
    connect(up_, &QPushButton::clicked, this, [this] { moveSelected(-1); });
    connect(down_, &QPushButton::clicked, this, [this] { moveSelected(1); });
    connect(list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem*, QListWidgetItem*) {
        onSelectionChanged();
    });

    setDocument(nullptr);
}

void RegionListWidget::setDocument(StructureDocument* doc) {
    doc_ = doc;
    const bool on = doc_ != nullptr;
    const std::initializer_list<QWidget*> fields = {list_, add_, remove_, up_, down_};
    for (QWidget* w : fields) w->setEnabled(on);
    refresh();
}

void RegionListWidget::refresh() {
    const QString keep = selectedRegionId();
    list_->blockSignals(true);
    list_->clear();
    if (doc_) {
        for (std::size_t i = 0; i < doc_->region_count(); ++i) {
            const RegionData r = doc_->region(i);
            auto* item = new QListWidgetItem(QString::fromStdString(r.name), list_);
            item->setData(kIdRole, QString::fromStdString(r.id));
        }
    }
    list_->blockSignals(false);
    if (!keep.isEmpty())
        selectRegion(keep);
    onSelectionChanged();  // fires selectionChanged even if the id could not be restored
}

QString RegionListWidget::selectedRegionId() const {
    const QListWidgetItem* item = list_->currentItem();
    return item ? item->data(kIdRole).toString() : QString();
}

void RegionListWidget::selectRegion(const QString& id) {
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->data(kIdRole).toString() == id) {
            list_->setCurrentRow(i);
            return;
        }
    }
    list_->setCurrentRow(-1);
}

void RegionListWidget::onSelectionChanged() { emit selectionChanged(selectedRegionId()); }

void RegionListWidget::addRegion() {
    if (!doc_) return;
    RegionData r;
    r.id = QUuid::createUuid().toString(QUuid::Id128).left(8).toStdString();
    r.name = "New Region";
    r.x_min = 0.0;
    r.x_max = doc_->width_cm();
    r.y_min = 0.0;
    r.y_max = doc_->height_cm();
    r.net_doping_cm3 = 1e15;
    doc_->add_region(r);
    refresh();
    selectRegion(QString::fromStdString(r.id));
    onSelectionChanged();
    emit regionsChanged();
}

void RegionListWidget::removeSelected() {
    if (!doc_) return;
    const QString id = selectedRegionId();
    if (id.isEmpty()) return;
    doc_->remove_region(id.toStdString());
    refresh();
    emit regionsChanged();
}

void RegionListWidget::moveSelected(int offset) {
    if (!doc_) return;
    const QString id = selectedRegionId();
    if (id.isEmpty()) return;
    doc_->move_region(id.toStdString(), offset);
    refresh();
    emit regionsChanged();
}

}  // namespace tcad::desktop
