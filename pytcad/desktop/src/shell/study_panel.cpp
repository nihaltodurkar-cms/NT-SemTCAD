#include "study_panel.hpp"

#include "data/npz.hpp"
#include "data/result_model.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <thread>

namespace tcad::desktop {

std::optional<double> studyMatrixCellValue(const QString& resultPath, const std::string& field) {
    try {
        NpzFile npz = NpzFile::open(resultPath.toStdWString());
        ResultModel model = ResultModel::from_npz(npz);
        const auto values = model.scalar(field).values;
        if (values.empty()) return std::nullopt;
        return *std::max_element(values.begin(), values.end());
    } catch (...) {
        return std::nullopt;
    }
}

namespace {
QString statusText(StudyController::RowStatus s) {
    switch (s) {
        case StudyController::RowStatus::BuildError: return QObject::tr("build_error");
        case StudyController::RowStatus::Pending: return QObject::tr("pending");
        case StudyController::RowStatus::Running: return QObject::tr("running");
        case StudyController::RowStatus::Done: return QObject::tr("done");
        case StudyController::RowStatus::Failed: return QObject::tr("failed");
    }
    return {};
}

QString paramText(const nlohmann::json& v) {
    if (v.is_number()) return QLocale::c().toString(v.get<double>(), 'g', QLocale::FloatingPointShortest);
    return QString::fromStdString(v.dump());
}
}  // namespace

StudyPanel::StudyPanel(StudyController* controller, QWidget* parent) : QWidget(parent), ctl_(controller) {
    auto* layout = new QVBoxLayout(this);

    template_combo_ = new QComboBox(this);
    template_combo_->setObjectName("StudyTemplate");
    layout->addWidget(new QLabel(tr("Template"), this));
    layout->addWidget(template_combo_);

    layout->addWidget(new QLabel(tr("Base parameters"), this));
    base_table_ = new QTableWidget(0, 2, this);
    base_table_->setObjectName("StudyBase");
    base_table_->setHorizontalHeaderLabels({tr("Parameter"), tr("Value")});
    base_table_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(base_table_);

    layout->addWidget(new QLabel(tr("Splits"), this));
    splits_table_ = new QTableWidget(0, 2, this);
    splits_table_->setObjectName("StudySplits");
    splits_table_->setHorizontalHeaderLabels({tr("Parameter"), tr("Levels")});
    splits_table_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(splits_table_);

    auto* split_btns = new QHBoxLayout();
    add_split_ = new QPushButton(tr("+"), this);
    add_split_->setObjectName("StudyAddSplit");
    remove_split_ = new QPushButton(tr("-"), this);
    remove_split_->setObjectName("StudyRemoveSplit");
    split_btns->addWidget(add_split_);
    split_btns->addWidget(remove_split_);
    split_btns->addStretch();
    layout->addLayout(split_btns);

    layout->addWidget(new QLabel(tr("Remote hosts (comma-separated; empty: local)"), this));
    remote_hosts_ = new QLineEdit(this);
    remote_hosts_->setObjectName("StudyRemoteHosts");
    layout->addWidget(remote_hosts_);

    auto* run_row = new QHBoxLayout();
    run_row->addWidget(new QLabel(tr("Pool"), this));
    pool_spin_ = new QSpinBox(this);
    pool_spin_->setObjectName("StudyPool");
    pool_spin_->setRange(1, 6);
    pool_spin_->setValue(std::max<int>(1, std::min<int>(6, static_cast<int>(std::thread::hardware_concurrency()))));
    run_row->addWidget(pool_spin_);
    build_ = new QPushButton(tr("Build rows"), this);
    build_->setObjectName("StudyBuild");
    run_ = new QPushButton(tr("Run"), this);
    run_->setObjectName("StudyRun");
    cancel_ = new QPushButton(tr("Cancel all"), this);
    cancel_->setObjectName("StudyCancel");
    run_row->addWidget(build_);
    run_row->addWidget(run_);
    run_row->addWidget(cancel_);
    run_row->addStretch();
    layout->addLayout(run_row);

    status_ = new QLabel(this);
    status_->setObjectName("StudyStatus");
    layout->addWidget(status_);

    rows_table_ = new QTableWidget(0, 0, this);
    rows_table_->setObjectName("StudyRows");
    rows_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    rows_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(rows_table_);

    auto* field_row = new QHBoxLayout();
    field_row->addWidget(new QLabel(tr("Matrix field"), this));
    field_combo_ = new QComboBox(this);
    field_combo_->setObjectName("StudyField");
    field_row->addWidget(field_combo_);
    field_row->addStretch();
    layout->addLayout(field_row);

    matrix_table_ = new QTableWidget(0, 0, this);
    matrix_table_->setObjectName("StudyMatrix");
    matrix_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(matrix_table_);

    connect(add_split_, &QPushButton::clicked, this, [this] { addSplitRow(QString(), QString()); });
    connect(remove_split_, &QPushButton::clicked, this, [this] {
        const int row = splits_table_->currentRow();
        if (row >= 0) removeSplitRow(row);
    });
    connect(template_combo_, &QComboBox::currentIndexChanged, this, &StudyPanel::onTemplateChanged);
    connect(build_, &QPushButton::clicked, this, &StudyPanel::onBuildClicked);
    connect(remote_hosts_, &QLineEdit::editingFinished, this, &StudyPanel::applyRemoteHosts);
    connect(run_, &QPushButton::clicked, this, &StudyPanel::onRunClicked);
    connect(cancel_, &QPushButton::clicked, ctl_, &StudyController::cancelAll);
    connect(rows_table_, &QTableWidget::cellDoubleClicked, this, &StudyPanel::onRowDoubleClicked);
    connect(field_combo_, &QComboBox::currentIndexChanged, this, &StudyPanel::rebuildMatrix);

    connect(ctl_, &StudyController::templatesChanged, this, &StudyPanel::onTemplatesChanged);
    connect(ctl_, &StudyController::rowsChanged, this, &StudyPanel::onRowsChanged);
    connect(ctl_, &StudyController::busyChanged, this, [this](bool) { updateEnabled(); });

    updateEnabled();
}

void StudyPanel::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    ensureTemplates();
}

