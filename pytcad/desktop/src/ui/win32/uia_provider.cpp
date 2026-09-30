#include "ui/win32/uia_provider.hpp"

#include "ui/win32/dwrite_text.hpp"
#include "ui/win32/line_edit.hpp"

#include <UIAutomationCoreApi.h>

#include <algorithm>
#include <string>
#include <vector>

namespace tcad::ui {

namespace {

std::string toUtf8(const wchar_t* w) {
    if (!w || !*w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n > 0 ? n - 1 : 0), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

BSTR bstr(std::string_view utf8) {
    const std::wstring w = toUtf16(utf8);
    return SysAllocStringLen(w.data(), static_cast<UINT>(w.size()));
}

CONTROLTYPEID controlType(Role r) {
    switch (r) {
        case Role::Group: return UIA_GroupControlTypeId;
        case Role::Text: return UIA_TextControlTypeId;
        case Role::Edit: return UIA_EditControlTypeId;
        case Role::Button: return UIA_ButtonControlTypeId;
        case Role::CheckBox: return UIA_CheckBoxControlTypeId;
        case Role::RadioButton: return UIA_RadioButtonControlTypeId;
        case Role::ComboBox: return UIA_ComboBoxControlTypeId;
        case Role::Slider: return UIA_SliderControlTypeId;
        case Role::List: return UIA_ListControlTypeId;
        case Role::ListItem: return UIA_ListItemControlTypeId;
        case Role::Tree: return UIA_TreeControlTypeId;
        case Role::TreeItem: return UIA_TreeItemControlTypeId;
        case Role::Tab: return UIA_TabControlTypeId;
        case Role::TabItem: return UIA_TabItemControlTypeId;
        case Role::Menu: return UIA_MenuControlTypeId;
        case Role::MenuItem: return UIA_MenuItemControlTypeId;
        case Role::ToolBar: return UIA_ToolBarControlTypeId;
        case Role::StatusBar: return UIA_StatusBarControlTypeId;
        case Role::ProgressBar: return UIA_ProgressBarControlTypeId;
        case Role::ScrollBar: return UIA_ScrollBarControlTypeId;
        case Role::Image: return UIA_ImageControlTypeId;
        case Role::Custom: return UIA_CustomControlTypeId;
        default: return UIA_PaneControlTypeId;
    }
}

std::vector<Widget*> visibleChildren(Widget* w) {
    std::vector<Widget*> v;
    for (auto& c : w->children())
        if (c->isVisibleSelf()) v.push_back(c.get());
    return v;
}

// -- the Text pattern's range over a LineEdit ---------------------------------------------------------------------

class TextRange final : public ITextRangeProvider {
public:
    TextRange(UiaElement* owner, LineEdit* edit, std::size_t a, std::size_t b) : owner_(owner), edit_(edit), a_(a), b_(b) {
        owner_->AddRef();
    }
    STDMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (riid == IID_IUnknown || riid == __uuidof(ITextRangeProvider)) {
            *out = static_cast<ITextRangeProvider*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override {
        const LONG r = InterlockedDecrement(&refs_);
        if (!r) {
            owner_->Release();
            delete this;
        }
        return static_cast<ULONG>(r);
    }
    bool alive() const { return owner_->widget() != nullptr; }
    EditModel& model() const { return edit_->model(); }
    std::size_t len() const { return model().text().size(); }
    void clamp() {
        a_ = std::min(a_, len());
        b_ = std::clamp(b_, a_, len());
    }
    std::pair<std::size_t, std::size_t> unitAt(TextUnit u, std::size_t p) const {
        if (u == TextUnit_Character) return {model().snap(p), p >= len() ? len() : model().nextStop(model().snap(p))};
        if (u == TextUnit_Word) return model().wordAt(p);
        return {0, len()};  // Format, Line, Paragraph, Page, Document: one line of text
    }
    std::size_t step(TextUnit u, std::size_t p, bool forward) const {  // the next / previous unit start
        if (u == TextUnit_Character) return forward ? model().nextStop(p) : model().prevStop(p);
        if (u == TextUnit_Word) {
            if (forward) {
                std::size_t e = model().wordAt(p).second;
                return e == p ? model().nextStop(p) : e;
            }
            if (p == 0) return 0;
            return model().wordAt(model().prevStop(p)).first;
        }
        return forward ? len() : 0;
    }
    int moveEndpoint(std::size_t& e, TextUnit u, int count) const {
        int moved = 0;
        while (count > 0 && e < len()) e = step(u, e, true), --count, ++moved;
        while (count < 0 && e > 0) e = step(u, e, false), ++count, --moved;
        return moved;
    }

    STDMETHODIMP Clone(ITextRangeProvider** out) override {
        *out = new TextRange(owner_, edit_, a_, b_);
        return S_OK;
    }
    STDMETHODIMP Compare(ITextRangeProvider* o, BOOL* same) override {
        auto* t = static_cast<TextRange*>(o);
        *same = t && t->edit_ == edit_ && t->a_ == a_ && t->b_ == b_;
        return S_OK;
    }
    STDMETHODIMP CompareEndpoints(TextPatternRangeEndpoint ep, ITextRangeProvider* o, TextPatternRangeEndpoint oep, int* cmp) override {
        auto* t = static_cast<TextRange*>(o);
        if (!t) return E_INVALIDARG;
        const std::size_t x = ep == TextPatternRangeEndpoint_Start ? a_ : b_;
        const std::size_t y = oep == TextPatternRangeEndpoint_Start ? t->a_ : t->b_;
        *cmp = x < y ? -1 : x > y ? 1 : 0;
        return S_OK;
    }
    STDMETHODIMP ExpandToEnclosingUnit(TextUnit u) override {
        if (!alive()) return UIA_E_ELEMENTNOTAVAILABLE;
        clamp();
        std::tie(a_, b_) = unitAt(u, a_);
        return S_OK;
    }
    STDMETHODIMP FindAttribute(TEXTATTRIBUTEID, VARIANT, BOOL, ITextRangeProvider** out) override {
        *out = nullptr;  // no text attributes in this app
        return S_OK;
    }
    STDMETHODIMP FindText(BSTR text, BOOL backward, BOOL ignore_case, ITextRangeProvider** out) override {
        *out = nullptr;
        if (!alive()) return UIA_E_ELEMENTNOTAVAILABLE;
        clamp();
        std::string hay = model().text().substr(a_, b_ - a_), needle = toUtf8(text);
        if (needle.empty()) return S_OK;
        if (ignore_case) {
            auto low = [](std::string s) {
                for (char& c : s) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
                return s;
            };
            hay = low(hay);
            needle = low(needle);
        }
        const std::size_t at = backward ? hay.rfind(needle) : hay.find(needle);
        if (at != std::string::npos) *out = new TextRange(owner_, edit_, a_ + at, a_ + at + needle.size());
        return S_OK;
    }
    STDMETHODIMP GetAttributeValue(TEXTATTRIBUTEID, VARIANT* v) override {
        v->vt = VT_UNKNOWN;
        return UiaGetReservedNotSupportedValue(&v->punkVal);
    }
    STDMETHODIMP GetBoundingRectangles(SAFEARRAY** out) override {
        if (!alive()) return UIA_E_ELEMENTNOTAVAILABLE;
        clamp();
        const RECT r = edit_->rangeScreenRect(a_, b_);
        *out = SafeArrayCreateVector(VT_R8, 0, 4);
        double v[4] = {static_cast<double>(r.left), static_cast<double>(r.top), static_cast<double>(r.right - r.left),
                       static_cast<double>(r.bottom - r.top)};
        for (LONG i = 0; i < 4; ++i) SafeArrayPutElement(*out, &i, &v[i]);
        return S_OK;
    }
    STDMETHODIMP GetEnclosingElement(IRawElementProviderSimple** out) override {
        *out = static_cast<IRawElementProviderSimple*>(owner_);
        owner_->AddRef();
        return S_OK;
    }
    STDMETHODIMP GetText(int max_len, BSTR* out) override {
        if (!alive()) return UIA_E_ELEMENTNOTAVAILABLE;
        clamp();
        std::wstring w = toUtf16(model().text().substr(a_, b_ - a_));
        if (max_len >= 0 && w.size() > static_cast<std::size_t>(max_len)) w.resize(static_cast<std::size_t>(max_len));
        *out = SysAllocStringLen(w.data(), static_cast<UINT>(w.size()));
        return S_OK;
    }
    STDMETHODIMP Move(TextUnit u, int count, int* moved) override {
        if (!alive()) return UIA_E_ELEMENTNOTAVAILABLE;
        clamp();
        const bool degenerate = a_ == b_;
        std::size_t start = a_;
        *moved = moveEndpoint(start, u, count);
        a_ = b_ = start;
        if (!degenerate) std::tie(a_, b_) = unitAt(u, a_);
        return S_OK;
    }
    STDMETHODIMP MoveEndpointByUnit(TextPatternRangeEndpoint ep, TextUnit u, int count, int* moved) override {
        if (!alive()) return UIA_E_ELEMENTNOTAVAILABLE;
        clamp();
        std::size_t& e = ep == TextPatternRangeEndpoint_Start ? a_ : b_;
        *moved = moveEndpoint(e, u, count);
        if (a_ > b_) (ep == TextPatternRangeEndpoint_Start ? b_ : a_) = e;  // the other end follows past it
        return S_OK;
    }
    STDMETHODIMP MoveEndpointByRange(TextPatternRangeEndpoint ep, ITextRangeProvider* o, TextPatternRangeEndpoint oep) override {
        auto* t = static_cast<TextRange*>(o);
        if (!t) return E_INVALIDARG;
        const std::size_t v = oep == TextPatternRangeEndpoint_Start ? t->a_ : t->b_;
        (ep == TextPatternRangeEndpoint_Start ? a_ : b_) = v;
        if (a_ > b_) (ep == TextPatternRangeEndpoint_Start ? b_ : a_) = v;
        return S_OK;
    }
    STDMETHODIMP Select() override {
        if (!alive()) return UIA_E_ELEMENTNOTAVAILABLE;
        clamp();
        model().setSelection(a_, b_);
        edit_->update();
        return S_OK;
    }
    STDMETHODIMP AddToSelection() override { return UIA_E_INVALIDOPERATION; }       // one selection only
    STDMETHODIMP RemoveFromSelection() override { return UIA_E_INVALIDOPERATION; }
    STDMETHODIMP ScrollIntoView(BOOL) override { return S_OK; }
    STDMETHODIMP GetChildren(SAFEARRAY** out) override {
        *out = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);  // no embedded objects
        return S_OK;
    }

private:
    LONG refs_ = 1;
    UiaElement* owner_;
    LineEdit* edit_;
    std::size_t a_, b_;
};

}  // namespace

// -- the host -----------------------------------------------------------------------------------------------------

UiaHost::UiaHost(HWND hwnd, Widget& root, InputRouter& router) : hwnd_(hwnd), root_(root), router_(router) {}

UiaHost::~UiaHost() {
    for (auto& [w, e] : elements_) {
        UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(e));
        e->detach();
        e->Release();
    }
}

bool UiaHost::onGetObject(WPARAM wp, LPARAM lp, LRESULT* result) {
    if (static_cast<long>(lp) != static_cast<long>(UiaRootObjectId)) return false;
    *result = UiaReturnRawElementProvider(hwnd_, wp, lp, static_cast<IRawElementProviderSimple*>(element(&root_)));
    return true;
}

UiaElement* UiaHost::element(Widget* w) {
    if (!w) return nullptr;
    if (auto it = elements_.find(w); it != elements_.end()) return it->second;
    auto* e = new UiaElement(this, w, next_id_++);
    elements_.emplace(w, e);
    if (auto* le = dynamic_cast<LineEdit*>(w)) le->on_accessible_value_changed = [this, le] { valueChanged(le); };
    return e;
}

void UiaHost::widgetGone(Widget* w) {
    std::vector<Widget*> sub;
    std::function<void(Widget*)> walk = [&](Widget* x) {
        sub.push_back(x);
        for (auto& c : x->children()) walk(c.get());
    };
    walk(w);
    for (Widget* x : sub) {
        auto it = elements_.find(x);
        if (it == elements_.end()) continue;
        UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(it->second));
        it->second->detach();  // a client still holding it gets UIA_E_ELEMENTNOTAVAILABLE
        it->second->Release();
        elements_.erase(it);
    }
}

