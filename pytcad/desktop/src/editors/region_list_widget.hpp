// RegionListWidget: add/remove/reorder regions in a StructureDocument
// (NATIVE-DESKTOP-PLAN.md section 20.4 S3 -- QML parity target:
// RegionList.qml). List order IS compositing priority (later overwrites
// earlier, per rasterize_doping's own rule -- StructureDocument's own
// header comment and StructureEditorView's hit-testing already follow
// this), so "move" here is a priority change, not merely visual reorder.
//
// A new region's id is generated the same shape the Python GUI's own
// AppController.addRegion() uses (`uuid.uuid4().hex[:8]`, checked
// directly): 8 lowercase hex characters, via QUuid rather than
// reproducing Python's RNG -- ids only need to be unique, not bit-
// identical to what Python would have picked for the same click.
#pragma once

#include "document/structure_document.hpp"

#include <QWidget>

class QListWidget;
class QPushButton;

namespace tcad::desktop {

class RegionListWidget : public QWidget {
    Q_OBJECT

public:
    explicit RegionListWidget(QWidget* parent = nullptr);

    // Non-owning; caller keeps the document alive. Null disables every control.
    void setDocument(StructureDocument* doc);
    StructureDocument* document() const { return doc_; }
    // Re-reads the document's region list (e.g. after the canvas moved/
    // resized/added one), preserving the current selection by id when
    // it still exists.
    void refresh();

    QString selectedRegionId() const;
    void selectRegion(const QString& id);

signals:
    void selectionChanged(const QString& regionId);
    // Add/remove/reorder happened (a field-only edit, e.g. from
    // DopingEditor, does not fire this -- the row count and order did
    // not change, so a full refresh() is unnecessary there).
    void regionsChanged();

private:
    void addRegion();
    void removeSelected();
    void moveSelected(int offset);
    void onSelectionChanged();

    StructureDocument* doc_ = nullptr;
    QListWidget* list_ = nullptr;
    QPushButton* add_ = nullptr;
    QPushButton* remove_ = nullptr;
    QPushButton* up_ = nullptr;
    QPushButton* down_ = nullptr;
};

}  // namespace tcad::desktop
