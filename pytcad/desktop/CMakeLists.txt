# PyTCAD native desktop application (NATIVE-DESKTOP-PLAN.md).
#
# A SEPARATE CMake project from core/ on purpose: core/ builds the
# MinGW `_core` extension for the tcad-dev Python; this builds MSVC
# executables against VTK (and, while TCAD_LEGACY_QT is ON, Qt 6) from the separate
# `tcad-gui` conda env. NATIVE-DESKTOP-PLAN.md section 27: Qt is being purged; the
# Win32 + VTK spike (tcad_native_spike) and everything it links are Qt-free, and
# build with -DTCAD_LEGACY_QT=OFF, which needs no Qt, no ADS, no vtkGUISupportQt.
# The two never link each other -- the app talks to the solver through
# the job-file/.npz process boundary only (plan section 3).
#
# Build with desktop/build.ps1 (sets up MSVC, finds tcad-gui). By hand:
#   (in an MSVC x64 developer shell)
#   cmake -S desktop -B build/desktop -G Ninja -DCMAKE_BUILD_TYPE=Release
#         -DCMAKE_PREFIX_PATH=<tcad-gui>/Library
#   cmake --build build/desktop
cmake_minimum_required(VERSION 3.24)
project(tcad_desktop VERSION 0.1.0 LANGUAGES CXX)

if(NOT MSVC)
  message(FATAL_ERROR "tcad_desktop is built with MSVC: Qt and VTK from conda-forge are "
                      "MSVC-ABI (NATIVE-DESKTOP-PLAN.md section 7.1). Use desktop/build.ps1.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(/W4 /permissive- /utf-8 /EHsc)
add_compile_definitions(NOMINMAX UNICODE _UNICODE)

# The legacy Qt Widgets app and its tests (section 27). ON until the panels are
# replaced; OFF builds only the Qt-free targets.
option(TCAD_LEGACY_QT "Build the legacy Qt Widgets app, its Qt tests and the Qt contract tools" ON)

find_package(nlohmann_json 3 REQUIRED)
find_package(ZLIB)
# VTK WITHOUT its Qt modules: vtkGUISupportQt / vtkRenderingQt are the only VTK
# modules that import Qt6 (their PE import tables, section 27.1). RenderingUI has
# the Win32 interactor, IOImage the PNG writer used by the spike's screenshot.
find_package(VTK 9 REQUIRED COMPONENTS
  CommonCore CommonDataModel CommonColor
  FiltersCore FiltersGeometry
  FiltersExtraction FiltersFlowPaths FiltersModeling FiltersSources
  RenderingCore RenderingOpenGL2 RenderingAnnotation RenderingFreeType
  RenderingVolume RenderingVolumeOpenGL2
  RenderingUI InteractionStyle IOImage)
set(TCAD_VTK_LIBS ${VTK_LIBRARIES})
if(TCAD_LEGACY_QT)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets Test)
  # Qt Advanced Docking System (LGPL-2.1, section 7.3): conda-forge
  # qt6-advanced-docking-system 5.1.1, built against qt6-main 6.11 -- the
  # two are pinned together in build.ps1 / README (section 15.9).
  find_package(qtadvanceddocking-qt6 5.1 REQUIRED)
  find_package(VTK 9 REQUIRED COMPONENTS GUISupportQt)
  set(TCAD_VTK_LEGACY_LIBS ${TCAD_VTK_LIBS} VTK::GUISupportQt)
endif()

# -- Qt-free data layer: .npz reader, result model, DeviceSpec document ----
add_library(tcad_desktop_data STATIC
  src/data/npz.cpp
  src/data/pyjson.cpp
  src/data/max_n_locator.cpp
  src/data/contour_levels.cpp
  src/data/line_cut.cpp
  src/data/result_model.cpp
  src/data/physics_lab_data.cpp
  src/document/device_spec_document.cpp
  src/document/structure_document.cpp
  src/document/mesh_document.cpp
  src/document/process_document.cpp
  src/document/validation.cpp)
target_include_directories(tcad_desktop_data PUBLIC src)
target_link_libraries(tcad_desktop_data PUBLIC nlohmann_json::nlohmann_json)
if(ZLIB_FOUND)
  target_link_libraries(tcad_desktop_data PRIVATE ZLIB::ZLIB)
  target_compile_definitions(tcad_desktop_data PRIVATE TCAD_HAVE_ZLIB=1)
