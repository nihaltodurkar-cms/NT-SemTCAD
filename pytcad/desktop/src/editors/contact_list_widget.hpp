// ContactListWidget: selection only, no add/remove (NATIVE-DESKTOP-
// PLAN.md section 20.4 S4). Checked directly against the QML GUI before
// writing this: `app_controller.py` has no `addContact`/`addGate` slot
// at all -- contacts and gates are only ever created by a template or
// device builder (`StructureModel.add_contact`/`add_gate`, called from
// Python, never from a QML button), and `StructurePanel.qml`'s own
// contact list is a plain `ListView` with no add/remove control either.
// Building add/remove here would be inventing capability beyond parity,
// not porting it -- so this mirrors the QML list exactly: pick one, feed
// it to the editor.
#pragma once

#include "document/structure_document.hpp"

#include <QWidget>

class QListWidget;

namespace tcad::desktop {

class ContactListWidget : public QWidget {
    Q_OBJECT

public:
    explicit ContactListWidget(QWidget* parent = nullptr);

    void setDocument(StructureDocument* doc);
    StructureDocument* document() const { return doc_; }
    void refresh();

    QString selectedContactId() const;
    void selectContact(const QString& id);

signals:
    void selectionChanged(const QString& contactId);

private:
    StructureDocument* doc_ = nullptr;
    QListWidget* list_ = nullptr;
};

}  // namespace tcad::desktop