void UiaHost::focusChanged(Widget* w) {
    ++events_;
    if (w && UiaClientsAreListening())
        UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(element(w)), UIA_AutomationFocusChangedEventId);
}

void UiaHost::valueChanged(Widget* w) {
    ++events_;
    if (!w || !UiaClientsAreListening()) return;
    auto* e = static_cast<IRawElementProviderSimple*>(element(w));
    VARIANT old_v, new_v;
    VariantInit(&old_v);
    VariantInit(&new_v);
    new_v.vt = VT_BSTR;
    new_v.bstrVal = bstr(w->accessibleValue());
    UiaRaiseAutomationPropertyChangedEvent(e, UIA_ValueValuePropertyId, old_v, new_v);
    UiaRaiseAutomationEvent(e, UIA_Text_TextChangedEventId);
    VariantClear(&new_v);
}

// -- the element --------------------------------------------------------------------------------------------------

ULONG UiaElement::Release() {
    const LONG r = InterlockedDecrement(&refs_);
    if (!r) delete this;
    return static_cast<ULONG>(r);
}

STDMETHODIMP UiaElement::QueryInterface(REFIID riid, void** out) {
    *out = nullptr;
    if (riid == IID_IUnknown || riid == __uuidof(IRawElementProviderSimple)) *out = static_cast<IRawElementProviderSimple*>(this);
    else if (riid == __uuidof(IRawElementProviderFragment)) *out = static_cast<IRawElementProviderFragment*>(this);
    else if (riid == __uuidof(IRawElementProviderFragmentRoot) && w_ && host_ && w_ == &host_->root())
        *out = static_cast<IRawElementProviderFragmentRoot*>(this);
    else if (riid == __uuidof(IInvokeProvider)) *out = static_cast<IInvokeProvider*>(this);
    else if (riid == __uuidof(IToggleProvider)) *out = static_cast<IToggleProvider*>(this);
    else if (riid == __uuidof(IValueProvider)) *out = static_cast<IValueProvider*>(this);
    else if (riid == __uuidof(ISelectionItemProvider)) *out = static_cast<ISelectionItemProvider*>(this);
    else if (riid == __uuidof(ITextProvider)) *out = static_cast<ITextProvider*>(this);
    else return E_NOINTERFACE;
    AddRef();
    return S_OK;
}