void StudyPanel::ensureTemplates() {
    if (templates_requested_) return;
    templates_requested_ = true;
    ctl_->requestTemplates();
}

void StudyPanel::addSplitRow(const QString& param, const QString& levels) {
    const int row = splits_table_->rowCount();
    splits_table_->insertRow(row);
    splits_table_->setItem(row, 0, new QTableWidgetItem(param));
    splits_table_->setItem(row, 1, new QTableWidgetItem(levels));
}

void StudyPanel::removeSplitRow(int row) {
    if (row >= 0 && row < splits_table_->rowCount()) splits_table_->removeRow(row);
}

const nlohmann::json* StudyPanel::currentTemplate() const {
    const int idx = template_combo_->currentIndex();
    if (idx < 0 || !ctl_->templates().is_array() || idx >= static_cast<int>(ctl_->templates().size())) return nullptr;
    return &ctl_->templates()[static_cast<std::size_t>(idx)];
}

void StudyPanel::onTemplatesChanged() {
    template_combo_->clear();
    if (!ctl_->templates().is_array()) return;
    for (const auto& t : ctl_->templates())
        template_combo_->addItem(QString::fromStdString(t.value("title", t.value("id", std::string()))));
    if (template_combo_->count() > 0) onTemplateChanged();
}

void StudyPanel::onTemplateChanged() {
    base_table_->setRowCount(0);
    const nlohmann::json* t = currentTemplate();
    if (!t) return;
    const auto& params = (*t)["params"];
    base_table_->setRowCount(static_cast<int>(params.size()));
    int row = 0;
    for (const auto& p : params) {
        auto* name_item = new QTableWidgetItem(QString::fromStdString(p.value("name", std::string())));
        name_item->setFlags(name_item->flags() & ~Qt::ItemIsEditable);
        name_item->setToolTip(QString("%1 [%2]").arg(QString::fromStdString(p.value("label", std::string())),
                                                      QString::fromStdString(p.value("unit", std::string()))));
        base_table_->setItem(row, 0, name_item);
        base_table_->setItem(row, 1, new QTableWidgetItem(paramText(p["default"])));
        ++row;
    }
}