endif()

# -- contract tools driven by the Python suite (gui/tests/test_desktop_contracts.py)
if(TCAD_LEGACY_QT)  # uses QCryptographicHash: goes with the Qt purge (a std hash replaces it)
  add_executable(tcad_npz_dump src/tools/npz_dump.cpp)
  target_link_libraries(tcad_npz_dump PRIVATE tcad_desktop_data Qt6::Core psapi)
endif()
add_executable(tcad_theme_dump src/tools/theme_dump.cpp)
target_include_directories(tcad_theme_dump PRIVATE src)
target_link_libraries(tcad_theme_dump PRIVATE tcad_desktop_data)
add_executable(tcad_spec_roundtrip src/tools/spec_roundtrip.cpp)
target_link_libraries(tcad_spec_roundtrip PRIVATE tcad_desktop_data)
add_executable(tcad_structure_roundtrip src/tools/structure_roundtrip.cpp)
target_link_libraries(tcad_structure_roundtrip PRIVATE tcad_desktop_data)

# -- PlotView's Qt-free axis maths (P2-S2): matplotlib's locators and
#    formatters, contract-tested by gui/tests/test_desktop_plot_ticks.py
add_library(tcad_desktop_plotmath STATIC src/views/plot/axis_ticks.cpp src/views/plot/plot_geometry.cpp)
target_include_directories(tcad_desktop_plotmath PUBLIC src)
target_link_libraries(tcad_desktop_plotmath PUBLIC tcad_desktop_data)
add_executable(tcad_plot_ticks src/tools/plot_ticks.cpp)
target_link_libraries(tcad_plot_ticks PRIVATE tcad_desktop_plotmath)

# -- FieldScene: the field display pipeline, Qt-free (section 27) --------------
# Shared by the legacy Qt wrapper (FieldView) and the native Win32 host.
add_library(tcad_field_scene STATIC
  src/views/field/field_scene.cpp src/views/field/field_scene.hpp
  src/views/field/field_scene_3d.cpp
  src/views/colormaps.cpp src/views/colormaps.hpp)
target_include_directories(tcad_field_scene PUBLIC src)
target_link_libraries(tcad_field_scene PUBLIC tcad_desktop_data ${TCAD_VTK_LIBS})
vtk_module_autoinit(TARGETS tcad_field_scene MODULES ${TCAD_VTK_LIBS})

# -- the native Win32 spike (section 27.4): NO Qt, no ADS, no vtkGUISupportQt ---
# C++23 for the NEW native code only; the shared libraries stay C++20.
add_executable(tcad_native_spike
  src/native/spike_main.cpp
  src/native/win32_window.cpp src/native/win32_window.hpp
  src/native/vtk_win32_host.cpp src/native/vtk_win32_host.hpp
  src/native/tcad_native.manifest)
target_compile_features(tcad_native_spike PRIVATE cxx_std_23)
target_link_libraries(tcad_native_spike PRIVATE tcad_field_scene tcad_desktop_data ${TCAD_VTK_LIBS} user32 gdi32 psapi)
vtk_module_autoinit(TARGETS tcad_native_spike MODULES ${TCAD_VTK_LIBS})

# -- N1: the native platform layer and application (section 27.5) -- NO Qt ------
# Portable core (no Win32, no VTK): the JSON-RPC session, backend/PATH configuration, settings, DPI/layout,
# input/shortcuts. Unit-tested on any platform (tests/platform/, run by ctest here and by
# gui/tests/test_desktop_platform_core.py with g++/clang++ off Windows).
add_library(tcad_platform_core STATIC
  src/platform/dpi.cpp src/platform/dpi.hpp
  src/platform/input.cpp src/platform/input.hpp
  src/platform/env_path.cpp src/platform/env_path.hpp
  src/platform/settings.cpp src/platform/settings.hpp
  src/platform/rpc_session.cpp src/platform/rpc_session.hpp)
