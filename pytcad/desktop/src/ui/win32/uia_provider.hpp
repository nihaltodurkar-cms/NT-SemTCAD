// UI Automation for the native UI framework (N2f, NATIVE-DESKTOP-PLAN.md 27.7): one provider element per widget, over
// the live tree, so Narrator, Accessibility Insights and UIA test clients see the same structure the user sees.
//
//   structure  IRawElementProviderFragment(Root): parent / siblings / children over VISIBLE widgets; the root is
//              hosted by the window (UiaHostProviderFromHwnd), so the element tree hangs under the HWND's element
//   properties control type (from Widget::accessibleRole), Name (accessibleName), AutomationId (Widget::name),
//              help text (toolTip), enabled, focusable, focused, offscreen, bounding rectangle, framework "TCAD"
//   patterns   Invoke (Button, MenuItem), Toggle (accessibleToggleState() >= 0), Value (accessibleHasValue()),
//              SelectionItem (accessibleSelectionState() >= 0: radio buttons, N3a), and Text for a LineEdit (document, selection, point, units Character / Word / Line..Document, find, select)
//   events     focus changed (from the InputRouter), Value property changed and text changed for edits -- raised only
//              when UiaClientsAreListening()
// A widget that dies or leaves the tree disconnects its element (UiaDisconnectProvider); a client still holding it
// gets UIA_E_ELEMENTNOTAVAILABLE, never a dangling pointer. Not raised yet: structure-changed events (N3/N4 add and
// remove panels at run time; N2's trees are built once).
#pragma once

#include "ui/core/input_router.hpp"
#include "ui/core/widget.hpp"

#include <UIAutomation.h>
#include <windows.h>

#include <map>
#include <string>
#include <string_view>

namespace tcad::ui {

class UiaElement;

class UiaHost {
public:
    UiaHost(HWND hwnd, Widget& root, InputRouter& router);
    ~UiaHost();  // disconnects every element
    UiaHost(const UiaHost&) = delete;
    UiaHost& operator=(const UiaHost&) = delete;

    // WM_GETOBJECT: the root element for UiaRootObjectId, nothing otherwise.
    bool onGetObject(WPARAM wp, LPARAM lp, LRESULT* result);
    UiaElement* element(Widget* w);  // get or create; not AddRef'd (the host holds one reference)
    void widgetGone(Widget* w);
    void focusChanged(Widget* w);
    void valueChanged(Widget* w);
    void rangeChanged(Widget* w);  // a slider's or spin box's RangeValue changed (or a combo box's value)
    void expandChanged(Widget* w); // a drop-down opened or closed
    void announce(Widget* w, std::string_view text);  // UIA's notification event: a screen reader speaks it
    HWND hwnd() const { return hwnd_; }
    Widget& root() const { return root_; }
    InputRouter& router() const { return router_; }
    int eventsAttempted() const { return events_; }  // tests: the wiring ran (whether or not a client listens)
    int announcements() const { return announcements_; }
    const std::string& lastAnnouncement() const { return last_announcement_; }

private:
    HWND hwnd_;
    Widget& root_;
    InputRouter& router_;
    std::map<Widget*, UiaElement*> elements_;
    int next_id_ = 1;
    int events_ = 0;
    int announcements_ = 0;
    std::string last_announcement_;
};

class UiaElement final : public IRawElementProviderSimple,
                         public IRawElementProviderFragment,
                         public IRawElementProviderFragmentRoot,
                         public IInvokeProvider,
                         public IToggleProvider,
                         public IValueProvider,
                         public IRangeValueProvider,
                         public IExpandCollapseProvider,
                         public ISelectionItemProvider,
                         public ITextProvider {
public:
    UiaElement(UiaHost* host, Widget* w, int id) : host_(host), w_(w), id_(id) {}
    Widget* widget() const { return w_; }
    void detach() { w_ = nullptr; host_ = nullptr; }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** out) override;
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override;
    // IRawElementProviderSimple
    STDMETHODIMP get_ProviderOptions(ProviderOptions* o) override;
    STDMETHODIMP GetPatternProvider(PATTERNID id, IUnknown** out) override;
    STDMETHODIMP GetPropertyValue(PROPERTYID id, VARIANT* v) override;
    STDMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** out) override;
    // IRawElementProviderFragment
    STDMETHODIMP Navigate(NavigateDirection d, IRawElementProviderFragment** out) override;
    STDMETHODIMP GetRuntimeId(SAFEARRAY** out) override;
    STDMETHODIMP get_BoundingRectangle(UiaRect* r) override;
    STDMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** out) override;
    STDMETHODIMP SetFocus() override;
    STDMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** out) override;
    // IRawElementProviderFragmentRoot
    STDMETHODIMP ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) override;
    STDMETHODIMP GetFocus(IRawElementProviderFragment** out) override;
    // IInvokeProvider
    STDMETHODIMP Invoke() override;
    // IToggleProvider
    STDMETHODIMP Toggle() override;
    STDMETHODIMP get_ToggleState(ToggleState* s) override;
    // IValueProvider
    STDMETHODIMP SetValue(LPCWSTR v) override;
    STDMETHODIMP get_Value(BSTR* v) override;
    STDMETHODIMP get_IsReadOnly(BOOL* r) override;  // also IRangeValueProvider's (the same signature)
    // IExpandCollapseProvider (combo boxes)
    STDMETHODIMP Expand() override;
    STDMETHODIMP Collapse() override;
    STDMETHODIMP get_ExpandCollapseState(ExpandCollapseState* s) override;
    // IRangeValueProvider (sliders and spin boxes; Widget::accessibleRange)
    STDMETHODIMP SetValue(double v) override;
    STDMETHODIMP get_Value(double* v) override;
    STDMETHODIMP get_Minimum(double* v) override;
    STDMETHODIMP get_Maximum(double* v) override;
    STDMETHODIMP get_LargeChange(double* v) override;
    STDMETHODIMP get_SmallChange(double* v) override;
    // ISelectionItemProvider (a radio button: selecting it checks it; it cannot be removed from the selection)
    STDMETHODIMP Select() override;
    STDMETHODIMP AddToSelection() override;
    STDMETHODIMP RemoveFromSelection() override;
    STDMETHODIMP get_IsSelected(BOOL* r) override;
    STDMETHODIMP get_SelectionContainer(IRawElementProviderSimple** out) override;
    // ITextProvider
    STDMETHODIMP GetSelection(SAFEARRAY** out) override;
    STDMETHODIMP GetVisibleRanges(SAFEARRAY** out) override;
    STDMETHODIMP RangeFromChild(IRawElementProviderSimple*, ITextRangeProvider** out) override;
    STDMETHODIMP RangeFromPoint(UiaPoint p, ITextRangeProvider** out) override;
    STDMETHODIMP get_DocumentRange(ITextRangeProvider** out) override;
    STDMETHODIMP get_SupportedTextSelection(SupportedTextSelection* s) override;

private:
    ~UiaElement() = default;
    IRawElementProviderFragment* wrap(Widget* w);  // AddRef'd element of w, or null
    class LineEdit* edit() const;

    LONG refs_ = 1;
    UiaHost* host_;
    Widget* w_;
    int id_;
};

}  // namespace tcad::ui
