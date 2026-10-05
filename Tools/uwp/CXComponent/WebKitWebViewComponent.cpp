// WebKit.WebKitWebView component (WRL). Thin projection over the existing
// WebKit::WebKitWebViewUWP wrapper; engine untouched. Design §3-§5.
#include "WebKitWinRTBase.h" // hand-declared fundamental async types
#include <activation.h>
#include <eventtoken.h>
#include <inspectable.h>
#include <roapi.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <utility>
#include <windows.applicationmodel.h>
#include <windows.h>
#include <windows.storage.h>
#include <windows.ui.xaml.controls.h>
#include <wrl.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>
// This MIDLRT emits the pre-migration spelling for the system async type.
typedef Windows::Foundation::IAsyncAction __x_Windows_CFoundation_CIAsyncAction;
// Classic C ABI only: the generated header's C++ template section needs a
// primary template the SDK does not provide standalone here.
#define RO_NO_TEMPLATE_NAME
#include "WebKitWebView.h" // MIDLRT output: ABI + C++ names
#undef RO_NO_TEMPLATE_NAME
#include "WebKitWebViewUWP.h" // existing wrapper, reused unchanged

using namespace Microsoft::WRL;
using Microsoft::WRL::Wrappers::HString;
using Microsoft::WRL::Wrappers::HStringReference;
namespace UIXaml = ABI::Windows::UI::Xaml;