LineEdit* UiaElement::edit() const { return dynamic_cast<LineEdit*>(w_); }

IRawElementProviderFragment* UiaElement::wrap(Widget* w) {
    if (!w || !host_) return nullptr;
    auto* e = static_cast<IRawElementProviderFragment*>(host_->element(w));
    e->AddRef();
    return e;
}

STDMETHODIMP UiaElement::get_ProviderOptions(ProviderOptions* o) {
    *o = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
    return S_OK;
}

STDMETHODIMP UiaElement::GetPatternProvider(PATTERNID id, IUnknown** out) {
    *out = nullptr;
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    const Role r = w_->accessibleRole();
    bool has = false;
    if (id == UIA_InvokePatternId) has = r == Role::Button || r == Role::MenuItem;
    else if (id == UIA_TogglePatternId) has = w_->accessibleToggleState() >= 0;
    else if (id == UIA_ValuePatternId) has = w_->accessibleHasValue();
    else if (id == UIA_SelectionItemPatternId) has = w_->accessibleSelectionState() >= 0;
    else if (id == UIA_TextPatternId) has = edit() != nullptr;
    if (has) {
        *out = static_cast<IRawElementProviderSimple*>(this);
        AddRef();
    }
    return S_OK;
}

STDMETHODIMP UiaElement::GetPropertyValue(PROPERTYID id, VARIANT* v) {
    VariantInit(v);
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    auto boolean = [&](bool b) {
        v->vt = VT_BOOL;
        v->boolVal = b ? VARIANT_TRUE : VARIANT_FALSE;
    };
    switch (id) {
        case UIA_ControlTypePropertyId: v->vt = VT_I4; v->lVal = controlType(w_->accessibleRole()); break;
        case UIA_NamePropertyId: if (!w_->accessibleName.empty()) v->vt = VT_BSTR, v->bstrVal = bstr(w_->accessibleName); break;
        case UIA_AutomationIdPropertyId: if (!w_->name.empty()) v->vt = VT_BSTR, v->bstrVal = bstr(w_->name); break;
        case UIA_HelpTextPropertyId: if (!w_->toolTip.empty()) v->vt = VT_BSTR, v->bstrVal = bstr(w_->toolTip); break;
        case UIA_ClassNamePropertyId: v->vt = VT_BSTR; v->bstrVal = SysAllocString(L"TcadWidget"); break;
        case UIA_FrameworkIdPropertyId: v->vt = VT_BSTR; v->bstrVal = SysAllocString(L"TCAD"); break;
        case UIA_IsEnabledPropertyId: boolean(w_->isEnabled()); break;
        case UIA_IsKeyboardFocusablePropertyId: boolean(w_->acceptsFocus(FocusReason::Other)); break;
        case UIA_HasKeyboardFocusPropertyId: boolean(w_->hasFocus()); break;
        case UIA_IsOffscreenPropertyId: boolean(!w_->isVisible()); break;
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId: boolean(true); break;
        default: break;
    }
    return S_OK;
}

