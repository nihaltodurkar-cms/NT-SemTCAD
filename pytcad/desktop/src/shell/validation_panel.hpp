// ValidationPanel (P4 S8, NATIVE-DESKTOP-PLAN.md section 20.4): a single
// banner/list reused for both structure and process errors -- QML's own
// ValidationPanel.qml is one component instantiated twice (StructurePanel
// with the default `errorsProperty`, ProcessPanel overriding it); this
// is the same "one widget, two call sites, pre-formatted message list"
// shape, just handed already-formatted strings (structure_error_messages/
// format_process_errors, document/validation.hpp) rather than reading a
// controller property by name -- there is no QML property system here.
#pragma once

#include <QWidget>

#include <string>
#include <vector>

class QLabel;
class QListWidget;

namespace tcad::desktop {

class ValidationPanel : public QWidget {
    Q_OBJECT

public:
    explicit ValidationPanel(QWidget* parent = nullptr);

    // Replaces the displayed messages; empty -> "OK", non-empty -> "FAILED".
    void setErrors(const std::vector<std::string>& messages);

    bool ok() const { return errors_.empty(); }
    std::size_t errorCount() const { return errors_.size(); }
    std::string errorAt(std::size_t index) const { return errors_.at(index); }
    QString statusText() const;

private:
    std::vector<std::string> errors_;
    QLabel* status_ = nullptr;
    QListWidget* list_ = nullptr;
};

}  // namespace tcad::desktop
