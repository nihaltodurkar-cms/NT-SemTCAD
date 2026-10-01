#include "ui/win32/uia_provider.hpp"

#include "ui/win32/dwrite_text.hpp"
#include "ui/win32/text_input.hpp"

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
        case Role::Spinner: return UIA_SpinnerControlTypeId;
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
        case Role::Table: return UIA_TableControlTypeId;
        case Role::Header: return UIA_HeaderControlTypeId;
        case Role::HeaderItem: return UIA_HeaderItemControlTypeId;
        case Role::DataItem: return UIA_DataItemControlTypeId;
        case Role::Splitter: return UIA_ThumbControlTypeId;
        case Role::Separator: return UIA_SeparatorControlTypeId;
        case Role::Window: return UIA_WindowControlTypeId;
        case Role::Dialog: return UIA_WindowControlTypeId;
        case Role::MenuBar: return UIA_MenuBarControlTypeId;
        default: return UIA_PaneControlTypeId;
    }
}

// The visible children as UI Automation sees them: a structural widget (a view's clipping viewport) is skipped and its
// children are the parent's (N3e).
void collectChildren(Widget* w, std::vector<Widget*>& out) {
    for (auto& c : w->children()) {
        if (!c->isVisibleSelf()) continue;
        if (c->accessibleIsStructural()) collectChildren(c.get(), out);
        else out.push_back(c.get());
    }
}

std::vector<Widget*> visibleChildren(Widget* w) {
    std::vector<Widget*> v;
    collectChildren(w, v);
    return v;
}

Widget* accessibleParent(Widget* w) {
    Widget* p = w->parent();
    while (p && p->accessibleIsStructural()) p = p->parent();
    return p;
}

// -- the Text pattern's range over a TextInput (LineEdit, PlainTextEdit) ---------------------------------------------------------------------

class TextRange final : public ITextRangeProvider {
public:
    TextRange(UiaElement* owner, TextInput* edit, std::size_t a, std::size_t b) : owner_(owner), edit_(edit), a_(a), b_(b) {
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
        if (u == TextUnit_Line || u == TextUnit_Paragraph) return edit_->lineRangeAt(p);  // hard lines (see TextInput)
        return {0, len()};  // Format, Page, Document
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
        if (u == TextUnit_Line || u == TextUnit_Paragraph) {
            const std::size_t at = std::min(p, len());
            if (forward) {
                const std::size_t e = edit_->lineRangeAt(at).second;
                return e < len() ? e + 1 : p;  // past the line feed; on the last line there is nowhere to go
            }
            const std::size_t s = edit_->lineRangeAt(at).first;
            return s == 0 ? 0 : edit_->lineRangeAt(s - 1).first;
        }
        return forward ? len() : 0;
    }
    int moveEndpoint(std::size_t& e, TextUnit u, int count) const {
        int moved = 0;
        while (count > 0 && e < len()) {
            const std::size_t n = step(u, e, true);
            if (n == e) break;  // on the last unit: nothing further to move to
            e = n, --count, ++moved;
        }
        while (count < 0 && e > 0) {
            const std::size_t n = step(u, e, false);
            if (n == e) break;
            e = n, ++count, --moved;
        }
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
        const std::vector<RECT> rects = edit_->rangeScreenRects(a_, b_);  // one per line (and per direction run)
        *out = SafeArrayCreateVector(VT_R8, 0, static_cast<ULONG>(4 * rects.size()));
        LONG i = 0;
        for (const RECT& r : rects) {
            const double v[4] = {static_cast<double>(r.left), static_cast<double>(r.top), static_cast<double>(r.right - r.left),
                                 static_cast<double>(r.bottom - r.top)};
            for (double d : v) SafeArrayPutElement(*out, &i, const_cast<double*>(&d)), ++i;
        }
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
    TextInput* edit_;
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
    if (!w || dying_.count(w)) return nullptr;  // (a widget on its way out gets no new element)
    if (auto it = elements_.find(w); it != elements_.end()) return it->second;
    auto* e = new UiaElement(this, w, next_id_++);
    elements_.emplace(w, e);
    if (auto* le = dynamic_cast<TextInput*>(w)) le->on_accessible_value_changed = [this, le] { valueChanged(le); };
    if (w->accessibleRange().valid || (w->accessibleHasValue() && !dynamic_cast<TextInput*>(w)))
        w->accessible_range_changed = [this, w] { rangeChanged(w); };
    if (w->accessibleExpandState() >= 0) w->accessible_expand_changed = [this, w] { expandChanged(w); };
    w->accessible_current_changed = [this, w] { currentChanged(w); };
    w->accessible_structure_changed = [this, w] { structureChanged(w); };
    return e;
}

void UiaHost::widgetGone(Widget* w) {
    std::vector<Widget*> sub;
    std::function<void(Widget*)> walk = [&](Widget* x) {
        sub.push_back(x);
        for (auto& c : x->children()) walk(c.get());
    };
    walk(w);
    // Two phases. UiaDisconnectProvider calls back into the providers (Navigate and the like) while it runs; those calls
    // must find the dying widgets' elements already detached, and must not make NEW elements for widgets that are about
    // to be freed (found with AddressSanitizer: an element made for a released editor during the disconnect outlived it).
    for (Widget* x : sub) dying_.insert(x);
    std::vector<UiaElement*> gone;
    for (Widget* x : sub) {
        auto it = elements_.find(x);
        if (it == elements_.end()) continue;
        it->second->detach();  // a client still holding it gets UIA_E_ELEMENTNOTAVAILABLE
        gone.push_back(it->second);
        elements_.erase(it);
    }
    for (UiaElement* e : gone) {
        UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(e));
        e->Release();
    }
    for (Widget* x : sub) dying_.erase(x);
}

