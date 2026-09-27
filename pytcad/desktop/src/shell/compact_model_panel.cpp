#include "compact_model_panel.hpp"

#include "run/compact_model_controller.hpp"
#include "shell/field_form.hpp"
#include "theme/theme.hpp"
#include "views/plot/plot_view.hpp"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <cmath>
#include <optional>

namespace tcad::desktop {
namespace {

// M38 Phase 4's own defaults (compact_runner.py's job dict; the removed
// QML app's DiodeExtractForm/MosfetExtractForm used the same numbers).
constexpr FieldDef kDiode[] = {
    {"DiodeLUm", "L [um]", "2.0"},        {"DiodeXjUm", "xj [um]", "0.5"},
    {"DiodeNa", "Na [cm^-3]", "1e17"},    {"DiodeNd", "Nd [cm^-3]", "1e19"},
    {"DiodeVStart", "V start [V]", "0.0"}, {"DiodeVStop", "V stop [V]", "0.8"},
    {"DiodeVStep", "V step [V]", "0.02"}, {"DiodeScale", "Scale [cm^2]", "1e-4"},
};
// Defaults are `tests/test_m38_compact_model.py`'s own G2-TCAD fixture
// (_tcad_mosfet(), test_g2_tcad_2d_mosfet_matches_long_channel_theory): a
// real Device2D that is known to converge and stay above threshold across
// this exact Vg range -- MOSFET1 refuses a window that crosses threshold
// (extract_mosfet1 returns EXACTLY zero below Vt0), so an arbitrary Vg
// start (e.g. 0 V) can fail on an ordinary device with Vt0 > 0.
constexpr FieldDef kMosfet[] = {
    {"MosLgUm", "Lg [um]", "1.0"},          {"MosLsdUm", "Lsd [um]", "0.5"},
    {"MosDepthUm", "depth [um]", "1.0"},    {"MosNa", "Na [cm^-3]", "5e16"},
    {"MosNsdPeak", "Nsd peak [cm^-3]", "5e18"}, {"MosToxNm", "tox [nm]", "10.0"},
    {"MosVgStart", "Vg start [V]", "0.5"},  {"MosVgStop", "Vg stop [V]", "3.0"},
    {"MosVgStep", "Vg step [V]", "0.25"},   {"MosVdsLin", "Vds (Id-Vg) [V]", "0.1"},
    {"MosVdStart", "Vd start [V]", "1.6"},  {"MosVdStop", "Vd stop [V]", "3.0"},
    {"MosVdStep", "Vd step [V]", "0.2"},    {"MosVgsSat", "Vgs (Id-Vd) [V]", "1.5"},
    {"MosScale", "Scale [cm]", "1e-4"},
};

}  // namespace

CompactModelPanel::CompactModelPanel(CompactModelController* controller, QWidget* parent)
    : QWidget(parent), ctl_(controller) {
    setObjectName("CompactModelPanel");
    auto* col = new QVBoxLayout(this);
    auto* top = new QFormLayout;
    kind_ = new QComboBox(this);
    kind_->setObjectName("CompactKind");
    kind_->addItem(tr("Diode"), QStringLiteral("diode"));
    kind_->addItem(tr("MOSFET (n)"), QStringLiteral("mosfet1"));
    top->addRow(tr("Kind"), kind_);
    col->addLayout(top);
    col->addWidget(buildForms());

    auto* buttons = new QHBoxLayout;
    extract_ = new QPushButton(tr("Extract"), this);
    extract_->setObjectName("CompactExtractButton");
    stop_ = new QPushButton(tr("Stop"), this);
    stop_->setObjectName("CompactStopButton");
    stop_->setEnabled(false);
    buttons->addWidget(extract_);
    buttons->addWidget(stop_);
    col->addLayout(buttons);

    status_ = new QLabel(this);
    status_->setObjectName("CompactStatus");
    status_->setWordWrap(true);
    col->addWidget(status_);

    plot_ = new plot::PlotView(this);
    plot_->setObjectName("CompactPlot");
    col->addWidget(plot_, 1);

    netlist_ = new QPlainTextEdit(this);
    netlist_->setObjectName("CompactNetlist");
    netlist_->setReadOnly(true);
    netlist_->setMaximumBlockCount(200);
    netlist_->setPlaceholderText(tr("The fitted SPICE netlist appears here after extraction."));
    col->addWidget(netlist_);

    connect(kind_, &QComboBox::currentIndexChanged, this, &CompactModelPanel::onKindChanged);
    connect(extract_, &QPushButton::clicked, this, &CompactModelPanel::requestExtract);
    connect(stop_, &QPushButton::clicked, ctl_, &CompactModelController::stop);
    connect(ctl_, &CompactModelController::started, this, &CompactModelPanel::onStarted);
    connect(ctl_, &CompactModelController::finished, this, &CompactModelPanel::onFinished);
    connect(ctl_, &CompactModelController::failed, this, &CompactModelPanel::onFailed);

    forms_->setCurrentIndex(kind_->currentIndex());
    updateEnabled();
}

QWidget* CompactModelPanel::buildForms() {
    forms_ = new QStackedWidget(this);
    forms_->setObjectName("CompactForms");
    auto page = [this] {
        auto* w = new QWidget(forms_);
        auto* form = new QFormLayout(w);
        form->setContentsMargins(0, 0, 0, 0);
        forms_->addWidget(w);
        return form;
    };
    QFormLayout* diode = page();
    for (const auto& f : kDiode) addField(diode, f, this);
    QFormLayout* mosfet = page();
    for (const auto& f : kMosfet) addField(mosfet, f, this);
    return forms_;
}

QLineEdit* CompactModelPanel::field(const QString& name) const { return findChild<QLineEdit*>(name); }

void CompactModelPanel::onKindChanged() { forms_->setCurrentIndex(kind_->currentIndex()); }

void CompactModelPanel::updateEnabled() {
    extract_->setEnabled(!busy_);
    stop_->setEnabled(busy_);
    kind_->setEnabled(!busy_);
    forms_->setEnabled(!busy_);
}

bool CompactModelPanel::requestExtract() {
    if (busy_) return false;
    std::optional<QString> bad;
    auto number = [&](const char* name) -> double { return numberField(this, name, bad); };
    const QString kind = kind_->currentData().toString();
    nlohmann::json job = {{"kind", kind.toStdString()}};
    if (kind == "diode") {
        job["L_um"] = number("DiodeLUm");
        job["xj_um"] = number("DiodeXjUm");
        job["Na"] = number("DiodeNa");
        job["Nd"] = number("DiodeNd");
        job["v_start"] = number("DiodeVStart");
        job["v_stop"] = number("DiodeVStop");
        job["v_step"] = number("DiodeVStep");
        job["scale"] = number("DiodeScale");
    } else {
        job["Lg_um"] = number("MosLgUm");
        job["Lsd_um"] = number("MosLsdUm");
        job["depth_um"] = number("MosDepthUm");
        job["Na"] = number("MosNa");
        job["Nsd_peak"] = number("MosNsdPeak");
        job["tox_nm"] = number("MosToxNm");
        job["vg_start"] = number("MosVgStart");
        job["vg_stop"] = number("MosVgStop");
        job["vg_step"] = number("MosVgStep");
        job["vds_lin"] = number("MosVdsLin");
        job["vd_start"] = number("MosVdStart");
        job["vd_stop"] = number("MosVdStop");
        job["vd_step"] = number("MosVdStep");
        job["vgs_sat"] = number("MosVgsSat");
        job["scale"] = number("MosScale");
    }
    if (bad) {
        emit inputRejected(tr("Invalid compact-model configuration"),
                           tr("%1 must be a finite number.").arg(*bad));
        return false;
    }
    return ctl_->extract(job);
}

void CompactModelPanel::onStarted() {
    busy_ = true;
    updateEnabled();
    status_->setText(tr("Extracting..."));
}

void CompactModelPanel::onFinished(const nlohmann::json& manifest) {
    busy_ = false;
    updateEnabled();
    const bool converged = manifest.value("converged", false);
    status_->setText(converged ? tr("Extraction converged.") : tr("Extraction did NOT converge -- inspect the fit."));
    netlist_->setPlainText(QString::fromStdString(manifest.value("netlist", std::string())));

    // manifest["curve"] and its sub-keys come from a subprocess-written
    // result file, not a value this process controls: a raw operator[]
    // on a missing key is undefined behaviour in a release build (it
    // skips nlohmann::json's own JSON_ASSERT and dereferences end()),
    // not a catchable exception -- so every access here goes through
    // .contains()/.at() first, and any shape mismatch (missing key,
    // wrong type) is reported through the same status label onFailed()
    // uses, never left to crash the process.
    if (!manifest.contains("curve") || !manifest.at("curve").is_object()) {
        status_->setText(tr("Extraction result has no curve data to plot."));
        return;
    }
    const auto& curve = manifest.at("curve");
    const bool diode = manifest.value("kind", std::string()) == "diode";
    const char* xKey = diode ? "v" : "vg";
    const char* tcadKey = diode ? "i_tcad" : "ig_tcad";
    const char* fitKey = diode ? "i_fit" : "ig_fit";
    if (!curve.contains(xKey) || !curve.contains(tcadKey) || !curve.contains(fitKey)) {
        status_->setText(tr("Extraction result's curve data is incomplete."));
        return;
    }
    plot::PlotModel model;
    try {
        const auto x = curve.at(xKey).get<std::vector<double>>();
        plot::Series tcad{tr("TCAD"), x, curve.at(tcadKey).get<std::vector<double>>(), theme::seriesColour(0)};
        plot::Series fit{tr("Fit"), x, curve.at(fitKey).get<std::vector<double>>(), theme::seriesColour(1)};
        fit.line = plot::LineStyle::Dashed;
        if (diode) {
            model.title = tr("Diode I-V");
            model.x = {tr("V [V]")};
            model.y = {tr("I [A]")};
        } else {
            model.title = tr("Id-Vg (fit)");
            model.x = {tr("Vg [V]")};
            model.y = {tr("Id [A]")};
        }
        model.series = {std::move(tcad), std::move(fit)};
    } catch (const nlohmann::json::exception&) {
        status_->setText(tr("Extraction result's curve data has the wrong shape."));
        return;
    }
    plot_->setModel(std::move(model));
}

void CompactModelPanel::onFailed(const QString& summary, const QString& details) {
    busy_ = false;
    updateEnabled();
    status_->setText(summary + (details.isEmpty() ? QString() : ": " + details));
}

}  // namespace tcad::desktop
