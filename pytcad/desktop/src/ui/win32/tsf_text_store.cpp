#include "ui/win32/tsf_text_store.hpp"

#include "ui/win32/dwrite_text.hpp"

#include <olectl.h>

#include <algorithm>

namespace tcad::ui {

namespace {

std::string toUtf8(const WCHAR* w, ULONG n) {
    if (!n) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(n), s.data(), len, nullptr, nullptr);
    return s;
}

}  // namespace

TextStore::TextStore(EditModel& model, HWND hwnd, Geometry geometry) : model_(model), hwnd_(hwnd), geo_(std::move(geometry)) {}

ULONG TextStore::Release() {
    const LONG r = InterlockedDecrement(&refs_);
    if (!r) {
        if (sink_) sink_->Release();
        delete this;
    }
    return static_cast<ULONG>(r);
}

STDMETHODIMP TextStore::QueryInterface(REFIID riid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (riid == IID_IUnknown || riid == __uuidof(ITextStoreACP2)) *out = static_cast<ITextStoreACP2*>(this);
    else if (riid == __uuidof(ITfContextOwnerCompositionSink)) *out = static_cast<ITfContextOwnerCompositionSink*>(this);
    else return E_NOINTERFACE;
    AddRef();
    return S_OK;
}

// -- positions ----------------------------------------------------------------------------------------------------

std::size_t TextStore::u16Length() const { return toUtf16(model_.text()).size(); }

std::size_t TextStore::u16Of(std::size_t u8) const {
    return toUtf16(std::string_view(model_.text()).substr(0, std::min(u8, model_.text().size()))).size();
}

std::size_t TextStore::u8Of(LONG acp) const {
    const std::string& t = model_.text();
    std::size_t units = 0, i = 0;
    while (i < t.size() && units < static_cast<std::size_t>(std::max<LONG>(acp, 0))) {
        const unsigned char c = static_cast<unsigned char>(t[i]);
        const std::size_t n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        units += n == 4 ? 2 : 1;  // 4-byte sequences are surrogate pairs
        i = std::min(t.size(), i + n);
    }
    return i;
}

// -- sink and locks -----------------------------------------------------------------------------------------------

STDMETHODIMP TextStore::AdviseSink(REFIID riid, IUnknown* punk, DWORD mask) {
    if (riid != IID_ITextStoreACPSink || !punk) return E_INVALIDARG;
    if (sink_) {  // the same sink may update its mask; another one is refused
        IUnknown *a = nullptr, *b = nullptr;
        sink_->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&a));
        punk->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&b));
        const bool same = a == b;
        if (a) a->Release();
        if (b) b->Release();
        if (!same) return CONNECT_E_ADVISELIMIT;
        sink_mask_ = mask;
        return S_OK;
    }
    if (FAILED(punk->QueryInterface(IID_ITextStoreACPSink, reinterpret_cast<void**>(&sink_)))) return E_NOINTERFACE;
    sink_mask_ = mask;
    return S_OK;
}

STDMETHODIMP TextStore::UnadviseSink(IUnknown*) {
    if (!sink_) return CONNECT_E_NOCONNECTION;
    sink_->Release();
    sink_ = nullptr;
    sink_mask_ = 0;
    return S_OK;
}

void TextStore::grant(DWORD flags, HRESULT* session) {
    lock_ = flags & TS_LF_READWRITE;
    ++grants_;
    *session = sink_->OnLockGranted(lock_);
    lock_ = 0;
}

STDMETHODIMP TextStore::RequestLock(DWORD flags, HRESULT* session) {
    if (!sink_) return E_UNEXPECTED;
    if (!session) return E_INVALIDARG;
    if (lock_) {  // already locked: a sync request cannot wait; an async one is queued for when this lock ends
        if (flags & TS_LF_SYNC) {
            *session = TS_E_SYNCHRONOUS;
            return S_OK;
        }
        queued_ |= flags & TS_LF_READWRITE;
        *session = TS_S_ASYNC;
        return S_OK;
    }
    grant(flags, session);
    while (queued_) {  // the queued request is granted now, as the documentation requires
        const DWORD q = queued_;
        queued_ = 0;
        HRESULT ignored = S_OK;
        grant(q, &ignored);
    }
    return S_OK;
}

void TextStore::notifyChanged(std::size_t start, std::size_t old_end, std::size_t new_end) {
    if (!sink_ || lock_) return;
    TS_TEXTCHANGE tc{static_cast<LONG>(start), static_cast<LONG>(old_end), static_cast<LONG>(new_end)};
    sink_->OnTextChange(0, &tc);
    notifySelectionChanged();
}