void UiaHost::currentChanged(Widget* container) {
    if (container && container->hasFocus()) focusChanged(container);  // the focus event names the current item
}

void UiaHost::structureChanged(Widget* w) {
    ++events_;
    ++structure_events_;
    if (w && UiaClientsAreListening())
        UiaRaiseStructureChangedEvent(static_cast<IRawElementProviderSimple*>(element(w)), StructureChangeType_ChildrenInvalidated, nullptr, 0);
}

void UiaHost::focusChanged(Widget* w) {
    ++events_;
    if (w) {
        if (Widget* child = w->accessibleFocusChild()) w = child;  // a list's focus is on its current item
    }
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

void UiaHost::rangeChanged(Widget* w) {
    ++events_;
    if (!w || !UiaClientsAreListening()) return;
    auto* e = static_cast<IRawElementProviderSimple*>(element(w));
    if (w->accessibleRange().valid) {
        VARIANT old_v, new_v;
        VariantInit(&old_v);
        VariantInit(&new_v);
        new_v.vt = VT_R8;
        new_v.dblVal = w->accessibleRange().value;
        UiaRaiseAutomationPropertyChangedEvent(e, UIA_RangeValueValuePropertyId, old_v, new_v);
    }
    if (w->accessibleHasValue()) valueChanged(w);
}

void UiaHost::expandChanged(Widget* w) {
    ++events_;
    if (!w || !UiaClientsAreListening()) return;
    auto* e = static_cast<IRawElementProviderSimple*>(element(w));
    VARIANT old_v, new_v;
    VariantInit(&old_v);
    VariantInit(&new_v);
    new_v.vt = VT_I4;
    new_v.lVal = w->accessibleExpandState() == 1 ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed;
    old_v.vt = VT_I4;
    old_v.lVal = w->accessibleExpandState() == 1 ? ExpandCollapseState_Collapsed : ExpandCollapseState_Expanded;
    UiaRaiseAutomationPropertyChangedEvent(e, UIA_ExpandCollapseExpandCollapseStatePropertyId, old_v, new_v);
}

void UiaHost::announce(Widget* w, std::string_view text) {
    ++events_;
    ++announcements_;
    last_announcement_.assign(text);
    if (!w || !UiaClientsAreListening()) return;
    auto* e = static_cast<IRawElementProviderSimple*>(element(w));
    BSTR t = bstr(std::string(text));
    BSTR id = SysAllocString(L"tcad.highlight");
    UiaRaiseNotificationEvent(e, NotificationKind_Other, NotificationProcessing_MostRecent, t, id);
    SysFreeString(t);
    SysFreeString(id);
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
    else if (riid == __uuidof(IRangeValueProvider)) *out = static_cast<IRangeValueProvider*>(this);
    else if (riid == __uuidof(IExpandCollapseProvider)) *out = static_cast<IExpandCollapseProvider*>(this);
    else if (riid == __uuidof(ISelectionItemProvider)) *out = static_cast<ISelectionItemProvider*>(this);
    else if (riid == __uuidof(ITextProvider)) *out = static_cast<ITextProvider*>(this);
    else if (riid == __uuidof(ISelectionProvider)) *out = static_cast<ISelectionProvider*>(this);
    else if (riid == __uuidof(IScrollProvider)) *out = static_cast<IScrollProvider*>(this);
    else if (riid == __uuidof(IScrollItemProvider)) *out = static_cast<IScrollItemProvider*>(this);
    else if (riid == __uuidof(IGridProvider)) *out = static_cast<IGridProvider*>(this);
    else if (riid == __uuidof(IGridItemProvider)) *out = static_cast<IGridItemProvider*>(this);
    else if (riid == __uuidof(ITableProvider)) *out = static_cast<ITableProvider*>(this);
    else if (riid == __uuidof(ITableItemProvider)) *out = static_cast<ITableItemProvider*>(this);
    else return E_NOINTERFACE;
    AddRef();
    return S_OK;
}

TextInput* UiaElement::edit() const { return dynamic_cast<TextInput*>(w_); }

IRawElementProviderFragment* UiaElement::wrap(Widget* w) {
    if (!w || !host_) return nullptr;
    auto* e = static_cast<IRawElementProviderFragment*>(host_->element(w));
    if (!e) return nullptr;
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
    else if (id == UIA_RangeValuePatternId) has = w_->accessibleRange().valid;
    else if (id == UIA_ExpandCollapsePatternId) has = w_->accessibleExpandState() >= 0;
    else if (id == UIA_TextPatternId) has = edit() != nullptr;
    else if (id == UIA_SelectionPatternId) has = w_->accessibleIsSelectionContainer();
    else if (id == UIA_ScrollPatternId) has = w_->accessibleScroll().valid;
    else if (id == UIA_ScrollItemPatternId) has = w_->accessibleSelectionContainer() != nullptr || w_->accessibleCell().valid;
    else if (id == UIA_GridPatternId) has = w_->accessibleIsGrid();
    else if (id == UIA_TablePatternId) has = w_->accessibleIsGrid();
    else if (id == UIA_GridItemPatternId) has = w_->accessibleCell().valid;
    else if (id == UIA_TableItemPatternId) has = w_->accessibleCell().valid && w_->accessibleCell().grid && !w_->accessibleCell().grid->accessibleColumnHeaders().empty();
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
        case UIA_NamePropertyId: {
            // A spin box's inner edit is named as the spin box is (a form's label names the spin box).
            const Widget* named = w_;
            if (named->accessibleName.empty() && named->parent() && named->parent()->accessibleRole() == Role::Spinner) named = named->parent();
            if (!named->accessibleName.empty()) v->vt = VT_BSTR, v->bstrVal = bstr(named->accessibleName);
            break;
        }
        case UIA_AutomationIdPropertyId: if (!w_->name.empty()) v->vt = VT_BSTR, v->bstrVal = bstr(w_->name); break;
        case UIA_HelpTextPropertyId: if (!w_->toolTip.empty()) v->vt = VT_BSTR, v->bstrVal = bstr(w_->toolTip); break;
        case UIA_ClassNamePropertyId: v->vt = VT_BSTR; v->bstrVal = SysAllocString(L"TcadWidget"); break;
        case UIA_FrameworkIdPropertyId: v->vt = VT_BSTR; v->bstrVal = SysAllocString(L"TCAD"); break;
        case UIA_IsEnabledPropertyId: boolean(w_->accessibleEnabled()); break;
        case UIA_IsKeyboardFocusablePropertyId: boolean(w_->accessibleFocusable()); break;
        case UIA_HasKeyboardFocusPropertyId: boolean(w_->accessibleFocused()); break;
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
        if (w_ != &host_->root()) target = accessibleParent(w_);  // the root's parent is the window's own element
    } else if (d == NavigateDirection_FirstChild || d == NavigateDirection_LastChild) {
        const auto kids = visibleChildren(w_);
        if (!kids.empty()) target = d == NavigateDirection_FirstChild ? kids.front() : kids.back();
    } else if (w_ != &host_->root() && accessibleParent(w_)) {
        const auto sibs = visibleChildren(accessibleParent(w_));
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
    if (f && f != &host_->root()) {
        if (Widget* child = f->accessibleFocusChild()) f = child;  // a list's focus is its current item
        *out = wrap(f);
    }
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
    const AccessibleRange a = w_->accessibleRange();
    *r = a.valid ? a.read_only : w_->accessibleReadOnly();
    return S_OK;
}

STDMETHODIMP UiaElement::Expand() {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!w_->isEnabled()) return UIA_E_ELEMENTNOTENABLED;
    w_->accessibleExpand(true);
    return S_OK;
}

