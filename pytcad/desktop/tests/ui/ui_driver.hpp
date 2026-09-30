// The headless UI test driver (N2c; grows into N2g's): drives a UiWindow with REAL window messages sent through its
// window procedure -- the same path as user input -- and never with SendInput, which would move the real cursor and
// type into whatever window has the focus. Positions are the tree's DIPs.
//
// Modifiers: the window reads Ctrl/Shift/Alt with GetKeyState (the real keyboard), so every synthetic key AND mouse
// message sets this thread's keyboard state to exactly the modifiers asked for and restores it afterwards -- a key the
// person at the machine happens to hold cannot change a test (the N1 self-test's flaky F12; and in N2e a held Shift
// turned a synthetic click into a Shift+click, 2026-09-30).
#pragma once

#include "platform/input.hpp"
#include "ui/win32/ui_window.hpp"

#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

namespace tcad::ui::testing {

class Driver {
public:
    explicit Driver(UiWindow& w) : w_(w), h_(w.window().hwnd()) {}

    LPARAM at(PointF dip) const {  // client device px, as Windows sends them
        const int x = static_cast<int>(std::lround(dip.x * w_.scale()));
        const int y = static_cast<int>(std::lround(dip.y * w_.scale()));
        return MAKELPARAM(static_cast<WORD>(static_cast<short>(x)), static_cast<WORD>(static_cast<short>(y)));
    }
    void move(PointF p, platform::Mod mods = platform::Mod::None) {
        withMods(mods, [&] { return SendMessageW(h_, WM_MOUSEMOVE, held_, at(p)); });
    }
    void press(PointF p, platform::MouseButton b = platform::MouseButton::Left, bool dbl = false, platform::Mod mods = platform::Mod::None) {
        held_ |= mk(b);
        withMods(mods, [&] { return SendMessageW(h_, downMsg(b, dbl), held_ | xbutton(b), at(p)); });
    }
    void release(PointF p, platform::MouseButton b = platform::MouseButton::Left, platform::Mod mods = platform::Mod::None) {
        held_ &= ~mk(b);
        withMods(mods, [&] { return SendMessageW(h_, upMsg(b), held_ | xbutton(b), at(p)); });
    }
    void click(PointF p) {
        press(p);
        release(p);
    }
    void wheel(PointF p, double steps) {  // WM_MOUSEWHEEL carries SCREEN coordinates
        POINT s{static_cast<LONG>(std::lround(p.x * w_.scale())), static_cast<LONG>(std::lround(p.y * w_.scale()))};
        ClientToScreen(h_, &s);
        withMods(platform::Mod::None, [&] {
            return SendMessageW(h_, WM_MOUSEWHEEL, MAKEWPARAM(held_, static_cast<short>(std::lround(steps * WHEEL_DELTA))),
                                MAKELPARAM(static_cast<WORD>(static_cast<short>(s.x)), static_cast<WORD>(static_cast<short>(s.y))));
        });
    }
    void leave() { SendMessageW(h_, WM_MOUSELEAVE, 0, 0); }
    // WM_KEYDOWN (WM_SYSKEYDOWN with Alt) then WM_KEYUP, with exactly `mods` held.
    LRESULT key(int vk, platform::Mod mods = platform::Mod::None) {
        const bool alt = any(mods & platform::Mod::Alt);
        return withMods(mods, [&] {
            const LRESULT r = SendMessageW(h_, alt ? WM_SYSKEYDOWN : WM_KEYDOWN, vk, alt ? 0x20000001 : 0x00000001);
            SendMessageW(h_, alt ? WM_SYSKEYUP : WM_KEYUP, vk, alt ? 0xE0000001 : 0xC0000001);
            return r;
        });
    }
    void type(const std::u16string& text) {
        for (char16_t c : text) SendMessageW(h_, WM_CHAR, c, 1);
    }
    bool setCursorMessage() { return SendMessageW(h_, WM_SETCURSOR, reinterpret_cast<WPARAM>(h_), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE)) != 0; }

private:
    template <class F>
    auto withMods(platform::Mod mods, F f) {
        BYTE keys[256]{}, want[256];
        GetKeyboardState(keys);
        std::copy(std::begin(keys), std::end(keys), want);
        auto set = [&](std::initializer_list<int> vks, bool down) {
            for (int vk : vks) want[vk] = down ? static_cast<BYTE>(want[vk] | 0x80) : static_cast<BYTE>(want[vk] & ~0x80);
        };
        set({VK_CONTROL, VK_LCONTROL}, any(mods & platform::Mod::Ctrl));
        set({VK_SHIFT, VK_LSHIFT}, any(mods & platform::Mod::Shift));
        set({VK_MENU, VK_LMENU}, any(mods & platform::Mod::Alt));
        set({VK_RCONTROL, VK_RSHIFT, VK_RMENU}, false);
        SetKeyboardState(want);
        auto r = f();
        SetKeyboardState(keys);
        return r;
    }
    static WPARAM mk(platform::MouseButton b) {
        using B = platform::MouseButton;
        return b == B::Left ? MK_LBUTTON : b == B::Right ? MK_RBUTTON : b == B::Middle ? MK_MBUTTON : b == B::X1 ? MK_XBUTTON1 : MK_XBUTTON2;
    }
    static WPARAM xbutton(platform::MouseButton b) {
        using B = platform::MouseButton;
        return b == B::X1 ? MAKEWPARAM(0, XBUTTON1) : b == B::X2 ? MAKEWPARAM(0, XBUTTON2) : 0;
    }
    static UINT downMsg(platform::MouseButton b, bool dbl) {
        using B = platform::MouseButton;
        if (b == B::Left) return dbl ? WM_LBUTTONDBLCLK : WM_LBUTTONDOWN;
        if (b == B::Right) return dbl ? WM_RBUTTONDBLCLK : WM_RBUTTONDOWN;
        if (b == B::Middle) return dbl ? WM_MBUTTONDBLCLK : WM_MBUTTONDOWN;
        return dbl ? WM_XBUTTONDBLCLK : WM_XBUTTONDOWN;
    }
    static UINT upMsg(platform::MouseButton b) {
        using B = platform::MouseButton;
        return b == B::Left ? WM_LBUTTONUP : b == B::Right ? WM_RBUTTONUP : b == B::Middle ? WM_MBUTTONUP : WM_XBUTTONUP;
    }

    UiWindow& w_;
    HWND h_;
    WPARAM held_ = 0;
};

}  // namespace tcad::ui::testing
