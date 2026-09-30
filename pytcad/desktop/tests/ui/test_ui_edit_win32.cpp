// N2e on Windows (NATIVE-DESKTOP-PLAN.md 27.7): the TSF text store driven through ITextStoreACP2 exactly as TSF calls
// it (a fake sink grants the locks), real TSF activation and focus, the bare LineEdit through real window messages
// (typing, clusters, word keys, selection, clipboard, undo, editing-finished, the mouse), and goldens of its states.
// Part of tcad_ui_render_tests. Real IMEs (Japanese, Pinyin), the emoji panel, dictation and the touch keyboard need a
// person: that checklist is in NATIVE-DESKTOP-PLAN.md 27.7.5.
#include "mini_test.hpp"
#include "render_test_support.hpp"
#include "ui_driver.hpp"

#include "ui/core/layout.hpp"
#include "ui/win32/clipboard.hpp"
#include "ui/win32/line_edit.hpp"
#include "ui/win32/ui_window.hpp"

#include <olectl.h>

#include <functional>
#include <iterator>
#include <string>
#include <vector>

using namespace tcad::ui;
using namespace tcad::ui::testing;
using tcad::platform::Mod;

namespace {

class FakeSink final : public ITextStoreACPSink {
public:
    std::function<void(DWORD)> on_lock;
    int text_changes = 0, selection_changes = 0;
    STDMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (riid == IID_IUnknown || riid == IID_ITextStoreACPSink) {
            *out = this;
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs; }
    STDMETHODIMP_(ULONG) Release() override { return --refs; }  // lives on the test's stack
    STDMETHODIMP OnTextChange(DWORD, const TS_TEXTCHANGE*) override { return ++text_changes, S_OK; }
    STDMETHODIMP OnSelectionChange() override { return ++selection_changes, S_OK; }
    STDMETHODIMP OnLayoutChange(TsLayoutCode, TsViewCookie) override { return S_OK; }
    STDMETHODIMP OnStatusChange(DWORD) override { return S_OK; }
    STDMETHODIMP OnAttrsChange(LONG, LONG, ULONG, const TS_ATTRID*) override { return S_OK; }
    STDMETHODIMP OnLockGranted(DWORD flags) override {
        if (on_lock) on_lock(flags);
        return S_OK;
    }
    STDMETHODIMP OnStartEditTransaction() override { return S_OK; }
    STDMETHODIMP OnEndEditTransaction() override { return S_OK; }
    ULONG refs = 1;
};

std::wstring textOf(TextStore& s) {
    WCHAR buf[64]{};
    ULONG got = 0, runs = 0;
    TS_RUNINFO ri[1];
    LONG next = 0;
    s.GetText(0, -1, buf, 64, &got, ri, 1, &runs, &next);
    return std::wstring(buf, got);
}

struct EditWindow {
    std::unique_ptr<UiWindow> w;
    LineEdit* edit = nullptr;
    LineEdit* other = nullptr;
    int finished = 0;
    explicit EditWindow(double scale = 1.0) {
        auto r = UiWindow::create(warp(), {.title = L"tcad_ui_edit_tests", .width = 300, .height = 120, .scale_override = scale});
        if (!r) return;
        w = std::move(*r);
        w->resizeClient(px(300, scale), px(120, scale));
        auto* col = w->root().setLayout<BoxLayout>(Orientation::Vertical);
        edit = col->add<LineEdit>(w->window().hwnd());
        other = col->add<LineEdit>(w->window().hwnd());
        col->addStretch(1);
        edit->on_editing_finished = [this] { ++finished; };
        edit->setCaretBlinking(false);
        other->setCaretBlinking(false);
        w->renderNow(nullptr, false);
    }
};

}  // namespace

