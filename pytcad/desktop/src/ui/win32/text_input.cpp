#include "ui/win32/text_input.hpp"

#include "ui/core/style.hpp"
#include "ui/win32/clipboard.hpp"

#include <algorithm>
#include <cmath>

namespace tcad::ui {

using platform::KeyEvent;
using platform::Mod;
using tcad::desktop::theme::T;

namespace {

std::string utf8Of(char32_t c) {
    std::string s;
    if (c < 0x80) s += static_cast<char>(c);
    else if (c < 0x800) s += static_cast<char>(0xC0 | (c >> 6)), s += static_cast<char>(0x80 | (c & 0x3F));
    else if (c < 0x10000)
        s += static_cast<char>(0xE0 | (c >> 12)), s += static_cast<char>(0x80 | ((c >> 6) & 0x3F)), s += static_cast<char>(0x80 | (c & 0x3F));
    else
        s += static_cast<char>(0xF0 | (c >> 18)), s += static_cast<char>(0x80 | ((c >> 12) & 0x3F)),
            s += static_cast<char>(0x80 | ((c >> 6) & 0x3F)), s += static_cast<char>(0x80 | (c & 0x3F));
    return s;
}

std::vector<std::size_t> codePointStops(std::string_view s) {
    std::vector<std::size_t> v;
    for (std::size_t i = 0; i < s.size(); ++i)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) v.push_back(i);
    v.push_back(s.size());
    return v;
}

}  // namespace

TextInput::TextInput(HWND window, bool multi_line)
    : model_(
          [this](std::string_view s) {
              TextEngine* te = textEngine();
              return te ? te->caretStops(s, style()) : codePointStops(s);  // before it is in a window
          },
          multi_line),
      hwnd_(window) {
    setFocusPolicy(FocusPolicy::Strong);
    setCursor(Cursor::IBeam);
    TextStore::Geometry geo;
    geo.range_screen = [this](std::size_t a, std::size_t b) { return rangeScreenRect(a, b); };
    geo.control_screen = [this] { return screenRect(); };
    geo.offset_at = [this](POINT screen) { return offsetAtScreen(screen); };
    store_ = new TextStore(model_, hwnd_, std::move(geo));
    setupTsf();
}

TextInput::~TextInput() {
    if (tsf_ && doc_) {
        ComPtr<ITfDocumentMgr> focused;
        if (SUCCEEDED(tsf_->GetFocus(&focused)) && focused.Get() == doc_.Get()) tsf_->SetFocus(blank_doc_.Get());
        doc_->Pop(TF_POPF_ALL);
    }
    ctx_.Reset();
    doc_.Reset();
    blank_doc_.Reset();
    if (tsf_) tsf_->Deactivate();  // balances Activate (TSF counts them per thread)
    tsf_.Reset();
    if (store_) store_->Release();
}

void TextInput::setupTsf() {
    // The thread's TSF manager (one per thread; Activate/Deactivate are counted). Without TSF (it failed to start)
    // the edit still takes WM_CHAR input; tsfReady() says which.
    if (FAILED(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&tsf_)))) return;
    if (FAILED(tsf_->Activate(&client_))) {
        tsf_.Reset();
        return;
    }
    if (FAILED(tsf_->CreateDocumentMgr(&doc_)) || FAILED(tsf_->CreateDocumentMgr(&blank_doc_)) ||
        FAILED(doc_->CreateContext(client_, 0, static_cast<ITextStoreACP2*>(store_), &ctx_, &cookie_)) || FAILED(doc_->Push(ctx_.Get()))) {
        ctx_.Reset();
        doc_.Reset();
    }
}

TextStyle TextInput::style() const {
    TextStyle s;
    s.color = token(isEnabled() ? T::Text : T::TextFaint);
    s.valign = VAlign::Top;
    return s;
}

float TextInput::lineHeight() const {
    TextEngine* te = textEngine();
    return te ? te->measure("", style()).height : 16.0f;
}

std::vector<RECT> TextInput::rangeScreenRects(std::size_t a, std::size_t b) const {
    const RectI w = windowRect();
    const double s = scale();
    std::vector<RECT> out;
    for (const RectF& r : rangeRects(a, b)) {
        POINT tl{w.x + static_cast<LONG>(std::lround(r.x * s)), w.y + static_cast<LONG>(std::lround(r.y * s))};
        POINT br{w.x + static_cast<LONG>(std::lround(r.right() * s)) + 1, w.y + static_cast<LONG>(std::lround(r.bottom() * s))};
        ClientToScreen(hwnd_, &tl);
        ClientToScreen(hwnd_, &br);
        out.push_back(RECT{tl.x, tl.y, br.x, br.y});
    }
    return out;
}

