// The widget tree of the native UI framework (N2b, NATIVE-DESKTOP-PLAN.md 27.7). Portable: no Win32.
//
// It mirrors the parts of QWidget the ported panels need, and nothing more:
//   * a parent owns its children (unique_ptr); `addChild<T>(...)` creates one, `adopt()` takes one over;
//   * geometry in integer DEVICE PIXELS relative to the parent, set by the parent's layout;
//   * size hints in DIPs (sizeHint / minimumSizeHint, explicit minimum/maximum sizes) plus a per-axis size policy --
//     the four Qt policies the panels' widgets use by default (Fixed, Minimum, Preferred, Expanding; see layout.hpp
//     for the measured subset and why);
//   * visibility and enabled state (effective = own AND every ancestor's);
//   * paint(Painter&) in DIPs, relative to the widget, clipped to it; update() asks the window for a repaint,
//     updateGeometry() for a new layout pass.
// The root widget is attached to a UiHost (the window: ui/win32/ui_window.hpp), which supplies the DPI scale and the
// text engine and receives the invalidations. Single-threaded (the UI thread).
#pragma once

#include "platform/input.hpp"
#include "ui/core/geometry.hpp"
#include "ui/core/painter.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tcad::ui {

class Layout;
class InputRouter;
class PopupService;
class Widget;

using TimerId = std::uint64_t;

// Timers for widgets (N2c): the platform's (N1's Application) in a window, a fake clock in portable tests.
class TimerService {
public:
    virtual ~TimerService() = default;
    virtual TimerId start(int interval_ms, bool repeat, std::function<void()> fn) = 0;
    virtual void stop(TimerId id) = 0;
};

class UiHost {
public:
    virtual ~UiHost() = default;
    virtual void invalidate(const RectI& window_px) = 0;  // repaint this region (window client coordinates)
    virtual void scheduleLayout() = 0;                    // size hints changed: lay the tree out again
    virtual TextEngine& textEngine() = 0;
    virtual double scale() const = 0;                     // device px per DIP
    virtual InputRouter* input() { return nullptr; }      // focus, hover, capture (N2c)
    virtual TimerService* timers() { return nullptr; }
    virtual void widgetGone(Widget*) {}                   // a widget (and its subtree) left the tree or died
    virtual PopupService* popups() { return nullptr; }    // top-level popup windows (N3c; ui/core/popup.hpp)
    // Tell a screen reader something happened to `w` that is not a property change: the highlighted row of an open
    // drop-down (UI Automation's notification event). No-op without one.
    virtual void announce(Widget*, std::string_view) {}
};

// The Qt focus policies the framework keeps (N2c): None; Tab (keyboard only); Click (mouse only); Strong (both).
enum class FocusPolicy { None, Tab, Click, Strong };
enum class FocusReason { Mouse, Tab, Backtab, Mnemonic, Window, Other };
// The cursor over a widget; Inherit takes the parent's. A fixed set: what N3's widgets need (edits, splitters, links).
enum class Cursor { Inherit, Arrow, IBeam, Hand, SizeWE, SizeNS, SizeAll, Cross, Wait };

// Accessibility (N2f): what a widget is to assistive technology. The Win32 side maps it to UI Automation control
// types and patterns: Invoke for Button/MenuItem, Toggle when accessibleToggleState() has a value, Value when
// accessibleHasValue(), Text for an edit (ui/win32/uia_provider.cpp).
enum class Role { Pane, Group, Text, Edit, Button, CheckBox, RadioButton, ComboBox, Slider, List, ListItem, Tree,
                  TreeItem, Tab, TabItem, Menu, MenuItem, ToolBar, StatusBar, ProgressBar, ScrollBar, Image, Spinner, Custom };

// RangeValue (N3b: sliders and spin boxes): a number within [minimum, maximum]; `small_step` is one arrow key,
// `large_step` one page. `valid` false: the widget has no range.
struct AccessibleRange {
    bool valid = false;
    double value = 0, minimum = 0, maximum = 0, small_step = 1, large_step = 10;
    bool read_only = false;
};

// A mouse event as a widget sees it: positions in DIPs, `pos` relative to the widget.
struct UiMouseEvent {
    platform::MouseType type = platform::MouseType::Move;
    platform::MouseButton button = platform::MouseButton::None;
    PointF pos;         // widget-local DIPs
    PointF window_pos;  // window DIPs
    platform::Mod mods = platform::Mod::None;
    unsigned buttons = 0;       // held buttons, bit per MouseButton
    double wheel_steps = 0;     // Wheel: notches, positive = away from the user
    int clicks = 1;             // 2 for a double click
    bool dragging = false;      // a button is held and the pointer moved past the system drag threshold
};