target_include_directories(tcad_platform_core PUBLIC src)
target_link_libraries(tcad_platform_core PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(tcad_platform_core PUBLIC cxx_std_23)
enable_testing()
add_executable(tcad_platform_core_tests tests/platform/test_platform_core.cpp)
target_link_libraries(tcad_platform_core_tests PRIVATE tcad_platform_core)
add_test(NAME tcad_platform_core_tests COMMAND tcad_platform_core_tests)
# Win32 side: message loop / timers / post, child process, window with input+DPI, workspace, file dialogs,
# the backend client's transport.
add_library(tcad_platform_win32 STATIC
  src/platform/win32_util.cpp src/platform/win32_util.hpp
  src/platform/app.cpp src/platform/app.hpp
  src/platform/process.cpp src/platform/process.hpp
  src/platform/window.cpp src/platform/window.hpp
  src/platform/workspace.cpp src/platform/workspace.hpp
  src/platform/file_dialog.cpp src/platform/file_dialog.hpp
  src/platform/backend_client.cpp src/platform/backend_client.hpp)
target_link_libraries(tcad_platform_win32 PUBLIC tcad_platform_core user32 gdi32 shell32 ole32 uuid)
# tcad_native: one minimal native window hosting the existing FieldScene and talking to the existing backend
add_executable(tcad_native
  src/native/app_main.cpp
  src/native/app_support.cpp src/native/app_support.hpp
  src/native/vtk_win32_host.cpp src/native/vtk_win32_host.hpp
  src/native/tcad_native.manifest)
target_link_libraries(tcad_native PRIVATE tcad_platform_win32 tcad_field_scene tcad_desktop_data ${TCAD_VTK_LIBS} psapi)
vtk_module_autoinit(TARGETS tcad_native MODULES ${TCAD_VTK_LIBS})

# -- N2b: the UI framework's portable core (section 27.7) -- NO Qt, NO Win32 -----
# Widget tree, layouts (Box/Form/Grid/Stack: the measured Qt subset), painter interface, recording painter, style.
add_library(tcad_ui_core STATIC
  src/ui/core/geometry.hpp src/ui/core/painter.hpp src/ui/core/style.hpp
  src/ui/core/recording_painter.cpp src/ui/core/recording_painter.hpp
  src/ui/core/widget.cpp src/ui/core/widget.hpp
  src/ui/core/layout.cpp src/ui/core/layout.hpp
  src/ui/core/input_router.cpp src/ui/core/input_router.hpp
  src/ui/core/edit_model.cpp src/ui/core/edit_model.hpp src/ui/core/keys.hpp
  # N3a (NATIVE-DESKTOP-PLAN.md 27.8): the portable widget set
  src/ui/widgets/mnemonic.cpp src/ui/widgets/mnemonic.hpp
  src/ui/widgets/label.cpp src/ui/widgets/label.hpp
  src/ui/widgets/button.cpp src/ui/widgets/button.hpp
  src/ui/widgets/group_box.cpp src/ui/widgets/group_box.hpp
  src/ui/widgets/numeric.cpp src/ui/widgets/numeric.hpp
  src/ui/widgets/slider.cpp src/ui/widgets/slider.hpp
  src/ui/widgets/combo_box.cpp src/ui/widgets/combo_box.hpp src/ui/core/popup.hpp)
target_include_directories(tcad_ui_core PUBLIC src)
target_compile_features(tcad_ui_core PUBLIC cxx_std_23)
target_link_libraries(tcad_ui_core PUBLIC tcad_platform_core)  # N2c: platform/input.hpp (portable)
add_executable(tcad_ui_core_tests tests/ui/test_ui_core.cpp tests/ui/test_ui_input.cpp tests/ui/test_ui_edit.cpp
  tests/ui/test_ui_widgets.cpp tests/ui/test_ui_numbers.cpp tests/ui/test_ui_combo.cpp)
target_include_directories(tcad_ui_core_tests PRIVATE tests/platform)
target_link_libraries(tcad_ui_core_tests PRIVATE tcad_ui_core)
add_test(NAME tcad_ui_core_tests COMMAND tcad_ui_core_tests)

# -- N2a: the UI framework's render core (section 27.7) -- NO Qt ----------------
# D3D12 device + queue, D3D11On12, Direct2D/DirectWrite into a DXGI flip-model swap chain per window, WIC readback.
add_library(tcad_ui_render STATIC
  src/ui/render/render_device.cpp src/ui/render/render_device.hpp
  src/ui/render/window_surface.cpp src/ui/render/window_surface.hpp
  src/ui/render/image.cpp src/ui/render/image.hpp)
target_include_directories(tcad_ui_render PUBLIC src)
target_compile_features(tcad_ui_render PUBLIC cxx_std_23)
target_link_libraries(tcad_ui_render PUBLIC d3d12 d3d11 dxgi dxguid d2d1 dwrite windowscodecs ole32)
# N2b, Win32 side: Direct2D painter, DirectWrite text engine, UiWindow (a platform window + surface + widget tree).
add_library(tcad_ui_win32 STATIC
  src/ui/win32/dwrite_text.cpp src/ui/win32/dwrite_text.hpp
  src/ui/win32/d2d_painter.cpp src/ui/win32/d2d_painter.hpp
  src/ui/win32/ui_window.cpp src/ui/win32/ui_window.hpp
  src/ui/win32/clipboard.cpp src/ui/win32/clipboard.hpp
  src/ui/win32/tsf_text_store.cpp src/ui/win32/tsf_text_store.hpp
  src/ui/win32/line_edit.cpp src/ui/win32/line_edit.hpp
  src/ui/win32/spin_box.cpp src/ui/win32/spin_box.hpp
  src/ui/win32/popup_window.cpp src/ui/win32/popup_window.hpp
  src/ui/win32/uia_provider.cpp src/ui/win32/uia_provider.hpp)
target_link_libraries(tcad_ui_win32 PUBLIC tcad_ui_core tcad_ui_render tcad_platform_win32 user32 ole32 oleaut32 uiautomationcore)
# The gate: goldens (WARP, exact) at 100/150/200% for the N2a scene and the N2b panel, device loss, resize/DPI churn,
# debug layer, GPU smoke, display list vs pixels, invalidation/layout coalescing, WM_PAINT.
add_executable(tcad_ui_render_tests
  tests/ui/test_ui_render.cpp tests/ui/test_ui_panel.cpp tests/ui/test_ui_input_win32.cpp tests/ui/ui_driver.hpp
  tests/ui/test_ui_text.cpp tests/ui/test_ui_edit_win32.cpp tests/ui/test_ui_a11y.cpp tests/ui/test_ui_widgets_win32.cpp tests/ui/test_ui_numbers_win32.cpp tests/ui/test_ui_combo_win32.cpp
  tests/ui/render_scene.cpp tests/ui/render_scene.hpp
  tests/ui/render_test_support.cpp tests/ui/render_test_support.hpp
  src/native/tcad_native.manifest)
target_include_directories(tcad_ui_render_tests PRIVATE tests/platform tests/ui)
target_link_libraries(tcad_ui_render_tests PRIVATE tcad_ui_win32 nlohmann_json::nlohmann_json psapi version dbghelp)
target_compile_definitions(tcad_ui_render_tests PRIVATE TCAD_UI_GOLDENS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/goldens")
add_test(NAME tcad_ui_render_tests COMMAND tcad_ui_render_tests)
# A visible window for looking at the render core (F5 = simulated device loss); not a gate.
add_executable(tcad_ui_demo
  tests/ui/ui_demo_main.cpp tests/ui/render_scene.cpp tests/ui/render_scene.hpp
  tests/ui/ui_gallery.cpp tests/ui/ui_gallery.hpp
  src/native/tcad_native.manifest)
target_include_directories(tcad_ui_demo PRIVATE tests/ui)
target_link_libraries(tcad_ui_demo PRIVATE tcad_ui_win32)

if(TCAD_LEGACY_QT)
# -- the application ----------------------------------------------------------
set(CMAKE_AUTOMOC ON)
# The client for the Python backend service (P1 S4): Qt Core only.
add_library(tcad_desktop_backend STATIC
  src/backend/backend_client.cpp src/backend/backend_client.hpp)
target_include_directories(tcad_desktop_backend PUBLIC src)
target_link_libraries(tcad_desktop_backend PUBLIC Qt6::Core nlohmann_json::nlohmann_json)
# The local solver runner (P3-S3): Qt Core only.
add_library(tcad_desktop_run STATIC src/run/runner_base.hpp
  src/run/job_runner.cpp src/run/job_runner.hpp
  src/run/run_controller.cpp src/run/run_controller.hpp
  src/run/telemetry.cpp src/run/telemetry.hpp
  src/run/batch_controller.cpp src/run/batch_controller.hpp
  src/run/study_controller.cpp src/run/study_controller.hpp
  src/run/remote_host.cpp src/run/remote_host.hpp
  src/run/remote_job_runner.cpp src/run/remote_job_runner.hpp
  src/run/project_controller.cpp src/run/project_controller.hpp
  src/run/compact_model_controller.cpp src/run/compact_model_controller.hpp)
target_include_directories(tcad_desktop_run PUBLIC src)
target_link_libraries(tcad_desktop_run PUBLIC tcad_desktop_backend tcad_desktop_data Qt6::Core nlohmann_json::nlohmann_json)

# The shell and views as a library (P1 S3): the app, and the shell e2e
# tests (tcad_desktop_shell_tests), link ONE copy of this code.
add_library(tcad_desktop_ui STATIC
  src/shell/main_window.cpp src/shell/main_window.hpp
  src/shell/app_settings.cpp src/shell/app_settings.hpp
  src/shell/info_panel.cpp src/shell/info_panel.hpp
  src/shell/display_panel.cpp src/shell/display_panel.hpp
  src/shell/view3d_panel.cpp src/shell/view3d_panel.hpp
  src/shell/playback_panel.cpp src/shell/playback_panel.hpp
  src/shell/plot_panel.cpp src/shell/plot_panel.hpp
  src/shell/run_panel.cpp src/shell/run_panel.hpp
  src/shell/console_panel.cpp src/shell/console_panel.hpp
  src/shell/telemetry_panel.cpp src/shell/telemetry_panel.hpp
  src/shell/study_panel.cpp src/shell/study_panel.hpp
  src/editors/mesh_editor.cpp src/editors/mesh_editor.hpp
  src/editors/structure_editor_view.cpp src/editors/structure_editor_view.hpp
  src/editors/region_list_widget.cpp src/editors/region_list_widget.hpp
  src/editors/doping_editor.cpp src/editors/doping_editor.hpp
  src/editors/contact_list_widget.cpp src/editors/contact_list_widget.hpp
  src/editors/gate_list_widget.cpp src/editors/gate_list_widget.hpp
  src/editors/contact_editor.cpp src/editors/contact_editor.hpp
  src/editors/gate_editor.cpp src/editors/gate_editor.hpp
  src/editors/process_step_list_widget.cpp src/editors/process_step_list_widget.hpp
  src/editors/substrate_step_editor.cpp src/editors/substrate_step_editor.hpp
  src/editors/implant_step_editor.cpp src/editors/implant_step_editor.hpp
  src/editors/anneal_step_editor.cpp src/editors/anneal_step_editor.hpp
  src/editors/oxidize_step_editor.cpp src/editors/oxidize_step_editor.hpp
  src/shell/validation_panel.cpp src/shell/validation_panel.hpp
  src/shell/build_panel.cpp src/shell/build_panel.hpp
  src/shell/catalog_panel.cpp src/shell/catalog_panel.hpp
  src/shell/compact_model_panel.cpp src/shell/compact_model_panel.hpp
  src/views/field/field_view.cpp src/views/field/field_view.hpp
  src/views/plot/plot_view.cpp src/views/plot/plot_view.hpp src/views/plot/plot_model.hpp
  src/views/plot/curve_modes.cpp src/views/plot/curve_modes.hpp
  src/theme/theme.cpp src/theme/theme.hpp src/theme/tokens.hpp
  src/bench/bench.cpp
  src/bench/selftest.cpp
  src/bench/bench_backend.cpp
  src/bench/bench_run.cpp)
target_include_directories(tcad_desktop_ui PUBLIC src)
target_link_libraries(tcad_desktop_ui PUBLIC
  tcad_field_scene tcad_desktop_data tcad_desktop_plotmath tcad_desktop_backend tcad_desktop_run Qt6::Widgets Qt6::OpenGLWidgets ads::qtadvanceddocking-qt6
  ${TCAD_VTK_LEGACY_LIBS} psapi)
vtk_module_autoinit(TARGETS tcad_desktop_ui MODULES ${TCAD_VTK_LEGACY_LIBS})

add_executable(tcad_desktop src/app/main.cpp)
target_link_libraries(tcad_desktop PRIVATE tcad_desktop_ui)
vtk_module_autoinit(TARGETS tcad_desktop MODULES ${TCAD_VTK_LEGACY_LIBS})

# -- unit tests -----------------------------------------------------------------
enable_testing()
add_executable(tcad_desktop_unit_tests tests/test_data.cpp)
target_link_libraries(tcad_desktop_unit_tests PRIVATE tcad_desktop_data Qt6::Test)
add_test(NAME tcad_desktop_unit_tests COMMAND tcad_desktop_unit_tests)

# Shell end-to-end tests (section 15.13, S3b/S3f): the REAL MainWindow in
# a real window, driven with QTest. Needs a GL surface and test data, so
# it is run by gui/tests/test_desktop_shell.py, not by ctest.
# Backend client tests (section 15.15, S4): the real service plus fake
# ones; run by gui/tests/test_desktop_backend.py.
add_executable(tcad_desktop_backend_tests tests/test_backend.cpp)
target_link_libraries(tcad_desktop_backend_tests PRIVATE tcad_desktop_backend tcad_desktop_data Qt6::Test)

# JobRunner tests (P3-S3): the real solver_runner plus fake solvers; run
# by gui/tests/test_desktop_run.py.
add_executable(tcad_desktop_run_tests tests/test_run.cpp)
target_link_libraries(tcad_desktop_run_tests PRIVATE tcad_desktop_run tcad_desktop_data Qt6::Test)

# RemoteJobRunner tests (P3-S8): against gui/tests/fixtures/fake_ssh.py/
# fake_scp.py, the same loopback stand-ins the Python remote tests use;
# run by gui/tests/test_desktop_remote_job_runner.py.
add_executable(tcad_desktop_remote_tests tests/test_remote_job_runner.cpp)
target_link_libraries(tcad_desktop_remote_tests PRIVATE tcad_desktop_run tcad_desktop_data Qt6::Test)

# ProjectController gates (P4 S9, plan section 20.4): the real backend's
# project.load/project.save; run by gui/tests/test_desktop_project.py.
add_executable(tcad_desktop_project_tests tests/test_project.cpp)
target_link_libraries(tcad_desktop_project_tests PRIVATE tcad_desktop_run tcad_desktop_data Qt6::Test)

# PlotView gates (P2-S2): the real widget rendered offscreen with grab(),
# probed; run by gui/tests/test_desktop_plot.py at display scales 1/1.5/2.
add_executable(tcad_desktop_plot_tests tests/test_plot.cpp)
target_link_libraries(tcad_desktop_plot_tests PRIVATE tcad_desktop_ui Qt6::Test)
vtk_module_autoinit(TARGETS tcad_desktop_plot_tests MODULES ${TCAD_VTK_LEGACY_LIBS})

# Run dock / Console / Run-Stop end to end (P3-S4): the real window, the real
# backend and solver; run by gui/tests/test_desktop_run_shell.py.
add_executable(tcad_desktop_run_shell_tests tests/test_run_shell.cpp)
target_link_libraries(tcad_desktop_run_shell_tests PRIVATE tcad_desktop_ui Qt6::Test)
vtk_module_autoinit(TARGETS tcad_desktop_run_shell_tests MODULES ${TCAD_VTK_LEGACY_LIBS})

add_executable(tcad_desktop_shell_tests tests/test_shell.cpp)
target_link_libraries(tcad_desktop_shell_tests PRIVATE tcad_desktop_ui Qt6::Test)
vtk_module_autoinit(TARGETS tcad_desktop_shell_tests MODULES ${TCAD_VTK_LEGACY_LIBS})

# Device Builder shell assembly gates (P4 section 21, plan section 20.4):
# the real MainWindow's Build dock against the real backend; run by
# gui/tests/test_desktop_build_shell.py.
add_executable(tcad_desktop_build_shell_tests tests/test_build_shell.cpp)
target_link_libraries(tcad_desktop_build_shell_tests PRIVATE tcad_desktop_ui Qt6::Test)
vtk_module_autoinit(TARGETS tcad_desktop_build_shell_tests MODULES ${TCAD_VTK_LEGACY_LIBS})

# Compact Model dock gates (section 25): the real window's Compact Model
# dock against a real `gui.services.compact_runner` subprocess; run by
# gui/tests/test_desktop_compact_model_shell.py.
add_executable(tcad_desktop_compact_model_shell_tests tests/test_compact_model_shell.cpp)
target_link_libraries(tcad_desktop_compact_model_shell_tests PRIVATE tcad_desktop_ui Qt6::Test)
vtk_module_autoinit(TARGETS tcad_desktop_compact_model_shell_tests MODULES ${TCAD_VTK_LEGACY_LIBS})

# MeshEditor / StructureEditorView gates (P4 S2, plan section 20.4): the
# real widgets, driven through their testable methods; run by
# gui/tests/test_desktop_editors.py.
add_executable(tcad_desktop_editor_tests tests/test_editors.cpp)
target_link_libraries(tcad_desktop_editor_tests PRIVATE tcad_desktop_ui Qt6::Test)
vtk_module_autoinit(TARGETS tcad_desktop_editor_tests MODULES ${TCAD_VTK_LEGACY_LIBS})

# UndoStack gates (P4 S8, plan section 20.4): Qt-free logic, tested with
# QtTest for convenience only (no widget/window needed); run by
# gui/tests/test_desktop_undo.py.
add_executable(tcad_desktop_undo_tests tests/test_undo.cpp)
target_link_libraries(tcad_desktop_undo_tests PRIVATE tcad_desktop_data Qt6::Test)

# ValidationPanel gates (P4 S8, plan section 20.4): the real widget plus
# the Qt-free error-formatting logic; run by
# gui/tests/test_desktop_validation.py.
add_executable(tcad_desktop_validation_tests tests/test_validation.cpp)
target_link_libraries(tcad_desktop_validation_tests PRIVATE tcad_desktop_ui Qt6::Test)
vtk_module_autoinit(TARGETS tcad_desktop_validation_tests MODULES ${TCAD_VTK_LEGACY_LIBS})

endif()  # TCAD_LEGACY_QT

# -- runtime manifest: where the Qt/VTK DLLs live, for the Python contract
#    tests and the dev launcher (no DLLs are copied in dev builds) ----------
# VTK's DLL directory: conda-forge puts every VTK DLL, and a copy of every
# Qt DLL, in <env>/Library/bin (Qt6::Core's own location, lib/qt6/bin,
# has no VTK).
get_target_property(_vtk_dll VTK::CommonCore IMPORTED_LOCATION_RELEASE)
if(NOT _vtk_dll)
  get_target_property(_vtk_dll VTK::CommonCore IMPORTED_LOCATION)
endif()
get_filename_component(TCAD_RUNTIME_BIN "${_vtk_dll}" DIRECTORY)
file(TO_NATIVE_PATH "${TCAD_RUNTIME_BIN}" TCAD_RUNTIME_BIN_NATIVE)
string(REPLACE "\\" "\\\\" TCAD_RUNTIME_BIN_JSON "${TCAD_RUNTIME_BIN_NATIVE}")
# The backend service's interpreter (the tcad-dev env; build.ps1 finds it)
# and its root (pytcad/, backend_service's parent) -- read by
# resolveBackendConfig() in dev builds (section 15.15).
set(TCAD_BACKEND_PYTHON "" CACHE FILEPATH "Python interpreter that runs backend_service")
file(TO_NATIVE_PATH "${TCAD_BACKEND_PYTHON}" _py_native)
string(REPLACE "\\" "\\\\" TCAD_BACKEND_PYTHON_JSON "${_py_native}")
get_filename_component(_backend_root "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
file(TO_NATIVE_PATH "${_backend_root}" _root_native)
string(REPLACE "\\" "\\\\" TCAD_BACKEND_ROOT_JSON "${_root_native}")
configure_file(desktop_runtime.json.in "${CMAKE_BINARY_DIR}/desktop_runtime.json" @ONLY)
configure_file(tcad_desktop.cmd.in "${CMAKE_BINARY_DIR}/tcad_desktop.cmd" @ONLY)