RECT TextInput::rangeScreenRect(std::size_t a, std::size_t b) const {
    RECT u{0, 0, 0, 0};
    bool first = true;
    for (const RECT& r : rangeScreenRects(a, b)) {
        if (first) u = r, first = false;
        else u = RECT{std::min(u.left, r.left), std::min(u.top, r.top), std::max(u.right, r.right), std::max(u.bottom, r.bottom)};
    }
    return u;
}

RECT TextInput::screenRect() const {
    const RectI w = windowRect();
    POINT tl{w.x, w.y}, br{w.right(), w.bottom()};
    ClientToScreen(hwnd_, &tl);
    ClientToScreen(hwnd_, &br);
    return RECT{tl.x, tl.y, br.x, br.y};
}

std::size_t TextInput::offsetAtScreen(POINT screen) const {
    ScreenToClient(hwnd_, &screen);
    const RectI w = windowRect();
    const double s = scale();
    return offsetAt({static_cast<float>((screen.x - w.x) / s), static_cast<float>((screen.y - w.y) / s)});
}

bool TextInput::accessibleSetValue(std::string_view v) {
    if (model_.readOnly()) return false;
    setText(v);
    return true;
}

void TextInput::restartBlink() {
    caret_on_ = true;
    if (blink_) stopTimer(blink_);
    blink_ = 0;
    if (blinking_ && hasFocus() && !model_.readOnly()) {
        const UINT ms = GetCaretBlinkTime();
        if (ms != INFINITE && ms > 0)
            blink_ = startTimer(static_cast<int>(ms), true, [this] {
                caret_on_ = !caret_on_;
                update();
            });
    }
}

void TextInput::setCaretBlinking(bool on) {
    blinking_ = on;
    restartBlink();
    update();
}

void TextInput::changed(std::size_t old_u16_len) {
    store_->notifyChanged(0, old_u16_len, store_->u16Length());  // the whole text: simple and always correct
    contentChanged();
    ensureCaretVisible();
    restartBlink();
    update();
    if (on_text_changed) on_text_changed();
    if (on_accessible_value_changed) on_accessible_value_changed();
}

void TextInput::changedByProgram(std::size_t old_u16_len) {
    store_->notifyChanged(0, old_u16_len, store_->u16Length());
    contentChanged();
    update();
    if (on_text_changed) on_text_changed();
    if (on_accessible_value_changed) on_accessible_value_changed();
}

void TextInput::selectionMoved() {
    store_->notifySelectionChanged();
    ensureCaretVisible();
    restartBlink();
    update();
    if (on_selection_changed) on_selection_changed();
}

void TextInput::setText(std::string_view utf8) {
    const std::size_t old = store_->u16Length();
    model_.setText(utf8);
    textSet();
    changed(old);
}

void TextInput::setReadOnly(bool r) {
    model_.setReadOnly(r);
    restartBlink();
    update();
}

void TextInput::setPlaceholderText(std::string s) {
    placeholder_ = std::move(s);
    update();
}

bool TextInput::mouseEvent(const UiMouseEvent& e) {
    using platform::MouseButton;
    using platform::MouseType;
    const bool press = e.type == MouseType::Down || e.type == MouseType::DoubleClick;  // a double click arrives as its own type
    if (press && e.button == MouseButton::Left) {
        const std::size_t at = offsetAt(e.pos);
        if (e.clicks >= 2) {
            const auto [a, b] = model_.wordAt(at);
            model_.setSelection(a, b);
        } else {
            model_.setSelection(any(e.mods & Mod::Shift) ? model_.anchor() : at, at);
        }
    } else if (e.type == MouseType::Move && (e.buttons & (1u << static_cast<int>(MouseButton::Left)))) {
        dragOutside(e);
        model_.setSelection(model_.anchor(), offsetAt(e.pos));
    } else if (e.type == MouseType::Up && e.button == MouseButton::Right) {
        return contextMenuRequested(e.pos) || true;  // the press was taken; the release belongs to the menu
    } else if (e.type != MouseType::Up) {
        return press;  // other buttons: taken, nothing to do
    }
    selectionMoved();
    return true;
}

bool TextInput::overridesShortcut(const KeyEvent& e) const {
    if (e.mods == Mod::Ctrl) return e.vk == 'A' || e.vk == 'C' || e.vk == 'X' || e.vk == 'V' || e.vk == 'Z' || e.vk == 'Y' || e.vk == VK_INSERT;
    if (e.mods == Mod::Shift) return e.vk == VK_INSERT || e.vk == VK_DELETE;
    return e.mods == (Mod::Ctrl | Mod::Shift) && e.vk == 'Z';
}