void TextStore::notifySelectionChanged() {
    if (sink_ && !lock_) sink_->OnSelectionChange();
}

void TextStore::notifyLayoutChanged() {
    if (sink_ && !lock_) sink_->OnLayoutChange(TS_LC_CHANGE, 1);
}

// -- status and text ----------------------------------------------------------------------------------------------

STDMETHODIMP TextStore::GetStatus(TS_STATUS* status) {
    if (!status) return E_INVALIDARG;
    status->dwDynamicFlags = model_.readOnly() ? TS_SD_READONLY : 0;
    status->dwStaticFlags = TS_SS_NOHIDDENTEXT;
    return S_OK;
}

STDMETHODIMP TextStore::QueryInsert(LONG start, LONG end, ULONG, LONG* rstart, LONG* rend) {
    const LONG len = static_cast<LONG>(u16Length());
    if (start < 0 || start > end || end > len) return E_INVALIDARG;
    if (!rstart || !rend) return E_INVALIDARG;
    *rstart = start;
    *rend = end;
    return S_OK;
}

STDMETHODIMP TextStore::GetSelection(ULONG index, ULONG, TS_SELECTION_ACP* sel, ULONG* fetched) {
    if (!canRead()) return TS_E_NOLOCK;
    if (!sel || !fetched) return E_INVALIDARG;
    if (index != TF_DEFAULT_SELECTION && index != 0) return E_INVALIDARG;
    const auto [a, b] = model_.selection();
    sel->acpStart = static_cast<LONG>(u16Of(a));
    sel->acpEnd = static_cast<LONG>(u16Of(b));
    sel->style.ase = model_.caret() == a && a != b ? TS_AE_START : TS_AE_END;
    sel->style.fInterimChar = FALSE;
    *fetched = 1;
    return S_OK;
}

STDMETHODIMP TextStore::SetSelection(ULONG count, const TS_SELECTION_ACP* sel) {
    if (!canWrite()) return TS_E_NOLOCK;
    if (count != 1 || !sel) return E_INVALIDARG;
    ++set_selection_calls_;
    const std::size_t a = u8Of(sel->acpStart), b = u8Of(sel->acpEnd);
    if (sel->style.ase == TS_AE_START) model_.setSelection(b, a);
    else model_.setSelection(a, b);
    return S_OK;
}

STDMETHODIMP TextStore::GetText(LONG start, LONG end, WCHAR* plain, ULONG plain_req, ULONG* plain_ret, TS_RUNINFO* runs,
                                ULONG runs_req, ULONG* runs_ret, LONG* next) {
    if (!canRead()) return TS_E_NOLOCK;
    const std::wstring w = toUtf16(model_.text());
    const LONG len = static_cast<LONG>(w.size());
    if (end == -1) end = len;
    if (start < 0 || start > len || end < start || end > len) return TF_E_INVALIDPOS;
    ULONG n = static_cast<ULONG>(end - start);
    if (plain && plain_req) n = std::min(n, plain_req);
    else if (!plain) n = std::min(n, static_cast<ULONG>(end - start));
    if (plain && plain_req) std::copy_n(w.begin() + start, n, plain);
    if (plain_ret) *plain_ret = plain && plain_req ? n : 0;
    if (runs && runs_req) {
        runs[0].uCount = n;
        runs[0].type = TS_RT_PLAIN;
        if (runs_ret) *runs_ret = 1;
    } else if (runs_ret) {
        *runs_ret = 0;
    }
    if (next) *next = start + static_cast<LONG>(n);
    return S_OK;
}

STDMETHODIMP TextStore::SetText(DWORD, LONG start, LONG end, const WCHAR* text, ULONG cch, TS_TEXTCHANGE* change) {
    if (!canWrite()) return TS_E_NOLOCK;
    if (model_.readOnly()) return TS_E_READONLY;
    const LONG len = static_cast<LONG>(u16Length());
    if (start < 0 || start > end || end > len) return TF_E_INVALIDPOS;
    ++text_writes_;
    model_.replaceRange(u8Of(start), u8Of(end), toUtf8(text, cch));
    if (change) {
        change->acpStart = start;
        change->acpOldEnd = end;
        change->acpNewEnd = start + static_cast<LONG>(cch);
    }
    return S_OK;
}

