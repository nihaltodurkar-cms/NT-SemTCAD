// FieldView: the LEGACY Qt Widgets host of FieldScene (NATIVE-DESKTOP-PLAN.md
// sections 5.1, 15, 27). All the display pipeline is in the Qt-free FieldScene
// (field_scene.hpp); this class only gives it a QVTKOpenGLNativeWidget surface,
// turns its callbacks into Qt signals and forwards mouse/resize events. It exists
// only while the Qt panels are being replaced (section 27) and goes with them.
//
// Every FieldScene method is inherited unchanged (setResult, setField, setLogScale, ...).
// The four that took or returned a QString keep their old names here as thin wrappers
// over the std::string versions (readoutAtStr, readoutForPointStr, ...).
#pragma once

#include "field_scene.hpp"

#include <QString>
#include <QVTKOpenGLNativeWidget.h>

#include <memory>

namespace tcad::desktop {

class FieldView : public QVTKOpenGLNativeWidget, public FieldScene {
    Q_OBJECT

public:
    explicit FieldView(QWidget* parent = nullptr);
    ~FieldView() override;

    // Hover readout at widget (logical, top-left origin) pixel coords.
    bool readoutAt(double x_px, double y_px, QString* text);
    QString readoutForPoint(const double p_um[3]) const;
    QString readoutForNode(std::size_t node) const;
    // Why not, when the result has no usable regions.
    bool explodedAvailable(QString* why = nullptr) const;

signals:
    void readoutChanged(const QString& text);
    void displayChanged();  // field, scale, colour map, range or overlays changed

protected:
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void initializeGL() override;

private:
    class QtHost;
    std::unique_ptr<QtHost> host_impl_;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> gl_window_;
};

}  // namespace tcad::desktop
