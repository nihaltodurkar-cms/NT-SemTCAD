// GateListWidget: the gate analogue of ContactListWidget -- selection
// only, no add/remove, for the same reason (see that file's comment).
#pragma once

#include "document/structure_document.hpp"

#include <QWidget>

class QListWidget;

namespace tcad::desktop {

class GateListWidget : public QWidget {
    Q_OBJECT

public:
    explicit GateListWidget(QWidget* parent = nullptr);

    void setDocument(StructureDocument* doc);
    StructureDocument* document() const { return doc_; }
    void refresh();

    QString selectedGateId() const;
    void selectGate(const QString& id);

signals:
    void selectionChanged(const QString& gateId);

private:
    StructureDocument* doc_ = nullptr;
    QListWidget* list_ = nullptr;
};

}  // namespace tcad::desktop