STDMETHODIMP UiaElement::Collapse() {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!w_->isEnabled()) return UIA_E_ELEMENTNOTENABLED;
    w_->accessibleExpand(false);
    return S_OK;
}

STDMETHODIMP UiaElement::get_ExpandCollapseState(ExpandCollapseState* s) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    *s = w_->accessibleExpandState() == 1 ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed;
    return S_OK;
}

STDMETHODIMP UiaElement::SetValue(double v) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!w_->isEnabled()) return UIA_E_ELEMENTNOTENABLED;
    return w_->accessibleSetRangeValue(v) ? S_OK : E_INVALIDARG;  // out of range, or not a whole number
}

STDMETHODIMP UiaElement::get_Value(double* v) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    *v = w_->accessibleRange().value;
    return S_OK;
}

STDMETHODIMP UiaElement::get_Minimum(double* v) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    *v = w_->accessibleRange().minimum;
    return S_OK;
}

STDMETHODIMP UiaElement::get_Maximum(double* v) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    *v = w_->accessibleRange().maximum;
    return S_OK;
}

STDMETHODIMP UiaElement::get_LargeChange(double* v) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    *v = w_->accessibleRange().large_step;
    return S_OK;
}

STDMETHODIMP UiaElement::get_SmallChange(double* v) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    *v = w_->accessibleRange().small_step;
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
    if (w_->accessibleSelectionContainer()) return w_->accessibleAddToSelection() ? S_OK : UIA_E_INVALIDOPERATION;  // a view's own rules
    return w_->accessibleSelectionState() == 1 ? S_OK : UIA_E_INVALIDOPERATION;  // single selection: only a no-op
}

