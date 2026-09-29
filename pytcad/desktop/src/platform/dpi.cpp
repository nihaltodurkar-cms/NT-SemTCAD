#include "platform/dpi.hpp"

#include <algorithm>

namespace tcad::platform {

WorkspaceLayout computeLayout(int client_w_px, int client_h_px, double scale, double status_height_logical) {
    WorkspaceLayout l;
    const int w = std::max(0, client_w_px), h = std::max(0, client_h_px);
    const int status_h = std::clamp(toDevice(std::max(0.0, status_height_logical), scale), 0, h);
    l.client = {0, 0, w, h};
    l.content = {0, 0, w, h - status_h};
    l.status = {0, h - status_h, w, status_h};
    return l;
}

}  // namespace tcad::platform
