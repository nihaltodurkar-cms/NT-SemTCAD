// The fakes the portable widget tests of N3e/N3f share (no Win32): a text engine that hit-tests and places a caret (6 DIPs a
// byte at the 12-DIP font, a 15-DIP line), a timer service on a fake clock, a clipboard, a popup service that records,
// an inline editor, and a host that wires them to an InputRouter. Header-only: each test source includes it.
#pragma once

#include "ui/core/clipboard.hpp"
#include "ui/core/inline_editor.hpp"
#include "ui/core/input_router.hpp"
#include "ui/core/keys.hpp"
#include "ui/core/popup.hpp"
#include "ui/core/recording_painter.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace tcad::ui::fake {

using platform::KeyEvent;
using platform::Mod;
using platform::MouseButton;
using platform::MouseEvent;
using platform::MouseType;

class Text final : public TextEngine {
public:
    SizeF measure(std::string_view s, const TextStyle& st) override { return {0.5f * st.size * static_cast<float>(s.size()), 1.25f * st.size}; }
    TextHit hitTest(std::string_view s, const TextStyle& st, float, PointF p) override {
        const float w = 0.5f * st.size;
        return {static_cast<std::size_t>(std::clamp(std::lround(p.x / w), 0L, static_cast<long>(s.size()))), true};
    }
    RectF caretRect(std::string_view, const TextStyle& st, float, std::size_t o) override { return {0.5f * st.size * static_cast<float>(o), 0, 0, 1.25f * st.size}; }
};

class Timers final : public TimerService {
public:
    struct Tm {
        long long due;
        int interval;
        bool repeat;
        std::function<void()> fn;
    };
    long long now = 0;
    TimerId next = 1;
    std::map<TimerId, Tm> live;
    TimerId start(int ms, bool repeat, std::function<void()> fn) override {
        live[next] = {now + ms, ms, repeat, std::move(fn)};
        return next++;
    }
    void stop(TimerId id) override { live.erase(id); }
    void advance(int ms) {
        const long long end = now + ms;
        for (;;) {
            TimerId id = 0;
            long long best = end + 1;
            for (auto& [k, t] : live)
                if (t.due <= end && t.due < best) id = k, best = t.due;
            if (!id) break;
            now = best;
            auto fn = live[id].fn;
            if (live[id].repeat) live[id].due += live[id].interval;
            else live.erase(id);
            fn();
        }
        now = end;
    }
};

class Board final : public Clipboard {
public:
    std::optional<std::string> value;
    void setText(std::string_view s) override { value = std::string(s); }
    std::optional<std::string> text() override { return value; }
};

class Editor final : public InlineEditor {
public:
    std::string value;
    bool selected_all = false;
    Editor() { setFocusPolicy(FocusPolicy::Strong); }
    std::string text() const override { return value; }
    void setText(std::string_view s) override { value = std::string(s); }
    void selectAll() override { selected_all = true; }
    bool keyEvent(const KeyEvent& e) override {
        if (!e.down) return false;
        if (e.vk == keys::Return && on_finished) return on_finished(true), true;
        if (e.vk == keys::Escape && on_finished) return on_finished(false), true;
        return false;
    }
    void focusChanged(bool in, FocusReason) override {
        if (!in && on_finished) on_finished(true);  // the focus left: committed, as the real editor does
    }
};