STDMETHODIMP UiaElement::RemoveFromSelection() {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    if (w_->accessibleSelectionContainer()) return w_->accessibleRemoveFromSelection() ? S_OK : UIA_E_INVALIDOPERATION;
    return w_->accessibleSelectionState() == 1 ? UIA_E_INVALIDOPERATION : S_OK;  // a checked radio stays checked
}

STDMETHODIMP UiaElement::get_IsSelected(BOOL* r) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    *r = w_->accessibleSelectionState() == 1;
    return S_OK;
}

STDMETHODIMP UiaElement::get_SelectionContainer(IRawElementProviderSimple** out) {
    *out = nullptr;  // radio groups have no container element
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    if (Widget* c = w_->accessibleSelectionContainer()) {
        auto* e = host_->element(c);
        if (!e) return S_OK;
        *out = static_cast<IRawElementProviderSimple*>(e);
        e->AddRef();
    }
    return S_OK;
}

HRESULT UiaElement::widgetArray(const std::vector<Widget*>& ws, SAFEARRAY** out) {
    *out = SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(ws.size()));
    LONG i = 0;
    for (Widget* w : ws) {
        IRawElementProviderSimple* p = static_cast<IRawElementProviderSimple*>(host_->element(w));
        if (p) SafeArrayPutElement(*out, &i, p);  // AddRefs
        ++i;
    }
    return S_OK;
}