STDMETHODIMP UiaElement::get_HostRawElementProvider(IRawElementProviderSimple** out) {
    *out = nullptr;
    if (w_ && host_ && w_ == &host_->root()) return UiaHostProviderFromHwnd(host_->hwnd(), out);
    return S_OK;
}

STDMETHODIMP UiaElement::Navigate(NavigateDirection d, IRawElementProviderFragment** out) {
    *out = nullptr;
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    Widget* target = nullptr;
    if (d == NavigateDirection_Parent) {
        if (w_ != &host_->root()) target = w_->parent();  // the root's parent is the window's own element
    } else if (d == NavigateDirection_FirstChild || d == NavigateDirection_LastChild) {
        const auto kids = visibleChildren(w_);
        if (!kids.empty()) target = d == NavigateDirection_FirstChild ? kids.front() : kids.back();
    } else if (w_ != &host_->root() && w_->parent()) {
        const auto sibs = visibleChildren(w_->parent());
        auto it = std::find(sibs.begin(), sibs.end(), w_);
        if (it != sibs.end()) {
            if (d == NavigateDirection_NextSibling && it + 1 != sibs.end()) target = *(it + 1);
            if (d == NavigateDirection_PreviousSibling && it != sibs.begin()) target = *(it - 1);
        }
    }
    *out = wrap(target);
    return S_OK;
}

