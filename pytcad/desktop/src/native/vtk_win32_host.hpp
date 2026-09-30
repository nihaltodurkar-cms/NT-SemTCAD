// The Win32 host of a FieldScene's VTK render window (NATIVE-DESKTOP-PLAN.md section 27.4).
// VTK draws with OpenGL into a child HWND (vtkWin32OpenGLRenderWindow, part of
// RenderingOpenGL2 -- not vtkGUISupportQt); vtkWin32RenderWindowInteractor turns that
// window's messages into VTK events. NO Qt anywhere: this is what replaces
// QVTKOpenGLNativeWidget.
#pragma once

#include "views/field/field_scene.hpp"

#include <windows.h>

#include <functional>
#include <string>

#include <vtkSmartPointer.h>

class vtkWin32OpenGLRenderWindow;
class vtkWin32RenderWindowInteractor;

namespace tcad::native {

class VtkWin32Host final : public tcad::desktop::SceneHost {
public:
    // Creates the child window inside `parent`, sized width_px x height_px, and its GL context.
    // Throws std::runtime_error when the window or the context cannot be created.
    VtkWin32Host(HWND parent, int width_px, int height_px);
    ~VtkWin32Host() override;

    vtkRenderWindow* renderWindow() const;
    HWND childWindow() const;
    // Route the interactor's mouse-move / leave / key events into the scene and on_key.
    void bind(tcad::desktop::FieldScene* scene);
    void resize(int width_px, int height_px);  // device pixels
    std::string glCapabilities() const;  // VTK's report: GL vendor / renderer / version

    // key symbol as VTK names it ("f", "l", "Escape", ...)
    std::function<void(const std::string&)> on_key;
    // The same event with the modifier state (N1: the workspace routes it as a shortcut). Both fire.
    std::function<void(const std::string& keysym, bool ctrl, bool shift, bool alt)> on_key_mods;

    // SceneHost
    double devicePixelRatio() const override;
    int logicalWidth() const override;
    bool glReady() const override { return ready_; }
    void requestRedraw() override;

private:
    HWND parent_ = nullptr;
    vtkSmartPointer<vtkWin32OpenGLRenderWindow> window_;
    vtkSmartPointer<vtkWin32RenderWindowInteractor> interactor_;
    tcad::desktop::FieldScene* scene_ = nullptr;
    bool ready_ = false;
};

}  // namespace tcad::native