// A popup service that records what was shown and lets a test dismiss it.
class Popups final : public PopupService {
public:
    class Handle final : public PopupHandle {
    public:
        Handle(Popups& s, Widget* anchor, std::unique_ptr<Widget> c) : svc(s), anchor(anchor) {
            root.setHost(svc.host);  // the content is in a tree with the host, as in a real popup window: text engine, timers
            owned = root.adopt(std::move(c));
        }
        ~Handle() override { root.setHost(nullptr); }
        void close() override { svc.closeHandle(this, false); }
        Widget* content() const override { return owned; }
        RectI screenRectPx() const override { return rect; }
        Popups& svc;
        Widget* anchor;
        Widget root;
        Widget* owned = nullptr;
        RectI rect{0, 0, 100, 100};
    };
    UiHost* host = nullptr;  // set by Host
    int shown = 0, closed_by_opener = 0, dismissed = 0;
    std::vector<std::unique_ptr<Handle>> stack;  // bottom first; a combo's drop-down is the only one, a submenu goes on top
    std::unique_ptr<Handle> tip;
    std::vector<std::unique_ptr<Handle>> graveyard;
    std::vector<PopupRequest> requests;  // every request, in order
    Handle* top() const { return stack.empty() ? nullptr : stack.back().get(); }
    PopupHandle* show(Widget* anchor, std::unique_ptr<Widget> content) override {
        PopupRequest r;
        r.anchor = anchor;
        return showRequest(r, std::move(content));
    }
    PopupHandle* showRequest(const PopupRequest& r, std::unique_ptr<Widget> content) override {
        requests.push_back(r);
        if (r.tooltip) {
            if (tip) graveyard.push_back(std::move(tip));
            ++shown;
            tip = std::make_unique<Handle>(*this, r.anchor, std::move(content));
            return tip.get();
        }
        if (tip) graveyard.push_back(std::move(tip));
        if (!r.child) dismiss();
        ++shown;
        stack.push_back(std::make_unique<Handle>(*this, r.anchor, std::move(content)));
        return stack.back().get();
    }
    // closes `h` and everything above it, top first
    void closeHandle(Handle* h, bool was_dismissed) {
        if (h == tip.get()) {
            graveyard.push_back(std::move(tip));
            return;
        }
        const auto at = std::find_if(stack.begin(), stack.end(), [&](const auto& x) { return x.get() == h; });
        if (at == stack.end()) return;
        while (stack.size() > static_cast<std::size_t>(at - stack.begin())) {
            auto top_h = std::move(stack.back());
            stack.pop_back();
            auto cb = top_h->on_dismissed;
            graveyard.push_back(std::move(top_h));
            if (was_dismissed) {
                ++dismissed;
                if (cb) cb();
            } else {
                ++closed_by_opener;
            }
        }
    }
    void dismiss() {
        if (tip) graveyard.push_back(std::move(tip));
        while (!stack.empty()) closeHandle(stack.front().get(), true);
    }
    bool open() const { return !stack.empty(); }
    int depth() const { return static_cast<int>(stack.size()); }
    Widget* content() const { return stack.empty() ? nullptr : stack.back()->owned; }
    Widget* contentAt(int i) const { return i >= 0 && i < depth() ? stack[static_cast<std::size_t>(i)]->owned : nullptr; }
    Widget* tipContent() const { return tip ? tip->owned : nullptr; }
};

class Host final : public UiHost {
public:
    Text text;
    Timers clock;
    Board board;
    Popups popups_;
    Widget root;
    InputRouter router{root};
    bool editing_allowed = true;
    Editor* last_editor = nullptr;
    int editors_made = 0;
    std::vector<std::string> announced;
    Host() {
        root.setHost(this);
        root.setGeometry({0, 0, 500, 400});
        router.setTimers(&clock);
        popups_.host = this;
    }
    ~Host() override { root.setHost(nullptr); }
    // A rig whose widgets listen to actions or menus declared after the host destroys its tree first (the contract: those outlive the widgets).
    void clearTree() {
        while (!root.children().empty()) root.release(root.children().front().get());
    }
    void invalidate(const RectI&) override {}
    void scheduleLayout() override {}
    TextEngine& textEngine() override { return text; }
    double scale() const override { return 1.0; }
    InputRouter* input() override { return &router; }
    TimerService* timers() override { return &clock; }
    Clipboard* clipboard() override { return &board; }
    PopupService* popups() override { return &popups_; }
    void announce(Widget*, std::string_view s) override { announced.emplace_back(s); }
    bool canCreateInlineEditor() const override { return editing_allowed; }
    std::unique_ptr<InlineEditor> createInlineEditor() override {
        if (!editing_allowed) return nullptr;
        auto e = std::make_unique<Editor>();
        last_editor = e.get();
        ++editors_made;
        return e;
    }
    void widgetGone(Widget* w) override {
        if (w == last_editor) last_editor = nullptr;
        router.widgetGone(w);
    }
};

constexpr unsigned kLeftBit = 1u << static_cast<unsigned>(MouseButton::Left);

inline MouseEvent mouse(MouseType t, float x, float y, unsigned held = 0, Mod mods = Mod::None, MouseButton b = MouseButton::Left) {
    MouseEvent e;
    e.type = t;
    e.button = t == MouseType::Move ? MouseButton::None : b;
    e.x = x;
    e.y = y;
    e.buttons_down = held;
    e.mods = mods;
    return e;
}
inline MouseEvent wheel(float x, float y, double steps) {
    MouseEvent e;
    e.type = MouseType::Wheel;
    e.x = x;
    e.y = y;
    e.wheel_steps = steps;
    return e;
}
inline KeyEvent key(int vk, Mod m = Mod::None) { return {vk, m, true, false}; }

inline bool is(const std::string& got, const std::string& want) {
    if (got != want) std::printf("  got      [%s]%c  expected [%s]%c", got.c_str(), 10, want.c_str(), 10);
    return got == want;
}

}  // namespace tcad::ui::fake

#define CHECK_STR(a, b) CHECK(::tcad::ui::fake::is((a), (b)))
