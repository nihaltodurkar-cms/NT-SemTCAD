// The Text Services Framework text store over an EditModel (N2e, decision 27.7-2: full TSF, not IMM32). IMEs,
// dictation, the touch keyboard and reconversion read and write the edit through it.
//
// Implemented: the ITextStoreACP2 lock protocol (RequestLock: synchronous and asynchronous, one queued async
// request granted when the current lock ends; TS_E_SYNCHRONOUS when a sync lock is refused), GetStatus, selection get/
// set, GetText/SetText/InsertTextAtSelection (with TS_IAS_QUERYONLY/NOQUERY), QueryInsert, GetEndACP, GetActiveView,
// GetTextExt/GetScreenExt/GetACPFromPoint through the owner's geometry callbacks, change notifications to the sink
// for edits the APP makes (typing without an IME, undo, programmatic text), and ITfContextOwnerCompositionSink to
// track the composition range in the model. Not implemented (no embedded objects or text attributes in this app):
// the embedded-object calls (TS_E_NOOBJECT / E_NOTIMPL) and attribute retrieval (no attributes reported).
//
// Positions: TSF counts UTF-16 units (ACPs); the model counts UTF-8 bytes. Every call maps between them.
#pragma once

#include "ui/core/edit_model.hpp"

#include <msctf.h>
#include <textstor.h>
#include <windows.h>

#include <functional>
#include <string>

namespace tcad::ui {

class TextStore final : public ITextStoreACP2, public ITfContextOwnerCompositionSink {
public:
    struct Geometry {
        std::function<RECT(std::size_t u8_start, std::size_t u8_end)> range_screen;  // screen rect of a text range
        std::function<RECT()> control_screen;                                         // the edit's screen rect
        std::function<std::size_t(POINT screen)> offset_at;                           // hit test (UTF-8 offset)
    };
    TextStore(EditModel& model, HWND hwnd, Geometry geometry);

    // The app changed the text or selection outside a TSF lock: tell TSF (no-op without a sink or during a lock).
    void notifyChanged(std::size_t old_u16_start, std::size_t old_u16_end, std::size_t new_u16_end);
    void notifySelectionChanged();
    void notifyLayoutChanged();
    std::size_t u16Length() const;
    std::size_t u16Of(std::size_t u8) const;  // UTF-8 offset -> ACP
    std::size_t u8Of(LONG acp) const;          // ACP -> UTF-8 offset (clamped)
    bool hasSink() const { return sink_ != nullptr; }
    DWORD lockFlags() const { return lock_; }
    int grants() const { return grants_; }
    int setSelectionCalls() const { return set_selection_calls_; }
    int textWrites() const { return text_writes_; }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** out) override;
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override;
    // ITextStoreACP2
    STDMETHODIMP AdviseSink(REFIID riid, IUnknown* punk, DWORD mask) override;
    STDMETHODIMP UnadviseSink(IUnknown* punk) override;
    STDMETHODIMP RequestLock(DWORD flags, HRESULT* session) override;
    STDMETHODIMP GetStatus(TS_STATUS* status) override;
    STDMETHODIMP QueryInsert(LONG start, LONG end, ULONG cch, LONG* rstart, LONG* rend) override;
    STDMETHODIMP GetSelection(ULONG index, ULONG count, TS_SELECTION_ACP* sel, ULONG* fetched) override;
    STDMETHODIMP SetSelection(ULONG count, const TS_SELECTION_ACP* sel) override;
    STDMETHODIMP GetText(LONG start, LONG end, WCHAR* plain, ULONG plain_req, ULONG* plain_ret, TS_RUNINFO* runs,
                         ULONG runs_req, ULONG* runs_ret, LONG* next) override;
    STDMETHODIMP SetText(DWORD flags, LONG start, LONG end, const WCHAR* text, ULONG cch, TS_TEXTCHANGE* change) override;
    STDMETHODIMP GetFormattedText(LONG, LONG, IDataObject**) override { return E_NOTIMPL; }
    STDMETHODIMP GetEmbedded(LONG, REFGUID, REFIID, IUnknown**) override { return E_NOTIMPL; }
    STDMETHODIMP QueryInsertEmbedded(const GUID*, const FORMATETC*, BOOL* ok) override;
    STDMETHODIMP InsertEmbedded(DWORD, LONG, LONG, IDataObject*, TS_TEXTCHANGE*) override { return E_NOTIMPL; }
    STDMETHODIMP InsertTextAtSelection(DWORD flags, const WCHAR* text, ULONG cch, LONG* start, LONG* end, TS_TEXTCHANGE* change) override;
    STDMETHODIMP InsertEmbeddedAtSelection(DWORD, IDataObject*, LONG*, LONG*, TS_TEXTCHANGE*) override { return E_NOTIMPL; }
    STDMETHODIMP RequestSupportedAttrs(DWORD, ULONG, const TS_ATTRID*) override { return S_OK; }
    STDMETHODIMP RequestAttrsAtPosition(LONG, ULONG, const TS_ATTRID*, DWORD) override { return S_OK; }
    STDMETHODIMP RequestAttrsTransitioningAtPosition(LONG, ULONG, const TS_ATTRID*, DWORD) override { return S_OK; }
    STDMETHODIMP FindNextAttrTransition(LONG, LONG halt, ULONG, const TS_ATTRID*, DWORD, LONG* next, BOOL* found, LONG* offset) override;
    STDMETHODIMP RetrieveRequestedAttrs(ULONG, TS_ATTRVAL*, ULONG* fetched) override;
    STDMETHODIMP GetEndACP(LONG* acp) override;
    STDMETHODIMP GetActiveView(TsViewCookie* view) override;
    STDMETHODIMP GetACPFromPoint(TsViewCookie, const POINT* pt, DWORD, LONG* acp) override;
    STDMETHODIMP GetTextExt(TsViewCookie, LONG start, LONG end, RECT* rc, BOOL* clipped) override;
    STDMETHODIMP GetScreenExt(TsViewCookie, RECT* rc) override;
    // ITfContextOwnerCompositionSink
    STDMETHODIMP OnStartComposition(ITfCompositionView* view, BOOL* ok) override;
    STDMETHODIMP OnUpdateComposition(ITfCompositionView* view, ITfRange* range) override;
    STDMETHODIMP OnEndComposition(ITfCompositionView* view) override;

private:
    ~TextStore() = default;
    bool canRead() const { return (lock_ & TS_LF_READ) == TS_LF_READ; }
    bool canWrite() const { return (lock_ & TS_LF_READWRITE) == TS_LF_READWRITE; }
    void grant(DWORD flags, HRESULT* session);
    void compositionFrom(ITfRange* range);

    LONG refs_ = 1;
    EditModel& model_;
    HWND hwnd_;
    Geometry geo_;
    ITextStoreACPSink* sink_ = nullptr;  // owned reference
    DWORD sink_mask_ = 0;
    DWORD lock_ = 0;
    DWORD queued_ = 0;  // a pending asynchronous lock request
    int grants_ = 0;
    int set_selection_calls_ = 0;
    int text_writes_ = 0;
};

}  // namespace tcad::ui
