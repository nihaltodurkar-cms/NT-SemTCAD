// The Study dock (NATIVE-DESKTOP-PLAN.md 17.16, P3-S7): a template, its
// base parameters and splits (tables, not QML's free text -- decision 5),
// a pool size, Build/Run/Cancel, the rows table (double-click opens a
// done row's result) and, for a 2-axis study, the matrix grid.
//
// The matrix value is max(field) over a done row's result, read directly
// from its npz file with the same reader the main view uses (ResultModel)
// -- a contract test checks it against StudyController.matrixCells on the
// same files. A row not done is an empty cell, never 0 (gate G-PARTIAL).
#pragma once

#include "run/study_controller.hpp"

#include <QWidget>

#include <optional>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTableWidgetItem;

namespace tcad::desktop {

// max(scalar field) over a "done" row's result file; nullopt on any
// failure to read it (a stale/removed file, an unknown field).
std::optional<double> studyMatrixCellValue(const QString& resultPath, const std::string& field);

class StudyPanel : public QWidget {
    Q_OBJECT

public:
    explicit StudyPanel(StudyController* controller, QWidget* parent = nullptr);

    QComboBox* templateCombo() const { return template_combo_; }
    QTableWidget* baseTable() const { return base_table_; }
    QTableWidget* splitsTable() const { return splits_table_; }
    QSpinBox* poolSpin() const { return pool_spin_; }
    QPushButton* buildButton() const { return build_; }
    QPushButton* runButton() const { return run_; }
    QPushButton* cancelButton() const { return cancel_; }
    QTableWidget* rowsTable() const { return rows_table_; }
    QLabel* statusLabel() const { return status_; }
    QComboBox* fieldCombo() const { return field_combo_; }
    QTableWidget* matrixTable() const { return matrix_table_; }
    // P3-S8: remote hosts (a Study-only feature, decision 8 of §17.7).
    // Comma-separated, same "type what you mean" style as QML's.
    QLineEdit* remoteHostsField() const { return remote_hosts_; }
    void applyRemoteHosts();  // reads remoteHostsField(), calls setRemoteHosts

    // Add one split row (Parameter, Levels "v1, v2, ..."), for tests.
    void addSplitRow(const QString& param, const QString& levels);
    void removeSplitRow(int row);

    // Read the base/splits tables as buildRows() wants them.
    nlohmann::json readBase() const;
    nlohmann::json readSplits(QString* error = nullptr) const;

signals:
    void inputRejected(const QString& title, const QString& detail);
    void openRequested(const QString& path);

protected:
    // Shown (its tab brought to the front): request study.templates. A
    // background tab starts no Python (the lazy-backend rule, kept).
    void showEvent(QShowEvent* event) override;

private:
    void ensureTemplates();
    void onTemplatesChanged();
    void onTemplateChanged();
    void onBuildClicked();
    void onRunClicked();
    void onRowsChanged();
    void onRowDoubleClicked(int row, int column);
    void rebuildRowsTable();
    void rebuildMatrix();
    void updateEnabled();
    const nlohmann::json* currentTemplate() const;

    StudyController* ctl_;
    QComboBox* template_combo_ = nullptr;
    QTableWidget* base_table_ = nullptr;
    QTableWidget* splits_table_ = nullptr;
    QPushButton* add_split_ = nullptr;
    QPushButton* remove_split_ = nullptr;
    QSpinBox* pool_spin_ = nullptr;
    QPushButton* build_ = nullptr;
    QPushButton* run_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QTableWidget* rows_table_ = nullptr;
    QLabel* status_ = nullptr;
    QComboBox* field_combo_ = nullptr;
    QTableWidget* matrix_table_ = nullptr;
    QLineEdit* remote_hosts_ = nullptr;
    QStringList axis_names_;
    bool templates_requested_ = false;
};

}  // namespace tcad::desktop
