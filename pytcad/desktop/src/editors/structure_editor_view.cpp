#include "structure_editor_view.hpp"

#include "theme/theme.hpp"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace tcad::desktop {
namespace {

constexpr double kMarginPx = 24.0;
constexpr double kEdgeTolerancePx = 6.0;

std::vector<double> uniform_axis(double length_cm, int n) {
    std::vector<double> axis;
    if (n < 2 || length_cm <= 0.0) return axis;
    axis.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) axis.push_back(length_cm * static_cast<double>(i) / (n - 1));
    return axis;
}

double nearest(const std::vector<double>& axis, double v) {
    if (axis.empty()) return v;
    auto it = std::min_element(axis.begin(), axis.end(),
                               [v](double a, double b) { return std::abs(a - v) < std::abs(b - v); });
    return *it;
}

}  // namespace

StructureEditorView::StructureEditorView(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumSize(200, 150);
}

void StructureEditorView::setDocuments(StructureDocument* structure, MeshDocument* mesh) {
    structure_ = structure;
    mesh_ = mesh;
    selected_.clear();
    readout_.clear();
    dragging_ = false;
    update();
}

QRectF StructureEditorView::domainRect() const {
    QRectF avail(kMarginPx, kMarginPx, width() - 2 * kMarginPx, height() - 2 * kMarginPx);
    if (!structure_ || avail.width() <= 0 || avail.height() <= 0) return avail;
    const double w_cm = structure_->width_cm(), h_cm = structure_->height_cm();
    if (w_cm <= 0 || h_cm <= 0) return avail;
    const double scale = std::min(avail.width() / w_cm, avail.height() / h_cm);
    const double dw = scale * w_cm, dh = scale * h_cm;
    return QRectF(avail.left() + (avail.width() - dw) / 2.0, avail.top() + (avail.height() - dh) / 2.0, dw, dh);
}

QPointF StructureEditorView::toPixel(double x_cm, double y_cm) const {
    if (!structure_) return {};
    const QRectF r = domainRect();
    const double w_cm = structure_->width_cm(), h_cm = structure_->height_cm();
    if (w_cm <= 0 || h_cm <= 0) return r.topLeft();
    return {r.left() + (x_cm / w_cm) * r.width(), r.top() + (y_cm / h_cm) * r.height()};
}

QPointF StructureEditorView::toMeshSpace(QPointF pixel) const {
    if (!structure_) return {};
    const QRectF r = domainRect();
    const double w_cm = structure_->width_cm(), h_cm = structure_->height_cm();
    if (r.width() <= 0 || r.height() <= 0) return {};
    return {(pixel.x() - r.left()) / r.width() * w_cm, (pixel.y() - r.top()) / r.height() * h_cm};
}

double StructureEditorView::snapX(double x_cm) const {
    if (!structure_ || !mesh_ || mesh_->grading() != "uniform") return x_cm;
    return nearest(uniform_axis(structure_->width_cm(), mesh_->nx()), x_cm);
}

double StructureEditorView::snapY(double y_cm) const {
    if (!structure_ || !mesh_ || mesh_->grading() != "uniform") return y_cm;
    return nearest(uniform_axis(structure_->height_cm(), mesh_->ny()), y_cm);
}

QString StructureEditorView::hitTestRegion(QPointF logical_pos) const {
    if (!structure_) return {};
    // Topmost first: rasterize_doping's own "later overwrites earlier"
    // compositing rule (structure_model.py) means the last region drawn
    // is the one actually visible/editable at an overlap.
    for (int i = static_cast<int>(structure_->region_count()) - 1; i >= 0; --i) {
        const RegionData r = structure_->region(static_cast<std::size_t>(i));
        const QRectF pr = QRectF(toPixel(r.x_min, r.y_min), toPixel(r.x_max, r.y_max)).normalized();
        if (pr.contains(logical_pos)) return QString::fromStdString(r.id);
    }
    return {};
}

void StructureEditorView::hoverAt(QPointF logical_pos) {
    if (!structure_) {
        readout_.clear();
        update();
        return;
    }
    const QPointF m = toMeshSpace(logical_pos);
    readout_ = QString("x = %1 um, y = %2 um").arg(m.x() * 1e4, 0, 'f', 3).arg(m.y() * 1e4, 0, 'f', 3);
    update();
}