enum class SizePolicy { Fixed, Minimum, Preferred, Expanding };

struct Policy {
    SizePolicy horizontal = SizePolicy::Preferred;
    SizePolicy vertical = SizePolicy::Preferred;
};

class Widget {
public:
    Widget();
    virtual ~Widget();
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    // -- tree
    Widget* parent() const { return parent_; }
    const std::vector<std::unique_ptr<Widget>>& children() const { return children_; }
    template <class T, class... A>
    T* addChild(A&&... a) {
        return static_cast<T*>(adopt(std::make_unique<T>(std::forward<A>(a)...)));
    }
    Widget* adopt(std::unique_ptr<Widget> child);
    std::unique_ptr<Widget> release(Widget* child);  // null if not a child
    Widget* root();
    Widget* findChild(std::string_view name);         // depth-first, this widget included
    std::string name;

    // -- the host (set on the root by the window)
    void setHost(UiHost* host) { host_ = host; }
    UiHost* host() const;
    double scale() const;          // 1.0 without a host
    TextEngine* textEngine() const;  // null without a host

    // -- geometry (device px)
    const RectI& geometry() const { return geometry_; }
    void setGeometry(const RectI& r);  // lays the children out again (always)
    RectI windowRect() const;          // geometry in window client coordinates
    SizeF sizeDips() const;

    // -- visibility and state
    void setVisible(bool v);
    void show() { setVisible(true); }
    void hide() { setVisible(false); }
    bool isVisibleSelf() const { return visible_; }
    bool isVisible() const;  // effective
    void setEnabled(bool e);
    bool isEnabled() const;  // effective

    // -- sizing (DIPs)
    virtual SizeF sizeHint() const;         // default: the layout's, else 0x0
    virtual SizeF minimumSizeHint() const;  // default: the layout's, else 0x0
    void setMinimumSize(SizeF s);
    void setMinimumWidth(float w) { setMinimumSize({w, min_size_.height}); }
    void setMinimumHeight(float h) { setMinimumSize({min_size_.width, h}); }
    SizeF minimumSize() const { return min_size_; }
    void setMaximumSize(SizeF s);
    SizeF maximumSize() const { return max_size_; }
    Policy sizePolicy() const { return policy_; }
    void setSizePolicy(Policy p);
    // Height-for-width (N3a): a word-wrapped label, or a widget whose layout holds one. heightForWidth(w) is the
    // height (DIPs) needed at width w (DIPs); layouts that know an item's width use it (layout.hpp).
    virtual bool hasHeightForWidth() const;
    virtual float heightForWidth(float width) const;
    // QWidget::setContentsMargins (N3a): space inside the widget its layout keeps clear (a group box's frame and
    // title). The layout's own margins apply inside it.
    void setContentsMargins(Margins m);
    virtual Margins contentsMargins() const { return contents_margins_; }  // a group box computes its own
    RectI contentsRectPx() const;  // widget-local px

    // -- layout (owned; its items are children of this widget)
    template <class L, class... A>
    L* setLayout(A&&... a) {
        auto l = std::make_unique<L>(this, std::forward<A>(a)...);
        L* raw = l.get();
        installLayout(std::move(l));
        return raw;
    }
    Layout* layout() const { return layout_.get(); }

    // -- painting and invalidation
    virtual void paint(Painter& p);
    void paintTree(Painter& p);  // this widget and its visible descendants
    void update();               // repaint this widget
    void updateGeometry();       // its size hints changed: the tree is laid out again

    // -- input (N2c). The InputRouter delivers events; a handler returns true when it used the event, false to let
    // it go to the parent (mouse, key) -- Qt's accept/ignore.
    void setFocusPolicy(FocusPolicy p) { focus_policy_ = p; }
    FocusPolicy focusPolicy() const { return focus_policy_; }
    bool acceptsFocus(FocusReason why) const;  // policy allows `why`, and visible and enabled
    // False takes a focusable widget out of the Tab order for now: an exclusive radio group is one Tab stop, its
    // checked button (N3a).
    virtual bool isTabStop() const { return true; }
    // Alt+<letter> underlines are shown (Windows' keyboard cues: after Alt is pressed, or always by the system
    // setting). True without a router.
    bool mnemonicCuesVisible() const;
    void setFocus(FocusReason why = FocusReason::Other);  // a focus proxy's, when it has one
    // QWidget::setFocusProxy (N3b): focusing this widget -- a label's buddy, Alt+<letter> -- focuses `w`, and hasFocus()
    // reports w's. `w` must be this widget or one of its descendants (it then dies with it); it stays the Tab stop.
    void setFocusProxy(Widget* w) { focus_proxy_ = w == this ? nullptr : w; }
    Widget* focusProxy() const { return focus_proxy_; }
    void clearFocus();
    bool hasFocus() const;  // the focus widget of an ACTIVE window
    bool isHovered() const;
    void setCursor(Cursor c) { cursor_ = c; }
    Cursor cursor() const { return cursor_; }
    // Alt+<mnemonic> activates the widget ("&Save" -> 'S'); case-insensitive, 0 = none.
    void setMnemonic(char32_t c) { mnemonic_ = c; }
    char32_t mnemonic() const { return mnemonic_; }
    std::string toolTip;
    PointF mapFromWindow(PointF window_dips) const;

