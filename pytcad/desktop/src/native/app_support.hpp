// Helpers of the native application (N1): the process's own module scan (the runtime no-Qt proof) and the frame
// capture used by the self-test. Same logic as the N0 spike's (spike_main.cpp keeps its own copy: the spike is a
// permanent regression and is not touched).
#pragma once

#include <vtkRenderWindow.h>

#include <string>
#include <vector>

namespace tcad::native {

// File names of every module loaded in this process, lower-cased.
std::vector<std::string> loadedModuleNames();
// Qt5/Qt6 DLLs, ADS, VTK's Qt modules, the Qt platform plugin.
bool isQtModule(const std::string& lower_module_name);

struct ImageStats {
    int width = 0, height = 0;
    std::size_t distinct_colours = 0;
    double non_background_fraction = 0;
};
// Read the render window's back buffer as RGB, write it as PNG (`png` may be empty) and measure it.
bool captureAndMeasure(vtkRenderWindow* rw, const std::string& png, ImageStats* stats);

}  // namespace tcad::native
