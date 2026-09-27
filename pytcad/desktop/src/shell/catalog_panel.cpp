#include "catalog_panel.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>

namespace tcad::desktop {

namespace {
constexpr int kRoleKey = Qt::UserRole + 1;

// study.templates' own "lo"/"hi" keys are PRESENT but JSON null when a
// TemplateParam has no bound (Python's `lo=None`/`hi=None`, carried
// through as an explicit null, not an absent key -- checked directly
// against templates_bindings.cpp before writing this) -- nlohmann::
// json::value(key, default) only substitutes the default for an ABSENT
// key, and throws a type_error trying to convert a present-but-null
// value to double. A real crash caught by this panel's own shell test
// on its first real run (the resistor template's own length_cm has no
// "hi"), not assumed safe just because .value() looks like a safe
// accessor.
double numberOr(const nlohmann::json& j, const char* key, double fallback) {
    if (!j.contains(key) || j.at(key).is_null()) return fallback;
    return j.at(key).get<double>();
}
}  // namespace

CatalogPanel::CatalogPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    auto* split = new QSplitter(Qt::Vertical, this);

    // -- template picker --------------------------------------------------------
    auto* templateBox = new QGroupBox(tr("Build from template"));
    auto* templateLayout = new QVBoxLayout(templateBox);
    template_box_ = new QComboBox(templateBox);
    template_box_->setObjectName("catalogTemplateBox");
    templateLayout->addWidget(template_box_);
    description_ = new QLabel(templateBox);
    description_->setObjectName("catalogTemplateDescription");
    description_->setWordWrap(true);
    templateLayout->addWidget(description_);
    param_container_ = new QWidget(templateBox);
    param_form_ = new QFormLayout(param_container_);
    templateLayout->addWidget(param_container_);
    build_button_ = new QPushButton(tr("Build"), templateBox);
    build_button_->setObjectName("catalogBuildButton");
    templateLayout->addWidget(build_button_);
    build_error_ = new QLabel(templateBox);
    build_error_->setObjectName("catalogBuildError");
    build_error_->setWordWrap(true);
    templateLayout->addWidget(build_error_);
    split->addWidget(templateBox);

    connect(template_box_, &QComboBox::currentIndexChanged, this, [this](int) { rebuildParamForm(); });
    connect(build_button_, &QPushButton::clicked, this, [this] {
        const int i = template_box_->currentIndex();
        if (i < 0 || i >= static_cast<int>(templates_.size())) return;
        const std::string id = templates_[static_cast<std::size_t>(i)].value("id", std::string());
        nlohmann::json values = nlohmann::json::object();
        for (int r = 0; r < param_form_->rowCount(); ++r) {
            auto* field = param_form_->itemAt(r, QFormLayout::FieldRole);
            if (!field || !field->widget()) continue;
            auto* box = qobject_cast<QDoubleSpinBox*>(field->widget());
            if (!box) continue;
            values[box->property("paramName").toString().toStdString()] = box->value();
        }
        emit buildTemplateRequested(QString::fromStdString(id), values);
    });

    // -- model catalog ------------------------------------------------------------
    auto* modelBox = new QGroupBox(tr("Physics models"));
    auto* modelLayout = new QVBoxLayout(modelBox);
    model_list_ = new QListWidget(modelBox);
    model_list_->setObjectName("catalogModelList");
    modelLayout->addWidget(model_list_, /*stretch=*/1);
    model_detail_ = new QPlainTextEdit(modelBox);
    model_detail_->setObjectName("catalogModelDetail");
    model_detail_->setReadOnly(true);
    modelLayout->addWidget(model_detail_);
    split->addWidget(modelBox);
    connect(model_list_, &QListWidget::itemChanged, this, &CatalogPanel::onModelItemChanged);
    connect(model_list_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem*, QListWidgetItem*) { onModelSelectionChanged(); });

    root->addWidget(split);
}

void CatalogPanel::setTemplates(const nlohmann::json& templates) {
    templates_ = templates;
    const QSignalBlocker block(template_box_);
    template_box_->clear();
    for (const auto& t : templates_)
        template_box_->addItem(QString::fromStdString(t.value("title", std::string())));
    rebuildParamForm();
}

