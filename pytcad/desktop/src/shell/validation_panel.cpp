#include "validation_panel.hpp"

#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

namespace tcad::desktop {

ValidationPanel::ValidationPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    status_ = new QLabel(this);
    status_->setObjectName("validationStatus");
    layout->addWidget(status_);
    list_ = new QListWidget(this);
    list_->setObjectName("validationList");
    layout->addWidget(list_);
    setErrors({});
}

void ValidationPanel::setErrors(const std::vector<std::string>& messages) {
    errors_ = messages;
    list_->clear();
    for (const auto& m : errors_) list_->addItem(QString("\xE2\x9C\x95 ") + QString::fromStdString(m));
    status_->setText(statusText());
}

QString ValidationPanel::statusText() const {
    return errors_.empty() ? QStringLiteral("OK") : QStringLiteral("FAILED");
}

}  // namespace tcad::desktop
