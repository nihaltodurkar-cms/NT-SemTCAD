// StructureEditorView: the native "StructureEditor" (NATIVE-DESKTOP-
// PLAN.md section 5.3/20.4 S2) -- a QWidget painted with QPainter, not
// QGraphicsView. Section 5.3 sketched QGraphicsView before P1/P2 existed;
// PlotView (views/plot/plot_view.hpp) established the actual precedent
// this codebase uses for an interactive, testable custom-drawn widget --
// geometry and interaction exposed as plain methods "for tests," painted
// at device pixels with theme tokens only -- so this class follows that
// convention instead, for consistency with the one already built and
// gated.
//
// Regions are drawn as rectangles (fill by net doping sign, outline in
// the theme border colour, a thicker Selection outline for the selected
// one) and are the only thing this view can move or resize by drag.
// Contacts and gates are drawn along their boundary edge but are NOT
// interactive here -- BoundarySpec is an edge name plus an optional
// numeric range, which is a more natural fit for S4's form editor than
// for a drag gesture; S2's own scope is the region canvas plus the mesh
// editor (plan section 20.4).
//
// Snapping (snapX/snapY) only applies when MeshDocument::grading() is
// "uniform": that axis is a plain linspace(0, length, n), computable
// here without porting graded_mesh() (pytcad/mesh.py, Python-only, not
// part of the frozen-core C++ port) to C++. A "graded" mesh's region
// edits are unsnapped in S2 -- disclosed, not a silent gap.
#pragma once

#include "document/mesh_document.hpp"
#include "document/structure_document.hpp"

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QWidget>

#include <optional>

namespace tcad::desktop {

class StructureEditorView : public QWidget {
    Q_OBJECT

public:
    explicit StructureEditorView(QWidget* parent = nullptr);

    // Non-owning; caller keeps both alive and in sync with each other
    // (same width_cm/height_cm the structure was authored against).
    // Null clears the view.
    void setDocuments(StructureDocument* structure, MeshDocument* mesh);
    StructureDocument* structureDocument() const { return structure_; }
    MeshDocument* meshDocument() const { return mesh_; }

    // -- geometry, in logical pixels; for tests (PlotView's own convention) --
    QRectF domainRect() const;
    QPointF toPixel(double x_cm, double y_cm) const;
    QPointF toMeshSpace(QPointF pixel) const;
    double snapX(double x_cm) const;
    double snapY(double y_cm) const;

    // -- selection and hover ------------------------------------------------
    QString selectedRegionId() const { return selected_; }
    QString hitTestRegion(QPointF logical_pos) const;  // "" if none; topmost first
    void hoverAt(QPointF logical_pos);
    QString readout() const { return readout_; }

    // Testable entry points mirroring the real mouse handlers exactly
    // (see plot_view.hpp's hoverAt precedent) -- a test drives these
    // directly instead of a real mouse cursor.
    void beginDragAt(QPointF logical_pos);
    void dragTo(QPointF logical_pos);
    void endDrag();
    bool isDragging() const { return dragging_; }

signals:
    void regionEdited(const QString& regionId);
    void selectionChanged(const QString& regionId);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    enum class DragMode { None, Move, ResizeRight, ResizeBottom };

    std::optional<std::size_t> regionIndexById(const QString& id) const;

    StructureDocument* structure_ = nullptr;
    MeshDocument* mesh_ = nullptr;

    QString selected_;
    QString readout_;

    bool dragging_ = false;
    DragMode drag_mode_ = DragMode::None;
    QString drag_region_id_;
    RegionData drag_orig_;
    QPointF drag_start_mesh_;
};

}  // namespace tcad::desktop