STDMETHODIMP UiaElement::GetRuntimeId(SAFEARRAY** out) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    int ids[] = {UiaAppendRuntimeId, id_};
    *out = SafeArrayCreateVector(VT_I4, 0, 2);
    for (LONG i = 0; i < 2; ++i) SafeArrayPutElement(*out, &i, &ids[i]);
    return S_OK;
}

STDMETHODIMP UiaElement::get_BoundingRectangle(UiaRect* r) {
    *r = {};
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!w_->isVisible()) return S_OK;
    const RectI g = w_->windowRect();
    POINT tl{g.x, g.y};
    ClientToScreen(host_->hwnd(), &tl);
    *r = {static_cast<double>(tl.x), static_cast<double>(tl.y), static_cast<double>(g.width), static_cast<double>(g.height)};
    return S_OK;
}

STDMETHODIMP UiaElement::GetEmbeddedFragmentRoots(SAFEARRAY** out) {
    *out = nullptr;
    return S_OK;
}

STDMETHODIMP UiaElement::SetFocus() {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    w_->setFocus(FocusReason::Other);
    return S_OK;
}

STDMETHODIMP UiaElement::get_FragmentRoot(IRawElementProviderFragmentRoot** out) {
    *out = nullptr;
    if (!host_) return UIA_E_ELEMENTNOTAVAILABLE;
    auto* root = host_->element(&host_->root());
    *out = static_cast<IRawElementProviderFragmentRoot*>(root);
    root->AddRef();
    return S_OK;
}

STDMETHODIMP UiaElement::ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) {
    *out = nullptr;
    if (!host_) return UIA_E_ELEMENTNOTAVAILABLE;
    POINT p{static_cast<LONG>(x), static_cast<LONG>(y)};
    ScreenToClient(host_->hwnd(), &p);
    const double s = host_->root().scale();
    *out = wrap(host_->router().widgetAt({static_cast<float>(p.x / s), static_cast<float>(p.y / s)}));
    return S_OK;
}

STDMETHODIMP UiaElement::GetFocus(IRawElementProviderFragment** out) {
    *out = nullptr;
    if (!host_) return UIA_E_ELEMENTNOTAVAILABLE;
    Widget* f = host_->router().focusWidget();
    if (f && f != &host_->root()) *out = wrap(f);
    return S_OK;
}

STDMETHODIMP UiaElement::Invoke() {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!w_->isEnabled()) return UIA_E_ELEMENTNOTENABLED;
    w_->accessibleInvoke();
    if (UiaClientsAreListening()) UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(this), UIA_Invoke_InvokedEventId);
    return S_OK;
}

