// Input routing of the native UI framework (N2c, NATIVE-DESKTOP-PLAN.md 27.7). Portable: the window (ui_window)
// feeds it N1's platform events with coordinates converted to window DIPs; everything that decides WHO gets an event
// lives here and is unit-tested without Win32.
//
// SCOPE -- what the Qt panels use, measured in desktop/src on 2026-09-30: overridden mouse press/move/release/
// double-click/wheel/leave handlers in the custom views (e.g. 7 mouseMoveEvent, 8 leaveEvent), setToolTip (28),
// QTimer (24), shortcuts on actions (8 setShortcut + 6 standard keys) and '&' mnemonics (~15). NOT used, so not built:
// setTabOrder, setFocusPolicy overrides, setCursor, keyPressEvent overrides, event filters. Drag and drop (3 QDrag)
// is left to a later slice.
//
// Mouse:  the deepest visible, enabled widget under the pointer gets the event, and it bubbles to the parent while
//         handlers return false. A press accepted by a widget GRABS the mouse (moves/release go to it, even outside it
//         or the window) until every button is up (Qt's implicit grab); hover is frozen during a grab. Moves are
//         delivered without a button too (Qt's mouse tracking, always on). A left or right press gives focus to the
//         first widget up the chain that takes Click focus; clicking elsewhere leaves focus alone.
// Hover:  hoverChanged(true/false) for every widget entering/leaving the chain under the pointer; the window leaving
//         clears it. A tooltip is asked for (on_tooltip) after the pointer rests on a widget with toolTip text for the
//         delay; any press, key or hover change hides it.
// Keys:   (1) the focus widget, if it overrides this key as a shortcut; (2) the window's shortcuts; (3) Tab/Shift+Tab
//         move focus through the Tab-focusable widgets in tree order (unless the focus widget wantsTab()); (4)
//         Alt+<letter> activates the next widget with that mnemonic; (5) the focus widget, bubbling to its parents.
//         Characters go to the focus widget only.
// Window: losing activation hides focus (hasFocus() is false, focusChanged(false, Window)) but remembers the focus
//         widget; activation restores it.
// Safety: a widget that dies or leaves the tree is forgotten at once (focus, hover, grab, tooltip), never
//         dereferenced later; focus on a widget that becomes hidden or disabled is dropped.
#pragma once

#include "platform/input.hpp"
#include "ui/core/widget.hpp"

#include <functional>
#include <vector>

namespace tcad::ui {

class InputRouter {
public:
    explicit InputRouter(Widget& root);

    void setTimers(TimerService* t) { timers_ = t; }
    void setDragThresholdPx(int px) { drag_threshold_px_ = px; }  // the system's SM_CXDRAG (4 on Windows)
    void setTooltipDelayMs(int ms) { tooltip_delay_ms_ = ms; }
    // Show a tooltip for the widget at the window-DIP point, or hide it (nullptr). The popup itself is N3's.
    std::function<void(Widget*, PointF)> on_tooltip;
    // Called for every press (before it is delivered) with the widget under the pointer; true swallows the press. An
    // open popup sets it to close itself and to keep the press that closed it from reaching whatever is under it when
    // that is the popup's opener (N3c). One at a time.
    // `owner` is the widget that set it: the filter goes when the owner does, and only the owner may clear it.
    void setPressFilter(Widget* owner, std::function<bool(Widget* hit)> f) {
        press_filter_owner_ = owner;
        press_filter_ = std::move(f);
    }
    void clearPressFilter(Widget* owner) {
        if (press_filter_owner_ == owner) press_filter_owner_ = nullptr, press_filter_ = nullptr;
    }
    bool hasPressFilter() const { return static_cast<bool>(press_filter_); }
    // The focus widget changed (null: none), while the window is active -- UI Automation's focus event (N2f).
    std::function<void(Widget*)> on_focus_changed;

    // Events; coordinates in WINDOW DIPs. Return true when some widget (or a shortcut) used it.
    bool mouse(const platform::MouseEvent& e);
    bool key(const platform::KeyEvent& e);
    bool character(char32_t c);
    void windowActivated(bool active);

    Widget* focusWidget() const { return focus_; }
    Widget* hoverWidget() const { return hover_.empty() ? nullptr : hover_.back(); }
    bool isHovered(const Widget* w) const;
    Widget* grabber() const { return grab_; }
    bool windowActive() const { return active_; }
    Cursor cursor() const;  // for the widget under the pointer (or the grabber); Arrow by default
    Widget* tooltipWidget() const { return tooltip_shown_; }
    // Keyboard cues (N3a): mnemonic underlines are hidden until Alt is first pressed in the window, as in Windows'
    // own controls, unless the system says to always show them (SPI_GETKEYBOARDCUES; the window sets it).
    bool mnemonicCuesVisible() const { return cues_ || always_cues_; }
    void setAlwaysShowCues(bool on) { always_cues_ = on; }

    void setFocus(Widget* w, FocusReason why);  // null clears
    bool focusNext(bool forward);               // Tab order; false when nothing can take focus
    platform::ShortcutMap& shortcuts() { return shortcuts_; }
    Widget* widgetAt(PointF window_dips) const;
    void widgetGone(Widget* w);  // w and its subtree
    void checkFocusStillValid();  // drop focus from a widget that became hidden or disabled
    void cancelGrab() { grab_ = nullptr, dragging_ = false; }  // the window lost the mouse capture
    ~InputRouter();

private:
    std::vector<Widget*> chainAt(PointF window_dips) const;  // root .. deepest
    void setHover(std::vector<Widget*> chain);
    bool deliverMouse(Widget* target, const platform::MouseEvent& e, PointF p, int clicks, Widget** accepted);
    void hideTooltip();
    void armTooltip();
    std::vector<Widget*> tabChain() const;
    // Widgets that died or left the tree during the current event: their pointers must not be touched again (a
    // handler may delete widgets), so liveness is looked up here instead of by walking the (freed) parent chain.
    bool alive(const Widget* w) const;
    void beginEvent() { dead_.clear(); }

    Widget& root_;
    TimerService* timers_ = nullptr;
    platform::ShortcutMap shortcuts_;
    Widget* focus_ = nullptr;
    Widget* grab_ = nullptr;
    std::vector<Widget*> hover_;
    bool active_ = true;
    PointF last_pos_;
    PointF press_pos_;
    bool dragging_ = false;
    int drag_threshold_px_ = 4;
    int tooltip_delay_ms_ = 700;
    TimerId tooltip_timer_ = 0;
    Widget* tooltip_target_ = nullptr;
    Widget* tooltip_shown_ = nullptr;
    std::vector<Widget*> dead_;
    std::function<bool(Widget*)> press_filter_;
    Widget* press_filter_owner_ = nullptr;
    bool cues_ = false;
    bool always_cues_ = false;
};

}  // namespace tcad::ui
