// ContactEditor: voltage only (NATIVE-DESKTOP-PLAN.md section 20.4 S4 --
// QML parity target: ContactEditor.qml, read directly before writing
// this). Name and edge are shown read-only; QML's own editor has no
// control for either -- boundary editing (edge, range) is not part of
// the reference GUI at all, so it is not invented here either.
#pragma once

#include "document/structure_document.hpp"

#include <QWidget>

class QDoubleSpinBox;
class QLabel;

namespace tcad::desktop {

class ContactEditor : public QWidget {
    Q_OBJECT

public:
    explicit ContactEditor(QWidget* parent = nullptr);

    // Non-owning. An empty `contactId` (or doc == nullptr) clears/disables the form.
    void setContact(StructureDocument* doc, const QString& contactId);
    QString contactId() const { return contact_id_; }
    void refresh();

signals:
    void contactEdited(const QString& contactId);

private:
    void writeBack();

    StructureDocument* doc_ = nullptr;
    QString contact_id_;
    bool loading_ = false;

    QLabel* name_ = nullptr;
    QLabel* edge_ = nullptr;
    QDoubleSpinBox* voltage_ = nullptr;
};

}  // namespace tcad::desktop