void CatalogPanel::setBuildError(const QString& message) { build_error_->setText(message); }

void CatalogPanel::rebuildParamForm() {
    while (param_form_->rowCount() > 0) param_form_->removeRow(0);
    build_error_->clear();
    const int i = template_box_->currentIndex();
    if (i < 0 || i >= static_cast<int>(templates_.size())) {
        description_->clear();
        return;
    }
    const auto& t = templates_[static_cast<std::size_t>(i)];
    description_->setText(QString::fromStdString(t.value("description", std::string())));
    for (const auto& p : t.value("params", nlohmann::json::array())) {
        auto* box = new QDoubleSpinBox(param_container_);
        // templates.cpp's own TemplateParam::integer (core/include/tcad/
        // uicore/templates.hpp) rejects a non-whole value for such a
        // param at build time (e.g. mesh nx/ny) -- decimals(0) makes the
        // widget's own input validator refuse a fractional value up
        // front, instead of accepting it and only failing later inside
        // templates.build with a message the user can't trace back to
        // this field.
        const bool isInteger = p.value("integer", false);
        box->setDecimals(isInteger ? 0 : 9);
        if (isInteger) box->setSingleStep(1.0);
        box->setRange(numberOr(p, "lo", -1e30), numberOr(p, "hi", 1e30));
        box->setValue(p.value("default", 0.0));
        box->setProperty("paramName", QString::fromStdString(p.value("name", std::string())));
        const QString label = QString::fromStdString(p.value("label", std::string())) + " [" +
                              QString::fromStdString(p.value("unit", std::string())) + "]";
        param_form_->addRow(label, box);
    }
}

void CatalogPanel::setModelInfos(const nlohmann::json& infos) {
    model_infos_ = infos;
    const QSignalBlocker block(model_list_);
    model_list_->clear();
    for (const auto& info : model_infos_) {
        auto* item = new QListWidgetItem(QString::fromStdString(info.value("title", std::string())));
        item->setData(kRoleKey, QString::fromStdString(info.value("key", std::string())));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        const std::string key = info.value("key", std::string());
        item->setCheckState(model_config_.value(key, false) ? Qt::Checked : Qt::Unchecked);
        model_list_->addItem(item);
    }
}

void CatalogPanel::setModelConfig(const nlohmann::json& config) {
    model_config_ = config.is_object() ? config : nlohmann::json::object();
    const QSignalBlocker block(model_list_);
    for (int i = 0; i < model_list_->count(); ++i) {
        auto* item = model_list_->item(i);
        const std::string key = item->data(kRoleKey).toString().toStdString();
        item->setCheckState(model_config_.value(key, false) ? Qt::Checked : Qt::Unchecked);
    }
}

void CatalogPanel::onModelItemChanged(QListWidgetItem* item) {
    const std::string key = item->data(kRoleKey).toString().toStdString();
    model_config_[key] = item->checkState() == Qt::Checked;
    emit modelConfigChanged(model_config_);
}

void CatalogPanel::onModelSelectionChanged() {
    auto* item = model_list_->currentItem();
    if (!item) {
        model_detail_->clear();
        return;
    }
    const std::string key = item->data(kRoleKey).toString().toStdString();
    for (const auto& info : model_infos_) {
        if (info.value("key", std::string()) != key) continue;
        QString text = QString::fromStdString(info.value("applicability", std::string())) + "\n\n";
        text += tr("Equations:") + "\n";
        for (const auto& eq : info.value("equations", nlohmann::json::array()))
            text += "  " + QString::fromStdString(eq.get<std::string>()) + "\n";
        text += "\n" + tr("References:") + "\n";
        for (const auto& ref : info.value("references", nlohmann::json::array()))
            text += "  " + QString::fromStdString(ref.get<std::string>()) + "\n";
        const std::string limitations = info.value("limitations", std::string());
        if (!limitations.empty()) text += "\n" + tr("Limitations:") + " " + QString::fromStdString(limitations);
        model_detail_->setPlainText(text);
        return;
    }
}

}  // namespace tcad::desktop