namespace WebKitComponent {

// Runtime class names and URI schemes shared by activation and property checks.
constexpr wchar_t kWebKitWebViewClassName[] = L"WebKit.WebKitWebView";
constexpr wchar_t kHttpSchemePrefix[] = L"http://";
constexpr wchar_t kHttpsSchemePrefix[] = L"https://";
constexpr size_t kUriPrefixSearchStart = 0;
constexpr DWORD kUtf8ConversionFlags = 0;
constexpr int kConversionSizeQueryOnly = 0;
constexpr int kNullTerminatedInputLength = -1;
constexpr int kNullTerminatorCharacterCount = 1;
constexpr UINT32 kEmptyStringLength = 0;
constexpr char kZeroInitializedByte = '\0';
constexpr UINT32 kWebKitWebViewClassNameLength = sizeof(kWebKitWebViewClassName) / sizeof(*kWebKitWebViewClassName) - 1;
constexpr UINT64 kInitialComponentNavigationId = 0;
constexpr UINT32 kCompletedAsyncActionId = 1;
constexpr ULONG kWebKitWebViewInterfaceCount = 1;
constexpr ULONG kFactoryInterfaceCount = 1;
constexpr INT32 kNoNavigationErrorCode = 0;

// Plain data holders constructed per event. Mutable Cancel on Starting only.
class WebKitNavigationEventArgs final
    : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, WebKit::IWebKitNavigationEventArgs> {
    InspectableClass(L"WebKit.WebKitNavigationEventArgs", BaseTrust);

public:
    // Captures the navigation payload before it is delivered to event handlers.
    HRESULT RuntimeClassInitialize(const wchar_t* url, UINT64 id, INT32 outcome, INT32 code)
    {
        m_id = id;
        m_outcome = outcome;
        m_code = code;
        return m_url.Set(url);
    }

    // Copies the payload URL into a caller-owned HSTRING.
    IFACEMETHODIMP get_Url(HSTRING* value) override { return m_url.CopyTo(value); }

    // Returns the ID used to correlate this payload with NavigationStarting.
    IFACEMETHODIMP get_NavigationId(UINT64* value) override
    {
        *value = m_id;
        return S_OK;
    }

    // Returns the terminal outcome represented by the payload.
    IFACEMETHODIMP get_Outcome(WebKit::WebKitNavigationOutcome* value) override
    {
        *value = static_cast<WebKit::WebKitNavigationOutcome>(m_outcome);
        return S_OK;
    }

    // Returns the engine failure code, or zero on success.
    IFACEMETHODIMP get_ErrorCode(INT32* value) override
    {
        *value = m_code;
        return S_OK;
    }

private:
    // Retains the finished or failed URL for the event lifetime.
    HString m_url;

    // Correlates the payload with its originating navigation.
    UINT64 m_id { kInitialComponentNavigationId };

    // Stores the generated navigation-outcome enum as its ABI integer.
    INT32 m_outcome { WebKit::WebKitNavigationOutcome_Completed };

    // Engine failure code; zero denotes no failure.
    INT32 m_code { kNoNavigationErrorCode };
};

// Veto event payload: target URI, navigation ID and a host-set cancel flag.
class WebKitNavigationStartingEventArgs final
    : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, WebKit::IWebKitNavigationStartingEventArgs> {
    InspectableClass(L"WebKit.WebKitNavigationStartingEventArgs", BaseTrust);

public:
    // Captures the target URI and navigation ID before the policy callback.
    HRESULT RuntimeClassInitialize(const wchar_t* url, UINT64 id)
    {
        m_id = id;
        return m_url.Set(url);
    }

    // Copies the proposed navigation URI into a caller-owned HSTRING.
    IFACEMETHODIMP get_Uri(HSTRING* value) override { return m_url.CopyTo(value); }

    // Returns the identifier assigned to this navigation attempt.
    IFACEMETHODIMP get_NavigationId(UINT64* value) override
    {
        *value = m_id;
        return S_OK;
    }

    // Returns whether a handler vetoed the navigation.
    IFACEMETHODIMP get_Cancel(boolean* value) override
    {
        *value = m_cancel;
        return S_OK;
    }

    // Records the host veto while synchronous policy dispatch is in progress.
    IFACEMETHODIMP put_Cancel(boolean value) override
    {
        m_cancel = value;
        return S_OK;
    }

    // Exposes the final veto decision to the engine policy trampoline.
    bool cancelled() const { return !!m_cancel; }

private:
    // Target URI retained for the policy event lifetime.
    HString m_url;

    // Identifier shared with later events from this navigation attempt.
    UINT64 m_id { kInitialComponentNavigationId };

    // Host-controlled veto flag; false allows navigation.
    boolean m_cancel { false };
};

// Commit event payload: committed URL plus whether it started a new document.
class WebKitSourceChangedEventArgs final
    : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, WebKit::IWebKitSourceChangedEventArgs> {
    InspectableClass(L"WebKit.WebKitSourceChangedEventArgs", BaseTrust);

public:
    // Captures the committed URI and document-change flag before dispatch.
    HRESULT RuntimeClassInitialize(const wchar_t* url, bool isNewDocument)
    {
        m_isNewDocument = isNewDocument;
        return m_url.Set(url);
    }

    // Copies the committed URI into a caller-owned HSTRING.
    IFACEMETHODIMP get_Uri(HSTRING* value) override { return m_url.CopyTo(value); }

    // Distinguishes a newly committed document from a same-document URL change.
    IFACEMETHODIMP get_IsNewDocument(boolean* value) override
    {
        *value = m_isNewDocument;
        return S_OK;
    }

private:
    // Committed URI retained for the source event lifetime.
    HString m_url;

    // True when the source change represents a new document.
    boolean m_isNewDocument { false };
};

// Already-completed async action returned for synchronous operations like EnsureInitializedAsync.
class CompletedAsyncAction final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, Windows::Foundation::IAsyncAction, Windows::Foundation::IAsyncInfo> {
    InspectableClass(L"WebKit.CompletedAsyncAction", BaseTrust);

public:
    // Immediately invokes a supplied handler because initialization already completed.
    IFACEMETHODIMP put_Completed(Windows::Foundation::IAsyncActionCompletedHandler* handler) override
    {
        if (!handler)
            return S_OK;
        ComPtr<Windows::Foundation::IAsyncAction> self(this);
        return handler->Invoke(self.Get(), static_cast<INT32>(Windows::Foundation::AsyncStatusValue::Completed));
    }