    virtual bool mouseEvent(const UiMouseEvent&) { return false; }
    virtual bool keyEvent(const platform::KeyEvent&) { return false; }
    virtual bool charEvent(char32_t) { return false; }
    // True: this key goes to the widget BEFORE the window's shortcuts (Qt's ShortcutOverride), e.g. an edit's Ctrl+A.
    virtual bool overridesShortcut(const platform::KeyEvent&) const { return false; }
    virtual bool wantsTab() const { return false; }  // true: Tab/Shift+Tab are keys for it, not focus moves
    virtual void focusChanged(bool /*in*/, FocusReason) {}
    virtual void hoverChanged(bool /*entered*/) {}
    virtual void activateMnemonic() { setFocus(FocusReason::Mnemonic); }

    // -- accessibility (N2f). The name is what a screen reader says (UIA Name); `name` above is the automation id.
    std::string accessibleName;
    virtual Role accessibleRole() const { return Role::Pane; }
    virtual bool accessibleHasValue() const { return false; }
    virtual std::string accessibleValue() const { return {}; }
    virtual bool accessibleSetValue(std::string_view) { return false; }
    virtual bool accessibleReadOnly() const { return true; }
    virtual bool accessibleInvoke() { return false; }  // true when it acted (a button's click)
    virtual int accessibleToggleState() const { return -1; }  // -1: no Toggle pattern; 0 off, 1 on, 2 indeterminate
    virtual void accessibleToggle() {}
    // SelectionItem (N3a: radio buttons; list rows and tabs later): -1 none, else 0/1 selected.
    virtual int accessibleSelectionState() const { return -1; }
    virtual void accessibleSelect() {}
    // RangeValue (N3b). accessibleSetRangeValue: false when the value is refused (out of range, not a whole number).
    virtual AccessibleRange accessibleRange() const { return {}; }
    virtual bool accessibleSetRangeValue(double) { return false; }
    // ExpandCollapse (N3c: combo boxes; menus later): -1 none, 0 collapsed, 1 expanded.
    virtual int accessibleExpandState() const { return -1; }
    virtual void accessibleExpand(bool /*open*/) {}
    std::function<void()> accessible_expand_changed;  // set by the window's UIA host
    void notifyExpandChanged() const {
        if (accessible_expand_changed) accessible_expand_changed();
    }
    void announce(std::string_view text);  // through the host; see UiHost::announce
    // Set by the window's UIA host: the widget's range value changed (raises the property-changed event).
    std::function<void()> accessible_range_changed;
    void notifyRangeChanged() const {
        if (accessible_range_changed) accessible_range_changed();
    }

    // -- timers, stopped automatically when the widget dies or leaves the tree. 0 when the tree has no timer service.
    TimerId startTimer(int interval_ms, bool repeat, std::function<void()> fn);
    void stopTimer(TimerId id);

protected:
    virtual void resized() {}

private:
    void installLayout(std::unique_ptr<Layout> l);
    void notifyGone();  // this subtree leaves the host: the router forgets it

    Widget* parent_ = nullptr;
    std::vector<std::unique_ptr<Widget>> children_;
    UiHost* host_ = nullptr;
    RectI geometry_;
    bool visible_ = true;
    bool enabled_ = true;
    SizeF min_size_{0, 0};
    SizeF max_size_{kMaxDip, kMaxDip};
    Policy policy_;
    Margins contents_margins_;
    std::unique_ptr<Layout> layout_;
    FocusPolicy focus_policy_ = FocusPolicy::None;
    Cursor cursor_ = Cursor::Inherit;
    char32_t mnemonic_ = 0;
    Widget* focus_proxy_ = nullptr;
    std::vector<TimerId> timers_;
};

}  // namespace tcad::ui
