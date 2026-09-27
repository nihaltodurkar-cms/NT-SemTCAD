#include "mesh_editor.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSpinBox>

#include <initializer_list>

namespace tcad::desktop {

MeshEditor::MeshEditor(QWidget* parent) : QWidget(parent) {
    auto* form = new QFormLayout(this);

    // objectName on each field: how gui/tests' Python contract test and
    // desktop/tests/test_editors.cpp reach a specific field without this
    // class exposing internal widget pointers (the QML side's own
    // objectName-lookup convention, carried over to native Qt Widgets).
    nx_ = new QSpinBox(this);
    nx_->setObjectName("nx");
    nx_->setRange(2, 2000);
    form->addRow(tr("Nx"), nx_);

    ny_ = new QSpinBox(this);
    ny_->setObjectName("ny");
    ny_->setRange(2, 2000);
    form->addRow(tr("Ny"), ny_);

    grading_ = new QComboBox(this);
    grading_->setObjectName("grading");
    grading_->addItems({"uniform", "graded"});
    form->addRow(tr("Grading"), grading_);

    h_min_ = new QDoubleSpinBox(this);
    h_min_->setObjectName("h_min");
    h_min_->setDecimals(9);
    h_min_->setRange(0.0, 1.0);
    form->addRow(tr("h_min [cm]"), h_min_);

    h_max_ = new QDoubleSpinBox(this);
    h_max_->setObjectName("h_max");
    h_max_->setDecimals(9);
    h_max_->setRange(0.0, 1.0);
    form->addRow(tr("h_max [cm]"), h_max_);

    ratio_ = new QDoubleSpinBox(this);
    ratio_->setObjectName("ratio");
    ratio_->setDecimals(3);
    ratio_->setRange(1.001, 3.0);
    form->addRow(tr("Ratio"), ratio_);

    nz_ = new QSpinBox(this);
    nz_->setObjectName("nz");
    nz_->setRange(0, 500);
    nz_->setSpecialValueText(tr("(2D)"));
    form->addRow(tr("Nz"), nz_);

    wireSignals();
    setFieldsEnabled(false);
}

void MeshEditor::wireSignals() {
    connect(nx_, &QSpinBox::editingFinished, this, [this] {
        if (loading_ || !doc_) return;
        doc_->set_nx(nx_->value());
        emit meshEdited();
    });
    connect(ny_, &QSpinBox::editingFinished, this, [this] {
        if (loading_ || !doc_) return;
        doc_->set_ny(ny_->value());
        emit meshEdited();
    });
    connect(grading_, &QComboBox::currentTextChanged, this, [this](const QString& text) {
        if (loading_ || !doc_) return;
        doc_->set_grading(text.toStdString());
        emit meshEdited();
    });
    connect(h_min_, &QDoubleSpinBox::editingFinished, this, [this] {
        if (loading_ || !doc_) return;
        doc_->set_h_min(h_min_->value());
        emit meshEdited();
    });
    connect(h_max_, &QDoubleSpinBox::editingFinished, this, [this] {
        if (loading_ || !doc_) return;
        doc_->set_h_max(h_max_->value());
        emit meshEdited();
    });
    connect(ratio_, &QDoubleSpinBox::editingFinished, this, [this] {
        if (loading_ || !doc_) return;
        doc_->set_ratio(ratio_->value());
        emit meshEdited();
    });
    connect(nz_, &QSpinBox::editingFinished, this, [this] {
        if (loading_ || !doc_) return;
        // 0 = "(2D)" (StructurePanel.qml's own depth_cm==0 convention).
        doc_->set_nz(nz_->value() == 0 ? std::optional<int>() : std::optional<int>(nz_->value()));
        emit meshEdited();
    });
}

void MeshEditor::setFieldsEnabled(bool on) {
    const std::initializer_list<QWidget*> fields = {nx_, ny_, grading_, h_min_, h_max_, ratio_, nz_};
    for (QWidget* w : fields) w->setEnabled(on);
}

void MeshEditor::setDocument(MeshDocument* doc) {
    doc_ = doc;
    setFieldsEnabled(doc_ != nullptr);
    refresh();
}

void MeshEditor::refresh() {
    loading_ = true;
    if (doc_) {
        nx_->setValue(doc_->nx());
        ny_->setValue(doc_->ny());
        grading_->setCurrentText(QString::fromStdString(doc_->grading()));
        h_min_->setValue(doc_->h_min().value_or(0.0));
        h_max_->setValue(doc_->h_max().value_or(0.0));
        ratio_->setValue(doc_->ratio());
        nz_->setValue(doc_->nz().value_or(0));
    } else {
        nx_->setValue(nx_->minimum());
        ny_->setValue(ny_->minimum());
        h_min_->setValue(0.0);
        h_max_->setValue(0.0);
        ratio_->setValue(ratio_->minimum());
        nz_->setValue(0);
    }
    loading_ = false;
}

}  // namespace tcad::desktop
