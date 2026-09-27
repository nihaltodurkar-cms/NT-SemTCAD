// CatalogPanel (P4 section 24, NATIVE-DESKTOP-PLAN.md): BuildPanel's
// third tab -- the template picker (build a device from a parametric
// template, same "adopt into the one shared document, no second
// device-editing path" rule the old BuilderController followed) and
// the Physics Lab's model catalog (per-model enable/disable checkboxes
// with the documented equations/references/limitations on selection).
//
// Qt Widgets only; MainWindow owns the BackendClient and feeds this
// panel plain JSON (study.templates' own array, catalog.list/describe's
// own shapes) -- this panel never calls the backend itself, the same
// division BuildPanel already keeps for validation.
#pragma once

#include <nlohmann/json.hpp>

#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;

namespace tcad::desktop {

class CatalogPanel : public QWidget {
    Q_OBJECT

public:
    explicit CatalogPanel(QWidget* parent = nullptr);

    // study.templates' own array: [{"id","title","description","params":
    // [{"name","label","unit","default","lo","hi","integer"}]}].
    void setTemplates(const nlohmann::json& templates);
    void setBuildError(const QString& message);

    // catalog.list()-ordered array of catalog.describe()'s own shape.
    void setModelInfos(const nlohmann::json& infos);
    // {model_key: bool} -- catalog.default_config()'s or a loaded
    // project's own carried-opaque "models" value (null -> defaults).
    void setModelConfig(const nlohmann::json& config);
    nlohmann::json modelConfig() const { return model_config_; }

signals:
    // `values`: {param_name: number}, every current field value.
    void buildTemplateRequested(const QString& id, const nlohmann::json& values);
    void modelConfigChanged(const nlohmann::json& config);

private:
    void rebuildParamForm();
    void onModelItemChanged(QListWidgetItem* item);
    void onModelSelectionChanged();

    nlohmann::json templates_ = nlohmann::json::array();
    nlohmann::json model_infos_ = nlohmann::json::array();
    nlohmann::json model_config_ = nlohmann::json::object();

    QComboBox* template_box_ = nullptr;
    QFormLayout* param_form_ = nullptr;
    QWidget* param_container_ = nullptr;
    QLabel* description_ = nullptr;
    QPushButton* build_button_ = nullptr;
    QLabel* build_error_ = nullptr;

    QListWidget* model_list_ = nullptr;
    QPlainTextEdit* model_detail_ = nullptr;
};

}  // namespace tcad::desktop