TEST(the_text_store_follows_the_tsf_lock_protocol) {
    EditModel model;
    auto* store = new TextStore(model, nullptr, {});
    FakeSink sink, other;
    CHECK(SUCCEEDED(store->AdviseSink(IID_ITextStoreACPSink, &sink, TS_AS_ALL_SINKS)));
    CHECK_EQ(store->AdviseSink(IID_ITextStoreACPSink, &other, TS_AS_ALL_SINKS), CONNECT_E_ADVISELIMIT);
    WCHAR buf[8];
    ULONG got = 0;
    CHECK_EQ(store->GetText(0, -1, buf, 8, &got, nullptr, 0, nullptr, nullptr), TS_E_NOLOCK);  // no lock, no text
    HRESULT session = E_FAIL, nested_sync = S_OK, nested_async = S_OK;
    int async_ran = 0;
    sink.on_lock = [&](DWORD flags) {
        if (async_ran) return;
        CHECK(flags == TS_LF_READWRITE);
        LONG s = -1, e = -1;
        TS_TEXTCHANGE ch{};
        CHECK(SUCCEEDED(store->InsertTextAtSelection(0, L"日本語", 3, &s, &e, &ch)));  // 日本語
        CHECK(s == 0 && e == 3 && ch.acpNewEnd == 3);
        CHECK(model.text() == "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E" && model.caret() == 9);
        CHECK(textOf(*store) == L"日本語");
        TS_SELECTION_ACP sel{1, 2, {TS_AE_END, FALSE}};
        CHECK(SUCCEEDED(store->SetSelection(1, &sel)));
        CHECK(model.anchor() == 3 && model.caret() == 6);  // ACP 1..2 = bytes 3..6
        TS_SELECTION_ACP back{};
        ULONG n = 0;
        CHECK(SUCCEEDED(store->GetSelection(TF_DEFAULT_SELECTION, 1, &back, &n)) && n == 1 && back.acpStart == 1 && back.acpEnd == 2);
        CHECK(SUCCEEDED(store->SetText(0, 0, 1, L"X", 1, &ch)));
        CHECK(textOf(*store) == L"X本語");
        LONG end_acp = 0;
        CHECK(SUCCEEDED(store->GetEndACP(&end_acp)) && end_acp == 3);
        CHECK(SUCCEEDED(store->InsertTextAtSelection(TS_IAS_QUERYONLY, nullptr, 0, &s, &e, nullptr)) && textOf(*store) == L"X本語");
        store->RequestLock(TS_LF_READ | TS_LF_SYNC, &nested_sync);  // a sync lock inside a lock cannot be granted
        store->RequestLock(TS_LF_READWRITE, &nested_async);          // an async one is queued
        sink.on_lock = [&](DWORD) {
            ++async_ran;
            LONG a = 0, b = 0;
            store->InsertTextAtSelection(TS_IAS_NOQUERY, L"\U0001F600", 2, &a, &b, nullptr);  // a surrogate pair
        };
    };
    CHECK(SUCCEEDED(store->RequestLock(TS_LF_READWRITE | TS_LF_SYNC, &session)));
    CHECK(nested_sync == TS_E_SYNCHRONOUS && nested_async == TS_S_ASYNC);
    CHECK_EQ(async_ran, 1);  // granted as soon as the first lock ended
    CHECK_EQ(store->grants(), 2);
    CHECK_EQ(store->u16Length(), std::size_t{5});  // X 本 語 plus two units for the emoji
    CHECK(model.text().size() == 1 + 3 + 3 + 4);
    // app-side changes are reported to TSF; read-only refuses writes
    const int before = sink.text_changes;
    store->notifyChanged(0, 5, 5);
    CHECK(sink.text_changes == before + 1 && sink.selection_changes >= 1);
    model.setReadOnly(true);
    HRESULT ro = S_OK;
    sink.on_lock = [&](DWORD) {
        TS_TEXTCHANGE ch{};
        ro = store->SetText(0, 0, 1, L"Y", 1, &ch);
    };
    store->RequestLock(TS_LF_READWRITE | TS_LF_SYNC, &session);
    CHECK_EQ(ro, TS_E_READONLY);
    TS_STATUS status{};
    CHECK(SUCCEEDED(store->GetStatus(&status)) && (status.dwDynamicFlags & TS_SD_READONLY));
    CHECK(SUCCEEDED(store->UnadviseSink(&sink)));
    store->Release();
}

