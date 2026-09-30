#include "native/vtk_win32_host.hpp"

#include <vtkCallbackCommand.h>
#include <vtkCommand.h>
#include <vtkOpenGLRenderWindow.h>
#include <vtkWin32OpenGLRenderWindow.h>
#include <vtkWin32RenderWindowInteractor.h>

#include <stdexcept>

namespace tcad::native {
namespace {

bool anyMouseButtonDown() {
    return (GetKeyState(VK_LBUTTON) | GetKeyState(VK_MBUTTON) | GetKeyState(VK_RBUTTON)) & 0x8000;
}

}  // namespace

VtkWin32Host::VtkWin32Host(HWND parent, int width_px, int height_px) : parent_(parent) {
    window_ = vtkSmartPointer<vtkWin32OpenGLRenderWindow>::New();
    window_->SetParentId(parent);  // VTK creates its own child window inside `parent`
    window_->SetPosition(0, 0);
    window_->SetSize(width_px, height_px);
    interactor_ = vtkSmartPointer<vtkWin32RenderWindowInteractor>::New();
    interactor_->SetRenderWindow(window_);
    interactor_->Initialize();  // creates the child window and the GL context
    if (!childWindow()) throw std::runtime_error("VTK did not create its render window (no HWND)");
    ready_ = true;
}

VtkWin32Host::~VtkWin32Host() {
    ready_ = false;
    if (window_) window_->Finalize();
}

vtkRenderWindow* VtkWin32Host::renderWindow() const { return window_; }

HWND VtkWin32Host::childWindow() const { return static_cast<HWND>(window_->GetGenericWindowId()); }

void VtkWin32Host::bind(tcad::desktop::FieldScene* scene) {
    scene_ = scene;
    auto onMouseMove = vtkSmartPointer<vtkCallbackCommand>::New();
    onMouseMove->SetClientData(this);
    onMouseMove->SetCallback([](vtkObject*, unsigned long, void* self_, void*) {
        auto* self = static_cast<VtkWin32Host*>(self_);
        if (!self->scene_) return;
        const int* pos = self->interactor_->GetEventPosition();  // device px, bottom-left origin
        const int* size = self->window_->GetSize();
        const double dpr = self->devicePixelRatio();
        self->scene_->handleMouseMove(pos[0] / dpr, (size[1] - 1 - pos[1]) / dpr, anyMouseButtonDown());
    });
    interactor_->AddObserver(vtkCommand::MouseMoveEvent, onMouseMove);

    auto onLeave = vtkSmartPointer<vtkCallbackCommand>::New();
    onLeave->SetClientData(this);
    onLeave->SetCallback([](vtkObject*, unsigned long, void* self_, void*) {
        auto* self = static_cast<VtkWin32Host*>(self_);
        if (self->scene_) self->scene_->handleLeave();
    });
    interactor_->AddObserver(vtkCommand::LeaveEvent, onLeave);

    auto onKey = vtkSmartPointer<vtkCallbackCommand>::New();
    onKey->SetClientData(this);
    onKey->SetCallback([](vtkObject*, unsigned long, void* self_, void*) {
        auto* self = static_cast<VtkWin32Host*>(self_);
        const std::string sym = self->interactor_->GetKeySym() ? self->interactor_->GetKeySym() : "";
        if (self->on_key) self->on_key(sym);
        if (self->on_key_mods)
            self->on_key_mods(sym, self->interactor_->GetControlKey() != 0, self->interactor_->GetShiftKey() != 0,
                              self->interactor_->GetAltKey() != 0);
    });
    interactor_->AddObserver(vtkCommand::KeyPressEvent, onKey);
}

void VtkWin32Host::resize(int width_px, int height_px) {
    if (width_px <= 0 || height_px <= 0) return;
    window_->SetSize(width_px, height_px);
    if (scene_) scene_->handleResize();
    window_->Render();
}

std::string VtkWin32Host::glCapabilities() const {
    const char* s = window_->ReportCapabilities();  // multi-line: vendor, renderer, version, ...
    return s ? s : "";
}

double VtkWin32Host::devicePixelRatio() const {
    const UINT dpi = parent_ ? GetDpiForWindow(parent_) : 96;
    return dpi ? dpi / 96.0 : 1.0;
}

int VtkWin32Host::logicalWidth() const {
    RECT r{};
    GetClientRect(parent_, &r);
    return static_cast<int>((r.right - r.left) / devicePixelRatio());
}

void VtkWin32Host::requestRedraw() {
    if (ready_) window_->Render();
}

}  // namespace tcad::native
