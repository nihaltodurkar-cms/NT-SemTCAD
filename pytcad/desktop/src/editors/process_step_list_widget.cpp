#include "process_step_list_widget.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QListWidget>
#include <QPushButton>
#include <QUuid>
#include <QVBoxLayout>

#include <initializer_list>

namespace tcad::desktop {
namespace {
constexpr int kIdRole = Qt::UserRole + 1;

nlohmann::ordered_json default_parameters(const std::string& operation) {
    if (operation == "substrate")
        return {{"length_cm", 1e-3}, {"background_doping_cm3", 1e15},
                {"mesh", {{"h_min_cm", 1e-7}, {"h_max_cm", 1e-5}, {"ratio", 1.2}}}};
    if (operation == "implant")
        return {{"species", "B"}, {"energy_keV", 30}, {"dose_cm2", 1e13}, {"tilt_deg", 7}};
    if (operation == "anneal")
        return {{"temperature_C", 1000}, {"time_s", 30}};
    if (operation == "oxidize")
        return {{"temperature_C", 1000}, {"time_hours", 0.5}, {"ambient", "dry"}};
    return nlohmann::ordered_json::object();
}

QString label_for(const std::string& operation) {
    if (operation == "substrate") return "Substrate";
    if (operation == "implant") return "Implant";
    if (operation == "anneal") return "Anneal";
    if (operation == "oxidize") return "Oxidize";
    return QString::fromStdString(operation);
}

}  // namespace

ProcessStepListWidget::ProcessStepListWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    list_ = new QListWidget(this);
    list_->setObjectName("processStepList");
    layout->addWidget(list_);

    auto* grid = new QGridLayout();
    operation_ = new QComboBox(this);
    operation_->setObjectName("processOperationBox");
    operation_->addItems({"substrate", "implant", "anneal", "oxidize"});
    grid->addWidget(operation_, 0, 0);
    add_ = new QPushButton(tr("Add"), this);
    add_->setObjectName("addProcessStep");
    grid->addWidget(add_, 0, 1);
    duplicate_ = new QPushButton(tr("Duplicate"), this);
    duplicate_->setObjectName("duplicateProcessStep");
    grid->addWidget(duplicate_, 0, 2);
    remove_ = new QPushButton(tr("Remove"), this);
    remove_->setObjectName("removeProcessStep");
    grid->addWidget(remove_, 1, 0);
    up_ = new QPushButton(tr("Up"), this);
    up_->setObjectName("moveProcessStepUp");
    grid->addWidget(up_, 1, 1);
    down_ = new QPushButton(tr("Down"), this);
    down_->setObjectName("moveProcessStepDown");
    grid->addWidget(down_, 1, 2);
    enabled_ = new QCheckBox(tr("Enabled"), this);
    enabled_->setObjectName("processStepEnabled");
    grid->addWidget(enabled_, 2, 0);
    layout->addLayout(grid);

    connect(add_, &QPushButton::clicked, this, &ProcessStepListWidget::addStep);
    connect(remove_, &QPushButton::clicked, this, &ProcessStepListWidget::removeSelected);
    connect(up_, &QPushButton::clicked, this, [this] { moveSelected(-1); });
    connect(down_, &QPushButton::clicked, this, [this] { moveSelected(1); });
    connect(duplicate_, &QPushButton::clicked, this, &ProcessStepListWidget::duplicateSelected);
    connect(enabled_, &QCheckBox::toggled, this, [this](bool) { toggleSelectedEnabled(); });
    connect(list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem*, QListWidgetItem*) {
        onSelectionChanged();
    });

    setDocument(nullptr);
}

void ProcessStepListWidget::setDocument(ProcessFlowDocument* doc) {
    doc_ = doc;
    const bool on = doc_ != nullptr;
    const std::initializer_list<QWidget*> fields = {list_, operation_, add_, remove_,
                                                    up_, down_, duplicate_, enabled_};
    for (QWidget* w : fields) w->setEnabled(on);
    refresh();
}

void ProcessStepListWidget::refresh() {
    const QString keep = selectedStepId();
    list_->blockSignals(true);
    list_->clear();
    if (doc_) {
        for (std::size_t i = 0; i < doc_->step_count(); ++i) {
            const ProcessStepData s = doc_->step(i);
            auto* item = new QListWidgetItem(
                QString::fromStdString(s.name) + " (" + label_for(s.operation) + ")" +
                    (s.enabled ? "" : " [off]"),
                list_);
            item->setData(kIdRole, QString::fromStdString(s.id));
        }
    }
    list_->blockSignals(false);
    if (!keep.isEmpty()) selectStep(keep);
    onSelectionChanged();
}

QString ProcessStepListWidget::selectedStepId() const {
    const QListWidgetItem* item = list_->currentItem();
    return item ? item->data(kIdRole).toString() : QString();
}

void ProcessStepListWidget::selectStep(const QString& id) {
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->data(kIdRole).toString() == id) {
            list_->setCurrentRow(i);
            return;
        }
    }
    list_->setCurrentRow(-1);
}

void ProcessStepListWidget::onSelectionChanged() {
    const QString id = selectedStepId();
    enabled_->blockSignals(true);
    if (doc_ && !id.isEmpty()) {
        for (std::size_t i = 0; i < doc_->step_count(); ++i) {
            if (doc_->step(i).id == id.toStdString()) {
                enabled_->setChecked(doc_->step(i).enabled);
                break;
            }
        }
    }
    enabled_->blockSignals(false);
    emit selectionChanged(id);
}

void ProcessStepListWidget::addStep() {
    if (!doc_) return;
    const std::string operation = operation_->currentText().toStdString();
    ProcessStepData s;
    s.id = QUuid::createUuid().toString(QUuid::Id128).left(8).toStdString();
    s.name = label_for(operation).toStdString();
    s.operation = operation;
    s.enabled = true;
    s.parameters = default_parameters(operation);
    doc_->add_step(s);
    refresh();
    selectStep(QString::fromStdString(s.id));
    onSelectionChanged();
    emit stepsChanged();
}

void ProcessStepListWidget::removeSelected() {
    if (!doc_) return;
    const QString id = selectedStepId();
    if (id.isEmpty()) return;
    doc_->remove_step(id.toStdString());
    refresh();
    emit stepsChanged();
}

void ProcessStepListWidget::moveSelected(int offset) {
    if (!doc_) return;
    const QString id = selectedStepId();
    if (id.isEmpty()) return;
    doc_->move_step(id.toStdString(), offset);
    refresh();
    emit stepsChanged();
}

void ProcessStepListWidget::duplicateSelected() {
    if (!doc_) return;
    const QString id = selectedStepId();
    if (id.isEmpty()) return;
    const std::string new_id = doc_->duplicate_step(id.toStdString());
    refresh();
    if (!new_id.empty()) selectStep(QString::fromStdString(new_id));
    onSelectionChanged();
    emit stepsChanged();
}

void ProcessStepListWidget::toggleSelectedEnabled() {
    if (!doc_) return;
    const QString id = selectedStepId();
    if (id.isEmpty()) return;
    doc_->set_step_enabled(id.toStdString(), enabled_->isChecked());
    refresh();
    selectStep(id);
    emit stepsChanged();
}

}  // namespace tcad::desktop
