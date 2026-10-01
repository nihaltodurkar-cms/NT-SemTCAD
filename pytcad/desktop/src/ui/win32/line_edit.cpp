#include "ui/win32/line_edit.hpp"

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

LineEdit::LineEdit(HWND window)
    : hwnd_(window),
      model_([this](std::string_view s) {
          TextEngine* te = textEngine();
          return te ? te->caretStops(s, style()) : codePointStops(s);  // before it is in a window
      }) {
    setFocusPolicy(FocusPolicy::Strong);
    setCursor(Cursor::IBeam);
    setSizePolicy({SizePolicy::Expanding, SizePolicy::Fixed});  // QLineEdit's defaults
    TextStore::Geometry geo;
    geo.range_screen = [this](std::size_t a, std::size_t b) { return rangeScreenRect(a, b); };
    geo.control_screen = [this] { return screenRect(); };
    geo.offset_at = [this](POINT screen) { return offsetAtScreen(screen); };
    store_ = new TextStore(model_, hwnd_, std::move(geo));
    setupTsf();
}

LineEdit::~LineEdit() {
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

RECT LineEdit::rangeScreenRect(std::size_t a, std::size_t b) const {
    const RectI w = windowRect();
    const double s = scale();
    const float x0 = caretX(a) - scroll_ + kPadding, x1 = caretX(b) - scroll_ + kPadding;
    POINT tl{w.x + static_cast<LONG>(std::lround(std::min(x0, x1) * s)), w.y + static_cast<LONG>(std::lround(textTop() * s))};
    POINT br{w.x + static_cast<LONG>(std::lround(std::max(x0, x1) * s)) + 1,
             w.y + static_cast<LONG>(std::lround((textTop() + lineHeight()) * s))};
    ClientToScreen(hwnd_, &tl);
    ClientToScreen(hwnd_, &br);
    return RECT{tl.x, tl.y, br.x, br.y};
}

RECT LineEdit::screenRect() const {
    const RectI w = windowRect();
    POINT tl{w.x, w.y}, br{w.right(), w.bottom()};
    ClientToScreen(hwnd_, &tl);
    ClientToScreen(hwnd_, &br);
    return RECT{tl.x, tl.y, br.x, br.y};
}

std::size_t LineEdit::offsetAtScreen(POINT screen) const {
    ScreenToClient(hwnd_, &screen);
    const RectI w = windowRect();
    const double s = scale();
    return offsetAt({static_cast<float>((screen.x - w.x) / s), static_cast<float>((screen.y - w.y) / s)});
}

bool LineEdit::accessibleSetValue(std::string_view v) {
    if (model_.readOnly()) return false;
    setText(v);
    return true;
}

void LineEdit::setupTsf() {
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

TextStyle LineEdit::style() const {
    TextStyle s;
    s.color = token(isEnabled() ? T::Text : T::TextFaint);
    s.valign = VAlign::Top;
    return s;
}

float LineEdit::lineHeight() const {
    TextEngine* te = textEngine();
    return te ? te->measure("", style()).height : 16.0f;
}

float LineEdit::textTop() const { return std::floor((sizeDips().height - lineHeight()) / 2); }

SizeF LineEdit::sizeHint() const {
    return {std::ceil(160.0f), std::ceil(lineHeight() + 2 * kPadding)};  // QLineEdit is about 17 characters wide
}

float LineEdit::caretX(std::size_t offset) const {
    TextEngine* te = textEngine();
    return te ? te->caretRect(model_.text(), style(), 0, offset).x : 0.0f;
}

std::size_t LineEdit::offsetAt(PointF local) const {
    TextEngine* te = textEngine();
    if (!te) return 0;
    return model_.snap(te->hitTest(model_.text(), style(), 0, {local.x - kPadding + scroll_, local.y - textTop()}).offset);
}

void LineEdit::ensureCaretVisible() {
    const float inner = std::max(1.0f, sizeDips().width - 2 * kPadding);
    const float x = caretX(model_.caret());
    if (x - scroll_ > inner) scroll_ = x - inner;
    if (x - scroll_ < 0) scroll_ = x;
    TextEngine* te = textEngine();
    const float full = te ? te->measure(model_.text(), style()).width : 0;
    scroll_ = std::clamp(scroll_, 0.0f, std::max(0.0f, full - inner));
}

void LineEdit::restartBlink() {
    caret_on_ = true;
    if (blink_) stopTimer(blink_);
    blink_ = 0;
    if (blinking_ && hasFocus()) {
        const UINT ms = GetCaretBlinkTime();
        if (ms != INFINITE && ms > 0)
            blink_ = startTimer(static_cast<int>(ms), true, [this] {
                caret_on_ = !caret_on_;
                update();
            });
    }
}

void LineEdit::setCaretBlinking(bool on) {
    blinking_ = on;
    restartBlink();
    update();
}

void LineEdit::changed(std::size_t old_u16_len) {
    store_->notifyChanged(0, old_u16_len, store_->u16Length());  // the whole text: simple and always correct
    ensureCaretVisible();
    restartBlink();
    update();
    if (on_text_changed) on_text_changed();
    if (on_accessible_value_changed) on_accessible_value_changed();
}

void LineEdit::setText(std::string_view utf8) {
    const std::size_t old = store_->u16Length();
    model_.setText(utf8);
    scroll_ = 0;
    changed(old);
}

void LineEdit::paint(Painter& p) {
    const SizeF s = sizeDips();
    p.fillRect({0, 0, s.width, s.height}, token(T::Base));
    const auto border = p.crisp({0, 0, s.width, s.height}, invalid_ ? 2.0f : 1.0f);
    p.strokeRect(border.rect, token(invalid_ ? T::Error : hasFocus() ? T::Focus : T::BorderStrong), border.width);
    p.save();
    p.clipRect({kPadding - 1, 1, std::max(0.0f, s.width - 2 * kPadding + 2), std::max(0.0f, s.height - 2)});
    const float ox = kPadding - scroll_, oy = textTop(), lh = lineHeight();
    if (model_.hasSelection()) {
        const auto [a, b] = model_.selection();
        const float x0 = caretX(a), x1 = caretX(b);
        p.fillRect({ox + std::min(x0, x1), oy, std::fabs(x1 - x0), lh}, token(hasFocus() ? T::Selection : T::AlternateBase));
    }
    TextStyle st = style();
    p.drawText({ox, oy, 100000.0f, lh}, model_.text(), st);
    if (model_.composing()) {  // the IME's composition, underlined as Windows edits do
        const auto [a, b] = model_.composition();
        const float x0 = caretX(a), x1 = caretX(b);
        p.drawLine({ox + std::min(x0, x1), oy + lh - 1}, {ox + std::max(x0, x1), oy + lh - 1}, token(T::Text), 1.0f);
    }
    if (hasFocus() && caret_on_) {
        const float x = std::round((ox + caretX(model_.caret())) * static_cast<float>(p.scale())) / static_cast<float>(p.scale());
        p.fillRect({x, oy, 1.0f / static_cast<float>(p.scale()), lh}, token(T::Text));
    }
    p.restore();
}

bool LineEdit::mouseEvent(const UiMouseEvent& e) {
    using platform::MouseButton;
    using platform::MouseType;
    if (e.type == MouseType::Down && e.button == MouseButton::Left) {
        const std::size_t at = offsetAt(e.pos);
        model_.setSelection(any(e.mods & Mod::Shift) ? model_.anchor() : at, at);
    } else if (e.type == MouseType::Move && (e.buttons & (1u << static_cast<int>(MouseButton::Left)))) {
        model_.setSelection(model_.anchor(), offsetAt(e.pos));
    } else if (e.type != MouseType::Up) {
        return e.type == MouseType::Down;  // other buttons: taken, nothing to do yet (context menu: N3)
    }
    store_->notifySelectionChanged();
    ensureCaretVisible();
    restartBlink();
    update();
    return true;
}

bool LineEdit::overridesShortcut(const KeyEvent& e) const {
    if (e.mods == Mod::Ctrl) return e.vk == 'A' || e.vk == 'C' || e.vk == 'X' || e.vk == 'V' || e.vk == 'Z' || e.vk == 'Y';
    return e.mods == (Mod::Ctrl | Mod::Shift) && e.vk == 'Z';
}

bool LineEdit::keyEvent(const KeyEvent& e) {
    if (!e.down) return false;
    const bool ctrl = any(e.mods & Mod::Ctrl), shift = any(e.mods & Mod::Shift);
    if (any(e.mods & Mod::Alt)) return false;
    const std::size_t old = store_->u16Length();
    const int rev = model_.revision();
    bool handled = true;
    switch (e.vk) {
        case VK_LEFT: model_.moveLeft(ctrl, shift); break;
        case VK_RIGHT: model_.moveRight(ctrl, shift); break;
        case VK_HOME: model_.home(shift); break;
        case VK_END: model_.end(shift); break;
        case VK_BACK: model_.backspace(ctrl); break;
        case VK_DELETE: model_.deleteForward(ctrl); break;
        case VK_RETURN:
            if (on_editing_finished) on_editing_finished();
            break;
        default:
            if (!ctrl) return false;
            if (e.vk == 'A') model_.selectAll();
            else if (e.vk == 'C') {
                if (model_.hasSelection()) setClipboardText(hwnd_, model_.selectedText());
            } else if (e.vk == 'X') {
                std::string cut;
                if (model_.hasSelection() && !model_.readOnly() && model_.cut(&cut)) setClipboardText(hwnd_, cut);
            } else if (e.vk == 'V') {
                if (auto t = clipboardText(hwnd_)) model_.insert(*t);
            } else if (e.vk == 'Z') {
                if (shift) model_.redo();
                else model_.undo();
            } else if (e.vk == 'Y') model_.redo();
            else handled = false;
    }
    if (!handled) return false;
    if (model_.revision() != rev) changed(old);
    else {
        store_->notifySelectionChanged();
        ensureCaretVisible();
        restartBlink();
        update();
    }
    return true;
}

bool LineEdit::charEvent(char32_t c) {
    if (c < 0x20 || c == 0x7F) return false;  // control characters arrive as keys
    const std::size_t old = store_->u16Length();
    if (!model_.insert(utf8Of(c), true)) return true;  // read-only: swallowed
    changed(old);
    return true;
}

void LineEdit::focusChanged(bool in, FocusReason why) {
    if (tsf_ && doc_) tsf_->SetFocus(in ? doc_.Get() : blank_doc_.Get());
    restartBlink();
    update();
    if (!in && on_editing_finished) on_editing_finished();  // QLineEdit: editingFinished on focus loss too
    if (on_focus_changed) on_focus_changed(in, why);
}

void LineEdit::setInvalid(bool on) {
    if (invalid_ == on) return;
    invalid_ = on;
    update();
}

}  // namespace tcad::ui