std::optional<std::size_t> StructureEditorView::regionIndexById(const QString& id) const {
    if (!structure_) return std::nullopt;
    for (std::size_t i = 0; i < structure_->region_count(); ++i)
        if (structure_->region(i).id == id.toStdString()) return i;
    return std::nullopt;
}

void StructureEditorView::beginDragAt(QPointF logical_pos) {
    if (!structure_) return;
    const QString id = hitTestRegion(logical_pos);
    if (selected_ != id) {
        selected_ = id;
        emit selectionChanged(selected_);
    }
    dragging_ = false;
    if (id.isEmpty()) {
        update();
        return;
    }
    const auto idx = regionIndexById(id);
    if (!idx) return;  // not reachable: hitTestRegion just found it
    drag_region_id_ = id;
    drag_orig_ = structure_->region(*idx);
    const QRectF pr = QRectF(toPixel(drag_orig_.x_min, drag_orig_.y_min),
                             toPixel(drag_orig_.x_max, drag_orig_.y_max))
                          .normalized();
    const bool near_right = std::abs(logical_pos.x() - pr.right()) <= kEdgeTolerancePx;
    const bool near_bottom = std::abs(logical_pos.y() - pr.bottom()) <= kEdgeTolerancePx;
    drag_mode_ = near_right ? DragMode::ResizeRight : (near_bottom ? DragMode::ResizeBottom : DragMode::Move);
    drag_start_mesh_ = toMeshSpace(logical_pos);
    dragging_ = true;
    update();
}

void StructureEditorView::dragTo(QPointF logical_pos) {
    if (!dragging_ || !structure_) return;
    const QPointF cur = toMeshSpace(logical_pos);
    const double dx = cur.x() - drag_start_mesh_.x();
    const double dy = cur.y() - drag_start_mesh_.y();
    RegionData r = drag_orig_;
    constexpr double kMinExtent = 1e-9;  // cm; never let a drag collapse a region to zero width/height
    if (drag_mode_ == DragMode::Move) {
        const double w = drag_orig_.x_max - drag_orig_.x_min, h = drag_orig_.y_max - drag_orig_.y_min;
        r.x_min = snapX(drag_orig_.x_min + dx);
        r.x_max = r.x_min + w;
        r.y_min = snapY(drag_orig_.y_min + dy);
        r.y_max = r.y_min + h;
    } else if (drag_mode_ == DragMode::ResizeRight) {
        r.x_max = snapX(std::max(drag_orig_.x_min + kMinExtent, drag_orig_.x_max + dx));
    } else if (drag_mode_ == DragMode::ResizeBottom) {
        r.y_max = snapY(std::max(drag_orig_.y_min + kMinExtent, drag_orig_.y_max + dy));
    }
    structure_->set_region(drag_region_id_.toStdString(), r);
    update();
}

void StructureEditorView::endDrag() {
    if (dragging_) emit regionEdited(drag_region_id_);
    dragging_ = false;
}

void StructureEditorView::mousePressEvent(QMouseEvent* e) { beginDragAt(e->position()); }

void StructureEditorView::mouseMoveEvent(QMouseEvent* e) {
    if (dragging_)
        dragTo(e->position());
    else
        hoverAt(e->position());
}

void StructureEditorView::mouseReleaseEvent(QMouseEvent*) { endDrag(); }

void StructureEditorView::leaveEvent(QEvent*) {
    readout_.clear();
    update();
}