TEST(tsf_takes_our_store_and_follows_the_edit_focus) {
    EditWindow f;
    CHECK(f.w && f.edit);
    if (!f.w) return;
    CHECK(f.edit->tsfReady());
    CHECK(f.edit->textStore()->hasSink());  // TSF advised its sink on our store when the context was created
    f.edit->setFocus();
    ComPtr<ITfDocumentMgr> focused;
    CHECK(SUCCEEDED(f.edit->tsfThreadManager()->GetFocus(&focused)) && focused.Get() == f.edit->tsfDocument());
    f.other->setFocus();
    focused.Reset();
    f.edit->tsfThreadManager()->GetFocus(&focused);
    CHECK(focused.Get() == f.other->tsfDocument());  // and never the unfocused edit's
    CHECK_EQ(f.finished, 1);                         // leaving the edit finished its editing
}

TEST(typing_and_editing_keys_arrive_through_real_messages) {
    EditWindow f;
    CHECK(f.w && f.edit);
    if (!f.w) return;
    Driver d(*f.w);
    int window_ctrl_a = 0;
    f.w->router().shortcuts().add("Ctrl+A", [&] { ++window_ctrl_a; });
    d.click({20, 15});
    CHECK(f.edit->hasFocus());
    d.type(u"héllo \U0001F600");  // héllo 😀
    CHECK(f.edit->text() == "h\xC3\xA9llo \xF0\x9F\x98\x80");
    d.key(VK_BACK);  // the whole emoji
    CHECK(f.edit->text() == "h\xC3\xA9llo ");
    d.key(VK_BACK, Mod::Ctrl);  // Ctrl+Backspace: the word before
    CHECK(f.edit->text().empty());
    d.type(u"alpha beta");
    d.key(VK_LEFT, Mod::Ctrl);
    CHECK_EQ(f.edit->model().caret(), std::size_t{6});
    d.key(VK_END, Mod::Shift);
    CHECK(f.edit->model().selectedText() == "beta");
    d.key('A', Mod::Ctrl);  // the edit's select-all wins over the window's Ctrl+A
    CHECK(window_ctrl_a == 0 && f.edit->model().selectedText() == "alpha beta");
    d.key(VK_DELETE);
    CHECK(f.edit->text().empty());
    d.key('Z', Mod::Ctrl);
    CHECK(f.edit->text() == "alpha beta");
    d.key(VK_RETURN);
    CHECK_EQ(f.finished, 1);
    d.key(VK_TAB);  // focus moves on: editing finished again
    CHECK(f.other->hasFocus() && f.finished == 2);
}