STDMETHODIMP UiaElement::GetSelection(SAFEARRAY** out) {
    if (w_ && !edit() && w_->accessibleIsSelectionContainer()) return widgetArray(w_->accessibleSelection(), out);
    TextInput* e = edit();
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
    TextInput* e = edit();
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
    TextInput* e = edit();
    if (!e) return UIA_E_ELEMENTNOTAVAILABLE;
    const std::size_t at = e->offsetAtScreen({static_cast<LONG>(p.x), static_cast<LONG>(p.y)});
    *out = new TextRange(this, e, at, at);
    return S_OK;
}

STDMETHODIMP UiaElement::get_DocumentRange(ITextRangeProvider** out) {
    TextInput* e = edit();
    if (!e) return UIA_E_ELEMENTNOTAVAILABLE;
    *out = new TextRange(this, e, 0, e->text().size());
    return S_OK;
}

STDMETHODIMP UiaElement::get_SupportedTextSelection(SupportedTextSelection* s) {
    *s = SupportedTextSelection_Single;
    return S_OK;
}

// -- Selection (a container's), Scroll, Grid and Table (N3e) ---------------------------------------------------------

STDMETHODIMP UiaElement::get_CanSelectMultiple(BOOL* r) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    *r = w_->accessibleCanSelectMultiple();
    return S_OK;
}

STDMETHODIMP UiaElement::get_IsSelectionRequired(BOOL* r) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    *r = w_->accessibleSelectionRequired();
    return S_OK;
}

STDMETHODIMP UiaElement::Scroll(ScrollAmount h, ScrollAmount v) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    const AccessibleScroll s = w_->accessibleScroll();
    auto amount = [](ScrollAmount a) {
        switch (a) {
            case ScrollAmount_LargeDecrement: return -2;
            case ScrollAmount_SmallDecrement: return -1;
            case ScrollAmount_SmallIncrement: return 1;
            case ScrollAmount_LargeIncrement: return 2;
            default: return 0;
        }
    };
    const int ha = amount(h), va = amount(v);
    if ((ha != 0 && !s.horizontal) || (va != 0 && !s.vertical)) return UIA_E_INVALIDOPERATION;  // it cannot scroll that way
    w_->accessibleScrollBy(ha, va);
    return S_OK;
}

STDMETHODIMP UiaElement::SetScrollPercent(double h, double v) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    const AccessibleScroll s = w_->accessibleScroll();
    auto bad = [](double p) { return p != UIA_ScrollPatternNoScroll && (p < 0 || p > 100); };
    if (bad(h) || bad(v)) return E_INVALIDARG;
    if ((h != UIA_ScrollPatternNoScroll && !s.horizontal) || (v != UIA_ScrollPatternNoScroll && !s.vertical)) return UIA_E_INVALIDOPERATION;
    w_->accessibleSetScrollPercent(h == UIA_ScrollPatternNoScroll ? -1 : h, v == UIA_ScrollPatternNoScroll ? -1 : v);
    return S_OK;
}

STDMETHODIMP UiaElement::get_HorizontalScrollPercent(double* r) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    const AccessibleScroll s = w_->accessibleScroll();
    *r = s.horizontal ? s.h_percent : UIA_ScrollPatternNoScroll;
    return S_OK;
}