void StructureEditorView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), theme::qcolor(theme::T::Background));
    if (!structure_) return;
    const QRectF dom = domainRect();

    p.setPen(theme::qcolor(theme::T::Border));
    p.drawRect(dom);

    // Mesh lines: only drawable for a computable ("uniform") axis -- see
    // the class comment on why "graded" is not ported here.
    if (mesh_ && mesh_->grading() == "uniform") {
        QPen meshPen(theme::qcolor(theme::T::Overlay));
        p.setPen(meshPen);
        for (double x : uniform_axis(structure_->width_cm(), mesh_->nx())) {
            const double px = toPixel(x, 0.0).x();
            p.drawLine(QPointF(px, dom.top()), QPointF(px, dom.bottom()));
        }
        for (double y : uniform_axis(structure_->height_cm(), mesh_->ny())) {
            const double py = toPixel(0.0, y).y();
            p.drawLine(QPointF(dom.left(), py), QPointF(dom.right(), py));
        }
    }

    // Regions: fill by net doping sign (donor/n-type vs acceptor/p-type),
    // a data colour, not a theme token (tokens.hpp's own convention for
    // data that "keeps its colours" -- theme::StructureColour here).
    QColor donorFill = theme::structureColour(theme::StructureColour::NType);
    donorFill.setAlpha(70);
    QColor acceptorFill = theme::structureColour(theme::StructureColour::PType);
    acceptorFill.setAlpha(70);
    for (std::size_t i = 0; i < structure_->region_count(); ++i) {
        const RegionData r = structure_->region(i);
        const QRectF pr = QRectF(toPixel(r.x_min, r.y_min), toPixel(r.x_max, r.y_max)).normalized();
        const double sign_value = (r.doping_profile == "uniform") ? r.net_doping_cm3
                                                                   : r.profile_peak_cm3.value_or(0.0);
        const bool donor = sign_value >= 0.0;
        p.fillRect(pr, donor ? donorFill : acceptorFill);
        const bool is_selected = (QString::fromStdString(r.id) == selected_);
        QPen pen(theme::qcolor(theme::T::Accent));
        if (!is_selected)
            pen = QPen(theme::structureColour(donor ? theme::StructureColour::NType
                                                     : theme::StructureColour::PType));
        pen.setWidthF(is_selected ? 2.0 : 1.5);
        p.setPen(pen);
        p.drawRect(pr);

        // Resize handles (section 26): a visible affordance for the drag-
        // resize this view already supports, at the 4 corners and the top/
        // bottom mid-edges (this view's own DragMode::ResizeRight/
        // ResizeBottom) -- drawn only for the selected region, matching
        // the mockup's white-fill/accent-border squares.
        if (is_selected) {
            constexpr double kHalf = 3.5;
            const QPointF pts[] = {pr.topLeft(), pr.topRight(), pr.bottomLeft(), pr.bottomRight(),
                                   {pr.center().x(), pr.top()}, {pr.center().x(), pr.bottom()}};
            p.setPen(QPen(theme::qcolor(theme::T::Accent), 1.5));
            p.setBrush(theme::qcolor(theme::T::Base));
            for (const QPointF& pt : pts)
                p.drawRect(QRectF(pt.x() - kHalf, pt.y() - kHalf, 2 * kHalf, 2 * kHalf));
            p.setBrush(Qt::NoBrush);
        }
    }

    // Contacts and gates: read-only overlay along their boundary edge
    // (S2's own scope note -- not draggable here, see the class comment).
    auto edge_segment = [&](const BoundaryData& b) -> QLineF {
        const double w = structure_->width_cm(), h = structure_->height_cm();
        if (b.edge == "left" || b.edge == "right") {
            const double x = (b.edge == "left") ? 0.0 : w;
            const double lo = b.range_lo.value_or(0.0), hi = b.range_hi.value_or(h);
            return QLineF(toPixel(x, lo), toPixel(x, hi));
        }
        const double y = (b.edge == "top") ? 0.0 : h;  // "top"/"bottom"; "front"/"back" (3D z-faces) not drawn in 2D
        const double lo = b.range_lo.value_or(0.0), hi = b.range_hi.value_or(w);
        return QLineF(toPixel(lo, y), toPixel(hi, y));
    };
    QPen contactPen(theme::structureColour(theme::StructureColour::Contact));
    contactPen.setWidth(4);
    p.setPen(contactPen);
    for (std::size_t i = 0; i < structure_->contact_count(); ++i)
        p.drawLine(edge_segment(structure_->contact(i).boundary));
    QPen gatePen(theme::structureColour(theme::StructureColour::Gate));
    gatePen.setWidth(4);
    p.setPen(gatePen);
    for (std::size_t i = 0; i < structure_->gate_count(); ++i)
        p.drawLine(edge_segment(structure_->gate(i).boundary));

    if (!readout_.isEmpty()) {
        p.setPen(theme::qcolor(theme::T::TextDim));
        p.drawText(QPointF(kMarginPx, height() - 6.0), readout_);
    }
}

}  // namespace tcad::desktop