TEST(copy_cut_and_paste_use_the_windows_clipboard) {
    // Only when the clipboard holds plain text or nothing: the test restores that text afterwards, and must never
    // destroy something it cannot restore (an image, files).
    if (!OpenClipboard(nullptr)) {
        std::printf("  SKIP: the clipboard is busy\n");
        return;
    }
    UINT fmt = 0;
    bool only_text = true;
    while ((fmt = EnumClipboardFormats(fmt)) != 0) {
        if (fmt == CF_UNICODETEXT || fmt == CF_TEXT || fmt == CF_OEMTEXT || fmt == CF_LOCALE) continue;
        // OLE's own bookkeeping when an app copies through OleSetClipboard: not user data
        wchar_t name[64]{};
        GetClipboardFormatNameW(fmt, name, 64);
        if (std::wstring(name) == L"DataObject" || std::wstring(name) == L"Ole Private Data") continue;
        only_text = false;
    }
    CloseClipboard();
    if (!only_text) {
        std::printf("  SKIP: the clipboard holds more than text; not overwriting it\n");
        return;
    }
    const auto saved = clipboardText(nullptr);
    EditWindow f;
    CHECK(f.w && f.edit);
    if (!f.w) return;
    Driver d(*f.w);
    d.click({20, 15});
    d.type(u"copy µm");
    d.key('A', Mod::Ctrl);
    d.key('C', Mod::Ctrl);
    CHECK(clipboardText(nullptr) == std::optional<std::string>("copy \xC2\xB5m"));
    d.key(VK_END);
    d.key('V', Mod::Ctrl);
    CHECK(f.edit->text() == "copy \xC2\xB5mcopy \xC2\xB5m");
    d.key(VK_HOME, Mod::Shift);
    d.key('X', Mod::Ctrl);
    CHECK(f.edit->text().empty() && clipboardText(nullptr) == std::optional<std::string>("copy \xC2\xB5mcopy \xC2\xB5m"));
    if (saved) setClipboardText(nullptr, *saved);
    else if (OpenClipboard(nullptr)) EmptyClipboard(), CloseClipboard();
}

TEST(the_mouse_places_the_caret_and_drags_a_selection) {
    EditWindow f;
    CHECK(f.w && f.edit);
    if (!f.w) return;
    Driver d(*f.w);
    f.edit->setText("abcdefgh");
    TextStyle st;
    const float x3 = f.w->text().caretRect("abcdefgh", st, 0, 3).x;
    const RectI g = f.edit->geometry();
    const float top = static_cast<float>(g.y) + 12;
    // the person at the machine holds Shift (simulated in this thread's keyboard state); the driver must still click
    BYTE keys[256];
    GetKeyboardState(keys);
    BYTE held[256];
    std::copy(std::begin(keys), std::end(keys), held);
    held[VK_SHIFT] |= 0x80;
    held[VK_LSHIFT] |= 0x80;
    SetKeyboardState(held);
    d.press({static_cast<float>(g.x) + LineEdit::kPadding + x3 + 0.5f, top});
    // a plain press: the anchor moves too (setText left it at 8). A Shift the person at the machine holds must not make
    // this a Shift+click -- the driver pins the modifiers of mouse messages (found: 12 of 30 runs failed before)
    CHECK(f.edit->model().caret() == 3 && f.edit->model().anchor() == 3);
    d.move({static_cast<float>(g.x) + 250, top});  // drag past the end
    d.release({static_cast<float>(g.x) + 250, top});
    CHECK((f.edit->model().selection() == std::pair<std::size_t, std::size_t>{3, 8}));
    d.press({static_cast<float>(g.x) + LineEdit::kPadding + 1, top}, tcad::platform::MouseButton::Left, false, Mod::Shift);
    d.release({static_cast<float>(g.x) + LineEdit::kPadding + 1, top});
    CHECK(f.edit->model().anchor() == 3 && f.edit->model().caret() == 0);  // Shift+click extends from the anchor
    SetKeyboardState(keys);
}

TEST(edit_states_match_the_goldens_at_three_scales) {
    for (int pct : {100, 150, 200}) {
        EditWindow f(pct / 100.0);
        CHECK(f.w && f.edit);
        if (!f.w) return;
        f.edit->setText("Selected text: 1.5 \xC2\xB5m");
        f.edit->setFocus();
        f.edit->model().setSelection(0, 8);  // "Selected", the caret at its end, visible (no blinking)
        f.other->setText("composing \xE6\x97\xA5\xE6\x9C\xAC");
        f.other->model().setComposition(10, 16);  // the IME's underline under the two CJK characters
        Image img;
        CHECK(f.w->renderNow(&img) == FrameStatus::Presented);
        const GoldenResult r = checkGolden(*warp(), "n2e_edit@" + std::to_string(pct) + ".png", img);
        CHECK(r != GoldenResult::Mismatch && r != GoldenResult::Missing);
    }
}
