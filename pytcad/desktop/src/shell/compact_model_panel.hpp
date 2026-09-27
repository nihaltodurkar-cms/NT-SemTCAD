// The Compact Model dock (NATIVE-DESKTOP-PLAN.md section 25): closes the
// QML app's CompactModelPanel gap -- diode / n-MOSFET SPICE-parameter
// extraction (M38 Phase 4, workbench.compact) against a real solved
// device, run as a local subprocess exactly like Run/Study.
//
// Numbers are typed as text and read in the C locale (RunPanel's own
// convention); an empty or non-finite value is refused, named, before
// anything is sent.
#pragma once

#include <nlohmann/json.hpp>

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;

namespace tcad::desktop {

class CompactModelController;

namespace plot {
class PlotView;
}

class CompactModelPanel : public QWidget {
    Q_OBJECT

public:
    explicit CompactModelPanel(CompactModelController* controller, QWidget* parent = nullptr);

    QComboBox* kindCombo() const { return kind_; }
    QPushButton* extractButton() const { return extract_; }
    QPushButton* stopButton() const { return stop_; }
    QLabel* statusLabel() const { return status_; }
    QPlainTextEdit* netlistView() const { return netlist_; }
    plot::PlotView* plotView() const { return plot_; }
    // A form field by its object name (e.g. "DiodeLUm"), for tests.
    QLineEdit* field(const QString& name) const;

signals:
    void inputRejected(const QString& title, const QString& detail);

private:
    QWidget* buildForms();
    void onKindChanged();
    bool requestExtract();
    void onStarted();
    void onFinished(const nlohmann::json& manifest);
    void onFailed(const QString& summary, const QString& details);
    void updateEnabled();

    CompactModelController* ctl_;
    QComboBox* kind_ = nullptr;
    QStackedWidget* forms_ = nullptr;
    QPushButton* extract_ = nullptr;
    QPushButton* stop_ = nullptr;
    QLabel* status_ = nullptr;
    QPlainTextEdit* netlist_ = nullptr;
    plot::PlotView* plot_ = nullptr;
    bool busy_ = false;
};

}  // namespace tcad::desktop