STDMETHODIMP TextStore::QueryInsertEmbedded(const GUID*, const FORMATETC*, BOOL* ok) {
    if (!ok) return E_INVALIDARG;
    *ok = FALSE;  // plain text only
    return S_OK;
}

STDMETHODIMP TextStore::InsertTextAtSelection(DWORD flags, const WCHAR* text, ULONG cch, LONG* start, LONG* end, TS_TEXTCHANGE* change) {
    const auto [a, b] = model_.selection();
    const LONG s = static_cast<LONG>(u16Of(a)), e = static_cast<LONG>(u16Of(b));
    if (flags & TS_IAS_QUERYONLY) {  // where the text WOULD go; no lock needed beyond read
        if (!canRead()) return TS_E_NOLOCK;
        if (start) *start = s;
        if (end) *end = e;
        return S_OK;
    }
    if (!canWrite()) return TS_E_NOLOCK;
    if (model_.readOnly()) return TS_E_READONLY;
    model_.replaceRange(a, b, toUtf8(text, cch));
    if (!(flags & TS_IAS_NOQUERY)) {
        if (start) *start = s;
        if (end) *end = s + static_cast<LONG>(cch);
    }
    if (change) {
        change->acpStart = s;
        change->acpOldEnd = e;
        change->acpNewEnd = s + static_cast<LONG>(cch);
    }
    return S_OK;
}

STDMETHODIMP TextStore::FindNextAttrTransition(LONG, LONG halt, ULONG, const TS_ATTRID*, DWORD, LONG* next, BOOL* found, LONG* offset) {
    if (next) *next = halt;
    if (found) *found = FALSE;
    if (offset) *offset = 0;
    return S_OK;
}

STDMETHODIMP TextStore::RetrieveRequestedAttrs(ULONG, TS_ATTRVAL*, ULONG* fetched) {
    if (fetched) *fetched = 0;
    return S_OK;
}

STDMETHODIMP TextStore::GetEndACP(LONG* acp) {
    if (!canRead()) return TS_E_NOLOCK;
    if (!acp) return E_INVALIDARG;
    *acp = static_cast<LONG>(u16Length());
    return S_OK;
}

STDMETHODIMP TextStore::GetActiveView(TsViewCookie* view) {
    if (!view) return E_INVALIDARG;
    *view = 1;
    return S_OK;
}

STDMETHODIMP TextStore::GetACPFromPoint(TsViewCookie, const POINT* pt, DWORD, LONG* acp) {
    if (!pt || !acp) return E_INVALIDARG;
    if (!geo_.offset_at) return E_NOTIMPL;
    *acp = static_cast<LONG>(u16Of(geo_.offset_at(*pt)));
    return S_OK;
}

STDMETHODIMP TextStore::GetTextExt(TsViewCookie, LONG start, LONG end, RECT* rc, BOOL* clipped) {
    if (!canRead()) return TS_E_NOLOCK;
    if (!rc || !clipped) return E_INVALIDARG;
    *rc = geo_.range_screen ? geo_.range_screen(u8Of(start), u8Of(end)) : RECT{};
    *clipped = FALSE;
    return S_OK;
}

STDMETHODIMP TextStore::GetScreenExt(TsViewCookie, RECT* rc) {
    if (!rc) return E_INVALIDARG;
    *rc = geo_.control_screen ? geo_.control_screen() : RECT{};
    return S_OK;
}

// -- composition ------------------------------------------------------------------------------------------------

void TextStore::compositionFrom(ITfRange* range) {
    ITfRangeACP* acp = nullptr;
    LONG s = 0, n = 0;
    if (range && SUCCEEDED(range->QueryInterface(IID_ITfRangeACP, reinterpret_cast<void**>(&acp)))) {
        acp->GetExtent(&s, &n);
        acp->Release();
        model_.setComposition(u8Of(s), u8Of(s + n));
    }
}

STDMETHODIMP TextStore::OnStartComposition(ITfCompositionView* view, BOOL* ok) {
    if (!ok) return E_INVALIDARG;
    *ok = TRUE;
    ITfRange* r = nullptr;
    if (view && SUCCEEDED(view->GetRange(&r))) {
        compositionFrom(r);
        r->Release();
    }
    return S_OK;
}

STDMETHODIMP TextStore::OnUpdateComposition(ITfCompositionView*, ITfRange* range) {
    compositionFrom(range);
    return S_OK;
}

STDMETHODIMP TextStore::OnEndComposition(ITfCompositionView*) {
    model_.clearComposition();
    return S_OK;
}

}  // namespace tcad::ui