bool TextInput::keyEvent(const KeyEvent& e) {
    if (!e.down) return false;
    const bool ctrl = any(e.mods & Mod::Ctrl), shift = any(e.mods & Mod::Shift);
    if (any(e.mods & Mod::Alt)) return false;
    if (beforeKey(e)) return true;
    if (e.vk == VK_APPS || (shift && e.vk == VK_F10)) return contextMenuRequested({sizeDips().width / 2, sizeDips().height / 2});
    const std::size_t old = store_->u16Length();
    const int rev = model_.revision();
    bool handled = true;
    auto paste = [&] {
        if (auto t = clipboardText(hwnd_)) model_.insert(*t);
    };
    auto copy = [&] {
        if (model_.hasSelection()) setClipboardText(hwnd_, model_.selectedText());
    };
    auto cut = [&] {
        std::string out;
        if (model_.hasSelection() && !model_.readOnly() && model_.cut(&out)) setClipboardText(hwnd_, out);
    };
    switch (e.vk) {
        case VK_LEFT: model_.moveLeft(ctrl, shift); break;
        case VK_RIGHT: model_.moveRight(ctrl, shift); break;
        case VK_HOME: model_.home(shift); break;
        case VK_END: model_.end(shift); break;
        case VK_BACK: model_.backspace(ctrl); break;
        case VK_DELETE:
            if (shift && !ctrl) cut();  // Shift+Delete
            else model_.deleteForward(ctrl);
            break;
        case VK_INSERT:
            if (ctrl && !shift) copy();  // Ctrl+Insert
            else if (shift && !ctrl) paste();  // Shift+Insert
            else handled = false;
            break;
        case VK_RETURN: return enterPressed();
        default:
            if (!ctrl) return false;
            if (e.vk == 'A') model_.selectAll();
            else if (e.vk == 'C') copy();
            else if (e.vk == 'X') cut();
            else if (e.vk == 'V') paste();
            else if (e.vk == 'Z') {
                if (shift) model_.redo();
                else model_.undo();
            } else if (e.vk == 'Y') model_.redo();
            else handled = false;
    }
    if (!handled) return false;
    if (model_.revision() != rev) changed(old);
    else selectionMoved();
    return true;
}

// -- the right-click menu (N3f) ----------------------------------------------------------------------------------------

bool TextInput::contextMenuRequested(PointF local) {
    if (!isEnabled()) return false;
    if (!context_menu_) {
        context_menu_ = std::make_unique<EditContextMenu>(
            EditContextMenu::Kind::Edit,
            [this] {
                EditContextMenu::State s;
                s.undo = !model_.readOnly() && model_.canUndo();
                s.cut = model_.hasSelection() && !model_.readOnly();
                s.copy = model_.hasSelection();
                s.paste = !model_.readOnly() && clipboardText(hwnd_).has_value();
                s.del = model_.hasSelection() && !model_.readOnly();
                s.select_all = !model_.text().empty();
                return s;
            },
            [this](EditContextMenu::Command c) { runMenuCommand(c); });
    }
    context_menu_->show(this, local);
    return true;
}

void TextInput::runMenuCommand(EditContextMenu::Command c) {
    using C = EditContextMenu::Command;
    const std::size_t old = store_->u16Length();
    const int rev = model_.revision();
    switch (c) {
        case C::Undo: model_.undo(); break;
        case C::Cut: {
            std::string out;
            if (model_.hasSelection() && !model_.readOnly() && model_.cut(&out)) setClipboardText(hwnd_, out);
            break;
        }
        case C::Copy:
            if (model_.hasSelection()) setClipboardText(hwnd_, model_.selectedText());
            break;
        case C::Paste:
            if (auto t = clipboardText(hwnd_)) model_.insert(*t);
            break;
        case C::Delete: model_.deleteForward(false); break;
        case C::SelectAll: model_.selectAll(); break;
    }
    if (model_.revision() != rev) changed(old);
    else selectionMoved();
}

bool TextInput::charEvent(char32_t c) {
    if (c < 0x20 || c == 0x7F) return false;  // control characters arrive as keys
    const std::size_t old = store_->u16Length();
    const int rev = model_.revision();
    if (!model_.insert(utf8Of(c), true)) return true;  // read-only, or refused by a validator: swallowed
    if (model_.revision() != rev) changed(old);
    return true;
}

void TextInput::focusChanged(bool in, FocusReason why) {
    if (tsf_ && doc_) tsf_->SetFocus(in ? doc_.Get() : blank_doc_.Get());
    restartBlink();
    update();
    if (!in) focusLost();
    if (on_focus_changed) on_focus_changed(in, why);
}

}  // namespace tcad::ui