nlohmann::json StudyPanel::readBase() const {
    nlohmann::json out = nlohmann::json::object();
    for (int row = 0; row < base_table_->rowCount(); ++row) {
        auto* name_item = base_table_->item(row, 0);
        auto* value_item = base_table_->item(row, 1);
        if (!name_item || !value_item) continue;
        const QString text = value_item->text().trimmed();
        if (text.isEmpty()) continue;
        bool ok = false;
        const double v = QLocale::c().toDouble(text, &ok);
        if (ok) out[name_item->text().toStdString()] = v;
    }
    return out;
}

nlohmann::json StudyPanel::readSplits(QString* error) const {
    nlohmann::json out = nlohmann::json::object();
    for (int row = 0; row < splits_table_->rowCount(); ++row) {
        auto* name_item = splits_table_->item(row, 0);
        auto* levels_item = splits_table_->item(row, 1);
        const QString name = name_item ? name_item->text().trimmed() : QString();
        const QString levels = levels_item ? levels_item->text().trimmed() : QString();
        if (name.isEmpty() && levels.isEmpty()) continue;
        if (name.isEmpty()) {
            if (error) *error = tr("split row %1 has levels but no parameter name").arg(row + 1);
            return {};
        }
        std::vector<double> values;
        for (const QString& part : levels.split(',', Qt::SkipEmptyParts)) {
            bool ok = false;
            const double v = QLocale::c().toDouble(part.trimmed(), &ok);
            if (!ok) {
                if (error) *error = tr("'%1' is not a number (parameter %2)").arg(part.trimmed(), name);
                return {};
            }
            values.push_back(v);
        }
        if (values.empty()) {
            if (error) *error = tr("parameter %1 has no levels").arg(name);
            return {};
        }
        out[name.toStdString()] = values;
    }
    return out;
}

void StudyPanel::onBuildClicked() {
    const nlohmann::json* t = currentTemplate();
    if (!t) {
        emit inputRejected(tr("No template"), tr("Choose a template first."));
        return;
    }
    QString error;
    const nlohmann::json splits = readSplits(&error);
    if (!error.isEmpty()) {
        emit inputRejected(tr("Invalid splits"), error);
        return;
    }
    QString build_error;
    if (!ctl_->buildRows(QString::fromStdString(t->value("id", std::string())), readBase(), splits, &build_error))
        if (!build_error.isEmpty()) emit inputRejected(tr("Study cannot build"), build_error);
}

void StudyPanel::onRunClicked() {
    applyRemoteHosts();  // in case the field was edited but never blurred
    ctl_->run(pool_spin_->value());
}

void StudyPanel::applyRemoteHosts() {
    ctl_->setRemoteHosts(remote_hosts_->text().split(QLatin1Char(','), Qt::SkipEmptyParts));
}

void StudyPanel::updateEnabled() {
    const bool busy = ctl_->busy();
    template_combo_->setEnabled(!busy);
    base_table_->setEnabled(!busy);
    splits_table_->setEnabled(!busy);
    add_split_->setEnabled(!busy);
    remove_split_->setEnabled(!busy);
    pool_spin_->setEnabled(!busy);
    build_->setEnabled(!busy);
    cancel_->setEnabled(busy);
    run_->setEnabled(!busy && !ctl_->rows().empty());
}

void StudyPanel::onRowsChanged() {
    axis_names_.clear();
    if (ctl_->axes().is_object())
        for (auto it = ctl_->axes().begin(); it != ctl_->axes().end(); ++it) axis_names_ << QString::fromStdString(it.key());
    rebuildRowsTable();
    // The field selector: scalar names off the first "done" row's result.
    const QString prev_field = field_combo_->currentText();
    field_combo_->clear();
    for (const auto& row : ctl_->rows()) {
        if (row.status != StudyController::RowStatus::Done || row.result_path.isEmpty()) continue;
        try {
            NpzFile npz = NpzFile::open(row.result_path.toStdWString());
            ResultModel model = ResultModel::from_npz(npz);
            for (const auto& name : model.scalar_names()) field_combo_->addItem(QString::fromStdString(name));
        } catch (...) {
        }
        break;
    }
    if (!prev_field.isEmpty()) {
        const int idx = field_combo_->findText(prev_field);
        if (idx >= 0) field_combo_->setCurrentIndex(idx);
    }
    rebuildMatrix();
    updateEnabled();

    int done = 0, running = 0, failed = 0, pending = 0, build_error = 0;
    for (const auto& row : ctl_->rows()) {
        switch (row.status) {
            case StudyController::RowStatus::Done: ++done; break;
            case StudyController::RowStatus::Running: ++running; break;
            case StudyController::RowStatus::Failed: ++failed; break;
            case StudyController::RowStatus::Pending: ++pending; break;
            case StudyController::RowStatus::BuildError: ++build_error; break;
        }
    }
    status_->setText(tr("%1 row(s): %2 done, %3 running, %4 pending, %5 failed, %6 rejected")
                         .arg(ctl_->rows().size())
                         .arg(done)
                         .arg(running)
                         .arg(pending)
                         .arg(failed)
                         .arg(build_error));
}

