#include "field_view.hpp"

#include <QMouseEvent>
#include <QResizeEvent>
#include <vtkRenderWindowInteractor.h>

#include <memory>

namespace tcad::desktop {

// The scene's view of the widget.
class FieldView::QtHost : public SceneHost {
public:
    explicit QtHost(FieldView* v) : view_(v) {}
    double devicePixelRatio() const override { return view_->devicePixelRatioF(); }
    int logicalWidth() const override { return view_->width(); }
    bool glReady() const override { return view_->isValid(); }
    void requestRedraw() override { view_->update(); }

private:
    FieldView* view_;
};

FieldView::FieldView(QWidget* parent)
    : QVTKOpenGLNativeWidget(parent), host_impl_(std::make_unique<QtHost>(this)) {
    gl_window_ = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    setRenderWindow(gl_window_);  // also gives the window its interactor
    attach(gl_window_, host_impl_.get());
    on_display_changed = [this] { emit displayChanged(); };
    on_readout_changed = [this](const std::string& s) { emit readoutChanged(QString::fromStdString(s)); };
    setMouseTracking(true);
}

FieldView::~FieldView() = default;

bool FieldView::readoutAt(double x_px, double y_px, QString* text) {
    std::string s;
    const bool hit = readoutAtStr(x_px, y_px, &s);
    *text = QString::fromStdString(s);
    return hit;
}

QString FieldView::readoutForPoint(const double p_um[3]) const { return QString::fromStdString(readoutForPointStr(p_um)); }

QString FieldView::readoutForNode(std::size_t node) const { return QString::fromStdString(readoutForNodeStr(node)); }

bool FieldView::explodedAvailable(QString* why) const {
    std::string s;
    const bool ok = explodedAvailableStr(&s);
    if (why) *why = QString::fromStdString(s);
    return ok;
}

void FieldView::mouseMoveEvent(QMouseEvent* event) {
    QVTKOpenGLNativeWidget::mouseMoveEvent(event);
    handleMouseMove(event->position().x(), event->position().y(), event->buttons() != Qt::NoButton);
}

void FieldView::leaveEvent(QEvent* event) {
    QVTKOpenGLNativeWidget::leaveEvent(event);
    handleLeave();
}

void FieldView::resizeEvent(QResizeEvent* event) {
    QVTKOpenGLNativeWidget::resizeEvent(event);
    handleResize();
}

void FieldView::initializeGL() {
    noteGlInitialised();
    QVTKOpenGLNativeWidget::initializeGL();
}

}  // namespace tcad::desktop