    // Returns null because this implementation does not retain a completion delegate.
    IFACEMETHODIMP get_Completed(Windows::Foundation::IAsyncActionCompletedHandler** handler) override
    {
        *handler = nullptr;
        return S_OK;
    }

    // Returns success with no payload for the completed initialization action.
    IFACEMETHODIMP GetResults() override { return S_OK; }

    // Returns the local identifier of this completed initialization action.
    IFACEMETHODIMP get_Id(UINT32* id) override
    {
        *id = kCompletedAsyncActionId;
        return S_OK;
    }

    // Reports successful completion using the OS AsyncStatus numeric contract.
    IFACEMETHODIMP get_Status(INT32* status) override
    {
        *status = static_cast<INT32>(Windows::Foundation::AsyncStatusValue::Completed);
        return S_OK;
    }

    // Reports no failure because initialization completed before creating this action.
    IFACEMETHODIMP get_ErrorCode(HRESULT* code) override
    {
        *code = S_OK;
        return S_OK;
    }

    // Cancellation has no effect on an already-completed action.
    IFACEMETHODIMP Cancel() override { return S_OK; }

    // Closing has no effect because the action retains no operation resources.
    IFACEMETHODIMP Close() override { return S_OK; }
};

// Single routed instance (Phase A): the engine log callback carries no
// context. A second control refuses init rather than losing events silently.
// Headless for Phase A: XAML composition is out (see RuntimeClassInitialize).
// Visual hosting returns in Phase B as containment behind a real parent.
class WebKitWebView final
    : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, WebKit::IWebKitWebView> {
    // Identity methods are explicit (no InspectableClass macro, so the
    // trust level stays BaseTrust). Only the default interface is enumerated.
public:
    // Reports IWebKitWebView as the public interface implemented by this object.
    IFACEMETHODIMP GetIids(ULONG* count, IID** iids) override
    {
        *iids = static_cast<IID*>(CoTaskMemAlloc(sizeof(IID)));
        if (!*iids)
            return E_OUTOFMEMORY;
        **iids = __uuidof(WebKit::IWebKitWebView);
        *count = kWebKitWebViewInterfaceCount;
        return S_OK;
    }

    // Returns the WinRT runtime-class name used by activation clients.
    IFACEMETHODIMP GetRuntimeClassName(HSTRING* name) override
    {
        return WindowsCreateString(kWebKitWebViewClassName, kWebKitWebViewClassNameLength, name);
    }

    // Reports BaseTrust for this OS-API-only component.
    IFACEMETHODIMP GetTrustLevel(TrustLevel* level) override
    {
        *level = BaseTrust;
        return S_OK;
    }

    // Initializes identity only; Phase A is headless and owns no XAML base.
    HRESULT RuntimeClassInitialize()
    {
        // No XAML base is composed. Device evidence (1.0.0.73-75) shows the
        // composed UserControl answers neither IContentControl nor the inner
        // object QI on this phone, so hosting the tree in it cannot work
        // here. Phase A needs no visuals: the engine navigates headless and
        // all contract events are engine-driven. Phase B hosts the wrapper
        // tree in a real parent by containment instead.
        return S_OK;
    }

    // Last committed main-frame URL; E_FAIL before initialization.
    IFACEMETHODIMP get_Source(HSTRING* value) override
    {
        if (!m_initialized)
            return E_FAIL;
        return m_source.CopyTo(value);
    }

    // Remote http(s) navigation; other schemes are E_INVALIDARG.
    IFACEMETHODIMP put_Source(HSTRING value) override
    {
        HRESULT hr = EnsureInitializedAsync(nullptr);
        if (FAILED(hr))
            return hr;
        UINT32 length = kEmptyStringLength;
        const wchar_t* raw = WindowsGetStringRawBuffer(value, &length);
        std::wstring url(raw ? raw : L"", length);
        if (url.rfind(kHttpSchemePrefix, kUriPrefixSearchStart) != kUriPrefixSearchStart
            && url.rfind(kHttpsSchemePrefix, kUriPrefixSearchStart) != kUriPrefixSearchStart)
            return E_INVALIDARG;
        return navigate(url);
    }

    // Back/forward availability from current engine history counts.
    IFACEMETHODIMP get_CanGoBack(boolean* value) override
    {
        WKPageStateUWP state;
        HRESULT hr = pageState(state);
        if (FAILED(hr))
            return hr;
        *value = !!state.canGoBack;
        return S_OK;
    }

    // Reports whether a forward session-history item exists.
    IFACEMETHODIMP get_CanGoForward(boolean* value) override
    {
        WKPageStateUWP state;
        HRESULT hr = pageState(state);
        if (FAILED(hr))
            return hr;
        *value = !!state.canGoForward;
        return S_OK;
    }

    // Traverses engine history; HistoryChanged follows when counts move.
    IFACEMETHODIMP GoBack() override
    {
        if (!m_view)
            return E_FAIL;
        HRESULT hr = m_view->goBack();
        if (SUCCEEDED(hr))
            fireHistoryChangedIfMoved();
        return hr;
    }

    // Traverses engine history forward; HistoryChanged follows when counts move.
    IFACEMETHODIMP GoForward() override
    {
        if (!m_view)
            return E_FAIL;
        HRESULT hr = m_view->goForward();
        if (SUCCEEDED(hr))
            fireHistoryChangedIfMoved();
        return hr;
    }

    // Reloads the current document in place; creates no history entry.
    IFACEMETHODIMP Reload() override { return m_view ? m_view->reload() : E_FAIL; }

    // Stops all in-flight loads for the page.
    IFACEMETHODIMP Stop() override { return m_view ? m_view->stop() : E_FAIL; }

    // Loads app-supplied HTML with the page's origin/base rules.
    IFACEMETHODIMP NavigateToString(HSTRING html) override
    {
        HRESULT hr = EnsureInitializedAsync(nullptr);
        if (FAILED(hr))
            return hr;
        UINT32 length = 0;
        const wchar_t* raw = WindowsGetStringRawBuffer(html, &length);
        int urlLength = WideCharToMultiByte(CP_UTF8, kUtf8ConversionFlags, raw, length, nullptr, kConversionSizeQueryOnly, nullptr, nullptr);
        std::string utf8(urlLength, kZeroInitializedByte);
        WideCharToMultiByte(CP_UTF8, kUtf8ConversionFlags, raw, length, utf8.data(), urlLength, nullptr, nullptr);
        return m_view->loadHTML(utf8.c_str());
    }

    // Starts the engine on first use; returns an already-completed action.
    IFACEMETHODIMP EnsureInitializedAsync(Windows::Foundation::IAsyncAction** operation) override
    {
        HRESULT hr = ensureInitialized();
        if (FAILED(hr))
            return hr;
        if (!operation)
            return S_OK; // internal callers need only the side effect
        return MakeAndInitialize<CompletedAsyncAction>(operation);
    }

    // Subscribes to the synchronous policy-time veto event.
    IFACEMETHODIMP add_NavigationStarting(WebKit::IWebKitNavStartingHandler* handler, EventRegistrationToken* token) override
    {
        return m_startingEvent.Add(handler, token);
    }

    // Removes the policy-time veto subscription.
    IFACEMETHODIMP remove_NavigationStarting(EventRegistrationToken token) override
    {
        return m_startingEvent.Remove(token);
    }

    // Subscribes to the commit-time URL event.
    IFACEMETHODIMP add_SourceChanged(WebKit::IWebKitSourceChangedHandler* handler, EventRegistrationToken* token) override
    {
        return m_sourceEvent.Add(handler, token);
    }

    // Removes the commit-time URL subscription.
    IFACEMETHODIMP remove_SourceChanged(EventRegistrationToken token) override
    {
        return m_sourceEvent.Remove(token);
    }

    // Subscribes to history-count changes.
    IFACEMETHODIMP add_HistoryChanged(WebKit::IWebKitNavigationEventHandler* handler, EventRegistrationToken* token) override
    {
        return m_historyEvent.Add(handler, token);
    }

    // Removes the history-count-change subscription.
    IFACEMETHODIMP remove_HistoryChanged(EventRegistrationToken token) override
    {
        return m_historyEvent.Remove(token);
    }

    // Subscribes to successful main-frame navigation completion.
    IFACEMETHODIMP add_NavigationCompleted(WebKit::IWebKitNavigationEventHandler* handler, EventRegistrationToken* token) override
    {
        return m_completedEvent.Add(handler, token);
    }

    // Removes the successful-load subscription.
    IFACEMETHODIMP remove_NavigationCompleted(EventRegistrationToken token) override
    {
        return m_completedEvent.Remove(token);
    }

    // Subscribes to failed main-frame navigation.
    IFACEMETHODIMP add_NavigationFailed(WebKit::IWebKitNavigationEventHandler* handler, EventRegistrationToken* token) override
    {
        return m_failedEvent.Add(handler, token);
    }

    // Removes the failed-load subscription.
    IFACEMETHODIMP remove_NavigationFailed(EventRegistrationToken token) override
    {
        return m_failedEvent.Remove(token);
    }

private:
    // Starts a remote navigation with the packaged CA bundle; E_INVALIDARG on bad input.
    HRESULT navigate(const std::wstring& url)
    {
        std::wstring caPath = packagePath() + L"\\cacert.pem";
        int urlLength = WideCharToMultiByte(CP_UTF8, kUtf8ConversionFlags, url.c_str(), kNullTerminatedInputLength, nullptr, kConversionSizeQueryOnly, nullptr, nullptr);
        int caLength = WideCharToMultiByte(CP_UTF8, kUtf8ConversionFlags, caPath.c_str(), kNullTerminatedInputLength, nullptr, kConversionSizeQueryOnly, nullptr, nullptr);
        if (urlLength <= kNullTerminatorCharacterCount || caLength <= kNullTerminatorCharacterCount)
            return E_INVALIDARG;
        std::string narrowUrl(urlLength, kZeroInitializedByte);
        int convertedLength = WideCharToMultiByte(CP_UTF8, kUtf8ConversionFlags, url.c_str(), kNullTerminatedInputLength, narrowUrl.data(), urlLength, nullptr, nullptr);
        if (convertedLength != urlLength)
            return HRESULT_FROM_WIN32(GetLastError());
        narrowUrl.resize(urlLength - kNullTerminatorCharacterCount);
        return m_view->navigateRemote(narrowUrl.c_str(), caPath.c_str());
    }

    // Reads engine scroll/size/history state; E_FAIL before initialization.
    HRESULT pageState(WKPageStateUWP& state)
    {
        if (!m_view)
            return E_FAIL;
        return m_view->pageState(state);
    }

    // Install location of the hosting package; empty when unavailable.
    static std::wstring packagePath()
    {
        ComPtr<ABI::Windows::ApplicationModel::IPackageStatics> packages;
        ComPtr<ABI::Windows::ApplicationModel::IPackage> package;
        ComPtr<ABI::Windows::Storage::IStorageFolder> folder;
        HString path;
        HRESULT hr = RoGetActivationFactory(
            HStringReference(RuntimeClass_Windows_ApplicationModel_Package).Get(), IID_PPV_ARGS(&packages));
        if (FAILED(hr))
            return {};
        hr = packages->get_Current(&package);
        if (FAILED(hr))
            return {};
        hr = package->get_InstalledLocation(&folder);
        if (FAILED(hr))
            return {};
        ComPtr<ABI::Windows::Storage::IStorageItem> item;
        hr = folder.As(&item);
        if (FAILED(hr))
            return {};
        hr = item->get_Path(path.GetAddressOf());
        if (FAILED(hr))
            return {};
        UINT32 length = 0;
        const wchar_t* raw = WindowsGetStringRawBuffer(path.Get(), &length);
        return std::wstring(raw ? raw : L"", length);
    }

    // Creates the wrapper and registers the policy veto. Headless: no XAML
    // hosting (see RuntimeClassInitialize). Refuses a second instance: the
    // log sink cannot route two pages yet.
    HRESULT ensureInitialized()
    {
        if (m_initialized)
            return S_OK;
        if (s_sinkInstance)
            return E_FAIL;
        m_view = std::make_unique<WebKit::WebKitWebViewUWP>();
        std::wstring fonts = packagePath() + L"\\Fonts";
        HRESULT hr = m_view->initialize(fonts.c_str(), &OnEngineLog);
        if (FAILED(hr)) {
            m_view.reset();
            return hr;
        }
        hr = m_view->setNavigationPolicyHandler(&OnPolicy, this);
        if (FAILED(hr)) {
            m_view.reset();
            return hr;
        }
        s_sinkInstance = this;
        m_initialized = true;
        return S_OK;
    }

    // Fires HistoryChanged only when Back/Forward counts actually moved.
    void fireHistoryChangedIfMoved()
    {
        WKPageStateUWP state;
        if (!m_view || FAILED(m_view->pageState(state)))
            return;
        if (!!state.canGoBack == !!m_canGoBack && !!state.canGoForward == !!m_canGoForward)
            return;
        m_canGoBack = !!state.canGoBack;
        m_canGoForward = !!state.canGoForward;
        ComPtr<WebKit::IWebKitNavigationEventArgs> args;
        if (FAILED(MakeAndInitialize<WebKitNavigationEventArgs>(
                &args, m_lastCommitted.c_str(), m_activeId, WebKit::WebKitNavigationOutcome_Completed, kNoNavigationErrorCode)))
            return;
        m_historyEvent.InvokeAll(sender(), args.Get());
    }

    // IInspectable identity handed to event handlers as sender.
    IInspectable* sender()
    {
        return static_cast<IInspectable*>(static_cast<WebKit::IWebKitWebView*>(this));
    }

    // Engine log tap: commit/finish/failure lines carry everything v1 needs.
    static void __stdcall OnEngineLog(const char* stage, unsigned long result)
    {
        char message[1024];
        int length = sprintf_s(message, "%s: 0x%08lx\r\n", stage, result);
        OutputDebugStringA(message);
        WebKitWebView* self = s_sinkInstance;
        if (!self)
            return;
        static const char kCommitted[] = "navigation-committed-url: ";
        static const char kCompleted[] = "navigation-completed-url: ";
        static const char kFailed[] = "navigation-provisional-error-url: ";
        if (!strncmp(stage, kCommitted, sizeof(kCommitted) - 1))
            self->onCommitted(stage + sizeof(kCommitted) - 1);
        else if (!strncmp(stage, kCompleted, sizeof(kCompleted) - 1))
            self->onCompleted(stage + sizeof(kCompleted) - 1, result);
        else if (!strncmp(stage, kFailed, sizeof(kFailed) - 1))
            self->onFailed(stage + sizeof(kFailed) - 1, result);
    }

    // Veto trampoline: fires Starting synchronously so Cancel lands in time.
    static bool __stdcall OnPolicy(void* context, const char* utf8Url)
    {
        return static_cast<WebKitWebView*>(context)->fireStarting(utf8Url);
    }

    // Builds the Starting payload, invokes host handlers and returns their veto decision.
    bool fireStarting(const char* utf8Url)
    {
        std::wstring url = utf8(utf8Url);
        if (sameDocument(url, m_lastCommitted))
            return true; // fragment-only: no event, allow
        UINT64 id = ++m_navId; // assigns the ID shared by this navigation's later events
        m_activeId = id;
        m_activeUrl = url;
        ComPtr<WebKit::IWebKitNavigationStartingEventArgs> args;
        HRESULT hr = MakeAndInitialize<WebKitNavigationStartingEventArgs>(&args, url.c_str(), id);
        if (FAILED(hr))
            return true;
        if (FAILED(m_startingEvent.InvokeAll(sender(), args.Get())))
            return true;
        boolean cancel = false;
        if (FAILED(args->get_Cancel(&cancel)))
            return true;
        return !cancel;
    }

    // Records the committed URL, updates Source, and fires SourceChanged.
    void onCommitted(const char* utf8Url)
    {
        std::wstring url = utf8(utf8Url);
        bool isNew = !sameDocument(url, m_lastCommitted);
        m_lastCommitted = url; // SourceChanged address following
        m_activeUrl = url; // redirect finalization for later attribution
        m_source.Set(url.c_str());
        ComPtr<WebKit::IWebKitSourceChangedEventArgs> args;
        if (FAILED(MakeAndInitialize<WebKitSourceChangedEventArgs>(&args, url.c_str(), isNew)))
            return;
        m_sourceEvent.InvokeAll(sender(), args.Get());
    }

    // Fires Completed and then HistoryChanged when counts moved with it.
    void onCompleted(const char* utf8Url, unsigned long)
    {
        fireTerminal(utf8(utf8Url), WebKit::WebKitNavigationOutcome_Completed, kNoNavigationErrorCode, m_completedEvent);
        fireHistoryChangedIfMoved();
    }

    // Maps engine cancellation to Cancelled; other transfer failures map to Failed.
    void onFailed(const char* utf8Url, unsigned long code)
    {
        fireTerminal(utf8(utf8Url), code == WKNavigationFailureCancelledUWP ? WebKit::WebKitNavigationOutcome_Cancelled : WebKit::WebKitNavigationOutcome_Failed, static_cast<INT32>(code), m_failedEvent);
    }

    // Builds terminal args against the active navigation ID and fires the event.
    void fireTerminal(const std::wstring& url, INT32 outcome, INT32 code,
        EventSource<WebKit::IWebKitNavigationEventHandler>& event)
    {
        ComPtr<WebKit::IWebKitNavigationEventArgs> args;
        if (FAILED(MakeAndInitialize<WebKitNavigationEventArgs>(
                &args, url.c_str(), m_activeId, outcome, code)))
            return;
        event.InvokeAll(sender(), args.Get());
    }

    // Converts engine UTF-8 log text; empty on invalid input.
    static std::wstring utf8(const char* text)
    {
        int length = MultiByteToWideChar(CP_UTF8, kUtf8ConversionFlags, text, kNullTerminatedInputLength, nullptr, kConversionSizeQueryOnly);
        if (length <= 1)
            return {};
        std::wstring wide(length, L'\0');
        int convertedLength = MultiByteToWideChar(CP_UTF8, kUtf8ConversionFlags, text, kNullTerminatedInputLength, wide.data(), length);
        if (convertedLength != length)
            return {};
        wide.resize(length - kNullTerminatorCharacterCount);
        return wide;
    }

    // Removes a fragment so URL equality detects same-document navigation.
    static bool sameDocument(const std::wstring& a, const std::wstring& b)
    {
        // Fragment-only difference means same document (matches WebView2).
        auto strip = [](const std::wstring& url) {
            size_t fragment = url.find(L'#');
            return fragment == std::wstring::npos ? url : url.substr(0, fragment);
        };
        return !a.empty() && strip(a) == strip(b);
    }

    // Reused native engine wrapper backing this component instance.
    std::unique_ptr<WebKit::WebKitWebViewUWP> m_view;

    // Host callbacks for successful main-frame completion.
    EventSource<WebKit::IWebKitNavigationEventHandler> m_completedEvent;

    // Host callbacks for failed or cancelled main-frame completion.
    EventSource<WebKit::IWebKitNavigationEventHandler> m_failedEvent;

    // Host callbacks when Back/Forward availability changes.
    EventSource<WebKit::IWebKitNavigationEventHandler> m_historyEvent;

    // Host callbacks for the synchronous policy-time navigation veto.
    EventSource<WebKit::IWebKitNavStartingHandler> m_startingEvent;

    // Host callbacks when the committed URL changes.
    EventSource<WebKit::IWebKitSourceChangedHandler> m_sourceEvent;

    // Last committed URL returned by the Source property.
    HString m_source;

    // Previous committed URL used for fragment-only comparisons.
    std::wstring m_lastCommitted;

    // Current or redirect-final URL used to attribute terminal events.
    std::wstring m_activeUrl;

    // ID shared by the active navigation's event payloads.
    UINT64 m_activeId { 0 };

    // Last assigned navigation ID.
    UINT64 m_navId { 0 };

    // Cached Back availability for change-only event delivery.
    unsigned m_canGoBack { 0 };

    // Cached Forward availability for change-only event delivery.
    unsigned m_canGoForward { 0 };

    // True after the engine wrapper and policy handler are ready.
    bool m_initialized { false };

    // Sole routed engine log sink until multi-instance routing is implemented.
    static WebKitWebView* s_sinkInstance;
};

// Process-wide Phase-A engine-log routing target.
WebKitWebView* WebKitWebView::s_sinkInstance = nullptr;

// Plain activation factory creating WebKitWebView; composability is internal.
// Manual identity methods are needed because InspectableClass rejects factories.
class WebKitWebViewFactory final
    : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IActivationFactory> {
public:
    // Reports IActivationFactory as the factory's sole public interface.
    IFACEMETHODIMP GetIids(ULONG* count, IID** iids) override
    {
        *iids = static_cast<IID*>(CoTaskMemAlloc(sizeof(IID)));
        if (!*iids)
            return E_OUTOFMEMORY;
        **iids = __uuidof(IActivationFactory);
        *count = kFactoryInterfaceCount;
        return S_OK;
    }

    // Returns the runtime-class identity served by this factory.
    IFACEMETHODIMP GetRuntimeClassName(HSTRING* name) override
    {
        return WindowsCreateString(kWebKitWebViewClassName, kWebKitWebViewClassNameLength, name);
    }

    // Reports BaseTrust for the OS-backed activation factory.
    IFACEMETHODIMP GetTrustLevel(TrustLevel* level) override
    {
        *level = BaseTrust;
        return S_OK;
    }

    // Creates one component instance for RoActivateInstance.
    IFACEMETHODIMP ActivateInstance(IInspectable** instance) override
    {
        // Creates and composes one control; the caller owns the reference.
        return MakeAndInitialize<WebKitWebView>(instance);
    }
};

// Serves WebKit.WebKitWebView only; other class IDs are unavailable.
STDAPI DllGetActivationFactory(HSTRING classId, IActivationFactory** factory)
{
    UINT32 length = 0;
    const wchar_t* raw = WindowsGetStringRawBuffer(classId, &length);
    std::wstring name(raw ? raw : L"", length);
    if (name != kWebKitWebViewClassName)
        return CLASS_E_CLASSNOTAVAILABLE;
    return MakeAndInitialize<WebKitWebViewFactory>(factory);
}

// Keeps the v1 component loaded until instance lifetime tracking is implemented.
STDAPI DllCanUnloadNow()
{
    return S_FALSE;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(module);
    return TRUE;
}

} // namespace WebKitComponent