void StudyPanel::rebuildRowsTable() {
    const auto& rows = ctl_->rows();
    rows_table_->setColumnCount(static_cast<int>(axis_names_.size()) + 3);
    QStringList headers;
    headers << tr("#");
    headers += axis_names_;
    headers << tr("Status") << tr("Error");
    rows_table_->setHorizontalHeaderLabels(headers);
    rows_table_->setRowCount(static_cast<int>(rows.size()));
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto& row = rows[static_cast<std::size_t>(i)];
        int col = 0;
        rows_table_->setItem(i, col++, new QTableWidgetItem(QString::number(i + 1)));
        for (const QString& axis : axis_names_) {
            const std::string key = axis.toStdString();
            const QString text = row.params.contains(key) ? paramText(row.params[key]) : QString();
            rows_table_->setItem(i, col++, new QTableWidgetItem(text));
        }
        rows_table_->setItem(i, col++, new QTableWidgetItem(statusText(row.status)));
        rows_table_->setItem(i, col++, new QTableWidgetItem(row.error));
    }
    rows_table_->resizeColumnsToContents();
}

void StudyPanel::onRowDoubleClicked(int row, int) {
    if (row < 0 || row >= static_cast<int>(ctl_->rows().size())) return;
    const auto& r = ctl_->rows()[static_cast<std::size_t>(row)];
    if (r.status == StudyController::RowStatus::Done && !r.result_path.isEmpty()) emit openRequested(r.result_path);
}

void StudyPanel::rebuildMatrix() {
    matrix_table_->setRowCount(0);
    matrix_table_->setColumnCount(0);
    if (axis_names_.size() != 2 || field_combo_->currentText().isEmpty()) return;
    const nlohmann::json& axes = ctl_->axes();
    const std::string a0 = axis_names_[0].toStdString();
    const std::string a1 = axis_names_[1].toStdString();
    if (!axes.contains(a0) || !axes.contains(a1)) return;
    const auto& v0 = axes[a0];
    const auto& v1 = axes[a1];
    matrix_table_->setRowCount(static_cast<int>(v0.size()));
    matrix_table_->setColumnCount(static_cast<int>(v1.size()));
    QStringList vlabels, hlabels;
    for (const auto& v : v0) vlabels << paramText(v);
    for (const auto& v : v1) hlabels << paramText(v);
    matrix_table_->setVerticalHeaderLabels(vlabels);
    matrix_table_->setHorizontalHeaderLabels(hlabels);

    const std::string field = field_combo_->currentText().toStdString();
    for (const auto& row : ctl_->rows()) {
        if (row.status != StudyController::RowStatus::Done) continue;  // not-done: cell stays empty
        if (!row.params.contains(a0) || !row.params.contains(a1)) continue;
        int r = -1, c = -1;
        for (int i = 0; i < static_cast<int>(v0.size()); ++i)
            if (std::abs(v0[static_cast<std::size_t>(i)].get<double>() - row.params[a0].get<double>()) < 1e-12) r = i;
        for (int j = 0; j < static_cast<int>(v1.size()); ++j)
            if (std::abs(v1[static_cast<std::size_t>(j)].get<double>() - row.params[a1].get<double>()) < 1e-12) c = j;
        if (r < 0 || c < 0) continue;
        const auto value = studyMatrixCellValue(row.result_path, field);
        if (!value) continue;  // a removed/unreadable result: leave the cell empty too
        matrix_table_->setItem(r, c, new QTableWidgetItem(QLocale::c().toString(*value, 'g', 6)));
    }
}

}  // namespace tcad::desktop
