#include "doping_editor.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>

#include <initializer_list>

namespace tcad::desktop {
namespace {

QDoubleSpinBox* makeCmField(QWidget* parent) {
    auto* box = new QDoubleSpinBox(parent);
    box->setDecimals(9);
    box->setRange(-1.0, 1.0);
    return box;
}

QDoubleSpinBox* makeDopingField(QWidget* parent) {
    // cm^-3, signed; QDoubleSpinBox has no scientific-notation display,
    // so this is a plain wide-range field -- functional, not polished
    // (S3's own scope is correctness of the edit, not display formatting).
    auto* box = new QDoubleSpinBox(parent);
    box->setDecimals(0);
    box->setRange(-1e21, 1e21);
    box->setSingleStep(1e14);
    return box;
}

}  // namespace

DopingEditor::DopingEditor(QWidget* parent) : QWidget(parent) {
    form_ = new QFormLayout(this);

    name_ = new QLineEdit(this);
    name_->setObjectName("regionName");
    form_->addRow(tr("Name"), name_);

    material_ = new QLabel(this);
    material_->setObjectName("regionMaterial");
    form_->addRow(tr("Material"), material_);

    x_min_ = makeCmField(this);
    x_min_->setObjectName("x_min");
    form_->addRow(tr("x_min [cm]"), x_min_);
    x_max_ = makeCmField(this);
    x_max_->setObjectName("x_max");
    form_->addRow(tr("x_max [cm]"), x_max_);
    y_min_ = makeCmField(this);
    y_min_->setObjectName("y_min");
    form_->addRow(tr("y_min [cm]"), y_min_);
    y_max_ = makeCmField(this);
    y_max_->setObjectName("y_max");
    form_->addRow(tr("y_max [cm]"), y_max_);
    z_min_ = makeCmField(this);
    z_min_->setObjectName("z_min");
    form_->addRow(tr("z_min [cm]"), z_min_);
    z_max_ = makeCmField(this);
    z_max_->setObjectName("z_max");
    form_->addRow(tr("z_max [cm]"), z_max_);

    net_doping_ = makeDopingField(this);
    net_doping_->setObjectName("net_doping_cm3");
    form_->addRow(tr("Net doping [cm^-3]"), net_doping_);

    profile_ = new QComboBox(this);
    profile_->setObjectName("doping_profile");
    profile_->addItems({"uniform", "gaussian_erfc"});
    form_->addRow(tr("Doping profile"), profile_);

    peak_ = makeDopingField(this);
    peak_->setObjectName("profile_peak_cm3");
    form_->addRow(tr("Peak [cm^-3]"), peak_);
    sigma_y_ = makeCmField(this);
    sigma_y_->setObjectName("profile_sigma_y");
    form_->addRow(tr("Sigma y [cm]"), sigma_y_);
    sigma_lat_ = makeCmField(this);
    sigma_lat_->setObjectName("profile_sigma_lat");
    form_->addRow(tr("Sigma lateral [cm]"), sigma_lat_);
    edge_x_ = makeCmField(this);
    edge_x_->setObjectName("profile_edge_x");
    form_->addRow(tr("Edge x [cm]"), edge_x_);

    high_side_ = new QComboBox(this);
    high_side_->setObjectName("profile_high_side");
    high_side_->addItems({"left", "right"});
    form_->addRow(tr("High side"), high_side_);

    wireSignals();
    setFieldsEnabled(false);
}

void DopingEditor::wireSignals() {
    connect(name_, &QLineEdit::editingFinished, this, [this] {
        if (!loading_) writeBack();
    });
    for (QDoubleSpinBox* box : {x_min_, x_max_, y_min_, y_max_, z_min_, z_max_, net_doping_,
                               peak_, sigma_y_, sigma_lat_, edge_x_}) {
        connect(box, &QDoubleSpinBox::editingFinished, this, [this] {
            if (!loading_) writeBack();
        });
    }
    connect(profile_, &QComboBox::currentTextChanged, this, [this](const QString&) {
        if (loading_) return;
        updateRowVisibility();
        writeBack();
    });
    connect(high_side_, &QComboBox::currentTextChanged, this, [this](const QString&) {
        if (!loading_) writeBack();
    });
}

void DopingEditor::setFieldsEnabled(bool on) {
    const std::initializer_list<QWidget*> fields = {
        name_, material_, x_min_, x_max_, y_min_, y_max_, z_min_, z_max_,
        net_doping_, profile_, peak_, sigma_y_, sigma_lat_, edge_x_, high_side_};
    for (QWidget* w : fields) w->setEnabled(on);
}

void DopingEditor::setRegion(StructureDocument* doc, const QString& regionId) {
    doc_ = doc;
    region_id_ = regionId;
    setFieldsEnabled(doc_ != nullptr && !region_id_.isEmpty());
    refresh();
}

void DopingEditor::refresh() {
    loading_ = true;
    is3d_ = doc_ && doc_->depth_cm().has_value();
    if (doc_ && !region_id_.isEmpty()) {
        RegionData r{};
        bool found = false;
        for (std::size_t i = 0; i < doc_->region_count(); ++i) {
            if (doc_->region(i).id == region_id_.toStdString()) {
                r = doc_->region(i);
                found = true;
                break;
            }
        }
        if (found) {
            name_->setText(QString::fromStdString(r.name));
            material_->setText(QString::fromStdString(r.material));
            x_min_->setValue(r.x_min);
            x_max_->setValue(r.x_max);
            y_min_->setValue(r.y_min);
            y_max_->setValue(r.y_max);
            z_min_->setValue(r.z_min.value_or(0.0));
            z_max_->setValue(r.z_max.value_or(0.0));
            net_doping_->setValue(r.net_doping_cm3);
            profile_->setCurrentText(QString::fromStdString(r.doping_profile));
            peak_->setValue(r.profile_peak_cm3.value_or(0.0));
            sigma_y_->setValue(r.profile_sigma_y.value_or(0.0));
            sigma_lat_->setValue(r.profile_sigma_lat.value_or(0.0));
            edge_x_->setValue(r.profile_edge_x.value_or(0.0));
            high_side_->setCurrentText(QString::fromStdString(r.profile_high_side));
        }
    }
    updateRowVisibility();
    loading_ = false;
}

void DopingEditor::updateRowVisibility() {
    form_->setRowVisible(z_min_, is3d_);
    form_->setRowVisible(z_max_, is3d_);
    const bool profiled = profile_->currentText() != "uniform";
    form_->setRowVisible(peak_, profiled);
    form_->setRowVisible(sigma_y_, profiled);
    form_->setRowVisible(sigma_lat_, profiled);
    form_->setRowVisible(edge_x_, profiled);
    form_->setRowVisible(high_side_, profiled);
}

void DopingEditor::writeBack() {
    if (!doc_ || region_id_.isEmpty()) return;
    RegionData r;
    r.id = region_id_.toStdString();
    r.name = name_->text().toStdString();
    r.material = material_->text().toStdString();  // read-only field, carried through unchanged
    r.x_min = x_min_->value();
    r.x_max = x_max_->value();
    r.y_min = y_min_->value();
    r.y_max = y_max_->value();
    if (is3d_) {
        r.z_min = z_min_->value();
        r.z_max = z_max_->value();
    }
    r.net_doping_cm3 = net_doping_->value();
    r.doping_profile = profile_->currentText().toStdString();
    if (r.doping_profile != "uniform") {
        r.profile_peak_cm3 = peak_->value();
        r.profile_sigma_y = sigma_y_->value();
        r.profile_sigma_lat = sigma_lat_->value();
        r.profile_edge_x = edge_x_->value();
    }
    r.profile_high_side = high_side_->currentText().toStdString();
    doc_->set_region(region_id_.toStdString(), r);
    emit regionEdited(region_id_);
}

}  // namespace tcad::desktop