STDMETHODIMP UiaElement::get_VerticalScrollPercent(double* r) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    const AccessibleScroll s = w_->accessibleScroll();
    *r = s.vertical ? s.v_percent : UIA_ScrollPatternNoScroll;
    return S_OK;
}

STDMETHODIMP UiaElement::get_HorizontalViewSize(double* r) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    *r = w_->accessibleScroll().h_view;
    return S_OK;
}

STDMETHODIMP UiaElement::get_VerticalViewSize(double* r) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    *r = w_->accessibleScroll().v_view;
    return S_OK;
}

STDMETHODIMP UiaElement::get_HorizontallyScrollable(BOOL* r) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    *r = w_->accessibleScroll().horizontal;
    return S_OK;
}

STDMETHODIMP UiaElement::get_VerticallyScrollable(BOOL* r) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    *r = w_->accessibleScroll().vertical;
    return S_OK;
}

STDMETHODIMP UiaElement::ScrollIntoView() {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    w_->accessibleScrollIntoView();
    return S_OK;
}

STDMETHODIMP UiaElement::GetItem(int row, int column, IRawElementProviderSimple** out) {
    *out = nullptr;
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    if (row < 0 || column < 0 || row >= w_->accessibleRowCount() || column >= w_->accessibleColumnCount()) return E_INVALIDARG;
    if (Widget* cell = w_->accessibleGridCell(row, column)) {
        auto* e = host_->element(cell);
        if (!e) return S_OK;
        *out = static_cast<IRawElementProviderSimple*>(e);
        e->AddRef();
    }
    return S_OK;
}

STDMETHODIMP UiaElement::get_RowCount(int* r) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    *r = w_->accessibleRowCount();
    return S_OK;
}

STDMETHODIMP UiaElement::get_ColumnCount(int* r) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    *r = w_->accessibleColumnCount();
    return S_OK;
}

STDMETHODIMP UiaElement::get_Row(int* r) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    *r = w_->accessibleCell().row;
    return S_OK;
}

STDMETHODIMP UiaElement::get_Column(int* r) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    *r = w_->accessibleCell().column;
    return S_OK;
}

STDMETHODIMP UiaElement::get_RowSpan(int* r) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    *r = w_->accessibleCell().row_span;
    return S_OK;
}

STDMETHODIMP UiaElement::get_ColumnSpan(int* r) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    *r = w_->accessibleCell().column_span;
    return S_OK;
}

STDMETHODIMP UiaElement::get_ContainingGrid(IRawElementProviderSimple** out) {
    *out = nullptr;
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    if (Widget* g = w_->accessibleCell().grid) {
        auto* e = host_->element(g);
        if (!e) return S_OK;
        *out = static_cast<IRawElementProviderSimple*>(e);
        e->AddRef();
    }
    return S_OK;
}

STDMETHODIMP UiaElement::GetRowHeaders(SAFEARRAY** out) { return widgetArray({}, out); }  // no row headers: the row numbers are decoration

STDMETHODIMP UiaElement::GetColumnHeaders(SAFEARRAY** out) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    return widgetArray(w_->accessibleColumnHeaders(), out);
}

STDMETHODIMP UiaElement::get_RowOrColumnMajor(RowOrColumnMajor* r) {
    *r = RowOrColumnMajor_RowMajor;
    return S_OK;
}

STDMETHODIMP UiaElement::GetRowHeaderItems(SAFEARRAY** out) { return widgetArray({}, out); }

STDMETHODIMP UiaElement::GetColumnHeaderItems(SAFEARRAY** out) {
    if (!w_) return UIA_E_ELEMENTNOTAVAILABLE;
    const AccessibleCell cell = w_->accessibleCell();
    std::vector<Widget*> one;
    if (cell.valid && cell.grid) {
        const auto headers = cell.grid->accessibleColumnHeaders();
        if (cell.column >= 0 && cell.column < static_cast<int>(headers.size())) one.push_back(headers[static_cast<std::size_t>(cell.column)]);
    }
    return widgetArray(one, out);
}

}  // namespace tcad::ui