STDMETHODIMP UiaElement::Toggle() {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!w_->isEnabled()) return UIA_E_ELEMENTNOTENABLED;
    w_->accessibleToggle();
    return S_OK;
}

STDMETHODIMP UiaElement::get_ToggleState(ToggleState* s) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    const int t = w_->accessibleToggleState();
    *s = t == 1 ? ToggleState_On : t == 2 ? ToggleState_Indeterminate : ToggleState_Off;
    return S_OK;
}

STDMETHODIMP UiaElement::SetValue(LPCWSTR v) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!w_->isEnabled()) return UIA_E_ELEMENTNOTENABLED;
    return w_->accessibleSetValue(toUtf8(v)) ? S_OK : UIA_E_INVALIDOPERATION;
}

STDMETHODIMP UiaElement::get_Value(BSTR* v) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    *v = bstr(w_->accessibleValue());
    return S_OK;
}

STDMETHODIMP UiaElement::get_IsReadOnly(BOOL* r) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    *r = w_->accessibleReadOnly();
    return S_OK;
}

STDMETHODIMP UiaElement::Select() {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!w_->isEnabled()) return UIA_E_ELEMENTNOTENABLED;
    w_->accessibleSelect();
    if (UiaClientsAreListening())
        UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(this), UIA_SelectionItem_ElementSelectedEventId);
    return S_OK;
}

STDMETHODIMP UiaElement::AddToSelection() {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    return w_->accessibleSelectionState() == 1 ? S_OK : UIA_E_INVALIDOPERATION;  // single selection: only a no-op
}

STDMETHODIMP UiaElement::RemoveFromSelection() {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    return w_->accessibleSelectionState() == 1 ? UIA_E_INVALIDOPERATION : S_OK;  // a checked radio stays checked
}

STDMETHODIMP UiaElement::get_IsSelected(BOOL* r) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    *r = w_->accessibleSelectionState() == 1;
    return S_OK;
}

STDMETHODIMP UiaElement::get_SelectionContainer(IRawElementProviderSimple** out) {
    *out = nullptr;  // radio groups have no container element (no Selection pattern on a parent yet)
    return w_ ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
}

STDMETHODIMP UiaElement::GetSelection(SAFEARRAY** out) {
    LineEdit* e = edit();
    if (!e) return UIA_E_ELEMENTNOTAVAILABLE;
    const auto [a, b] = e->model().selection();
    ITextRangeProvider* r = new TextRange(this, e, a, b);
    *out = SafeArrayCreateVector(VT_UNKNOWN, 0, 1);
    LONG i = 0;
    SafeArrayPutElement(*out, &i, r);  // AddRefs
    r->Release();
    return S_OK;
}

STDMETHODIMP UiaElement::GetVisibleRanges(SAFEARRAY** out) {
    LineEdit* e = edit();
    if (!e) return UIA_E_ELEMENTNOTAVAILABLE;
    ITextRangeProvider* r = new TextRange(this, e, 0, e->text().size());
    *out = SafeArrayCreateVector(VT_UNKNOWN, 0, 1);
    LONG i = 0;
    SafeArrayPutElement(*out, &i, r);
    r->Release();
    return S_OK;
}

STDMETHODIMP UiaElement::RangeFromChild(IRawElementProviderSimple*, ITextRangeProvider** out) {
    *out = nullptr;
    return E_INVALIDARG;  // an edit has no child elements
}

STDMETHODIMP UiaElement::RangeFromPoint(UiaPoint p, ITextRangeProvider** out) {
    LineEdit* e = edit();
    if (!e) return UIA_E_ELEMENTNOTAVAILABLE;
    const std::size_t at = e->offsetAtScreen({static_cast<LONG>(p.x), static_cast<LONG>(p.y)});
    *out = new TextRange(this, e, at, at);
    return S_OK;
}

STDMETHODIMP UiaElement::get_DocumentRange(ITextRangeProvider** out) {
    LineEdit* e = edit();
    if (!e) return UIA_E_ELEMENTNOTAVAILABLE;
    *out = new TextRange(this, e, 0, e->text().size());
    return S_OK;
}

STDMETHODIMP UiaElement::get_SupportedTextSelection(SupportedTextSelection* s) {
    *s = SupportedTextSelection_Single;
    return S_OK;
}

}  // namespace tcad::ui
