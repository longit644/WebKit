// Keep this order: generated contract references the hand-declared async
// types, and curl's winsock2 declarations must follow winsock2.h.
// clang-format off
#include "WebKitWebViewUWP.h"
#include "WebKitWinRTBase.h"
#include "WebKitWebView.h" // component contract (Phase-A diagnostic only)
#include <stdio.h>
#include <string>
#include <vector>
#include <windows.applicationmodel.activation.h>
#include <windows.applicationmodel.h>
#include <windows.foundation.h>
#include <windows.storage.h>
#include <windows.ui.core.h>
#include <windows.ui.viewmanagement.h>
#include <windows.ui.xaml.markup.h>
#include <windows.ui.xaml.media.h>
#include <winsock2.h>
#include <curl/curl.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
// clang-format on

using namespace Microsoft::WRL;
using Microsoft::WRL::Wrappers::HString;
using Microsoft::WRL::Wrappers::HStringReference;
namespace Xaml = ABI::Windows::UI::Xaml;
namespace Media = ABI::Windows::UI::Xaml::Media;
namespace Controls = ABI::Windows::UI::Xaml::Controls;
namespace Foundation = ABI::Windows::Foundation;
namespace Activation = ABI::Windows::ApplicationModel::Activation;
namespace Storage = ABI::Windows::Storage;

static wchar_t logPath[1024];

// Stage values for the temporary blocked-template smoke sequence.
enum BlockedTemplateTestStage : unsigned {
    // Suite has not yet issued its packaged-template failure.
    BlockedTemplateTestNotStarted = 0,

    // Packaged template failure was requested; waiting for its interstitial.
    BlockedTemplateTestPackagedPagePending = 1,

    // Override template failure was requested; waiting for its interstitial.
    BlockedTemplateTestOverridePagePending = 2,

    // Both template sources were observed or the test timed out.
    BlockedTemplateTestFinished = 3
};

// Stage values for the temporary component event-sequence diagnostic.
enum ComponentDiagnosticStage : unsigned {
    // Not started until the native shell has initialized.
    ComponentDiagnosticNotStarted = 0,

    // Initial fixture navigation is running.
    ComponentDiagnosticIndexPending = 1,

    // Second fixture navigation is running.
    ComponentDiagnosticSecondPending = 2,

    // Back traversal is running.
    ComponentDiagnosticBackPending = 3,

    // Policy-veto navigation is running.
    ComponentDiagnosticVetoPending = 4,

    // Every component event scenario has been issued.
    ComponentDiagnosticFinished = 5
};

// Source IDs logged by the blocked-page template resolver.
enum BlockedTemplateSource : unsigned long {
    // No file was readable; the embedded minimal page was used.
    BlockedTemplateSourceEmbeddedFallback = 0,

    // Install-directory blocked.html supplied the page.
    BlockedTemplateSourcePackagedFile = 1,

    // App LocalState blocked.html overrode the packaged default.
    BlockedTemplateSourceLocalStateFile = 2,

    // Explicit path supplied by the MiniBrowser host took precedence.
    BlockedTemplateSourceExplicitPath = 3
};

// Render-tick budgets at 60 Hz used by the temporary diagnostics.
constexpr unsigned kBlockedTemplateTestMaxTicks = 1800;
constexpr unsigned kComponentSecondNavigationTick = 600;
constexpr unsigned kComponentBackNavigationTick = 1200;
constexpr unsigned kComponentVetoNavigationTick = 1800;
constexpr unsigned kComponentDiagnosticFinishTick = 2400;
constexpr unsigned kNativeNavigationSampleCount = 8;
constexpr unsigned kNativeNavigationSampleInterval = 30;
constexpr unsigned kNativeNavigationMaxTicks = 600;
constexpr unsigned kExpectedBlockedTemplateTestCount = 2;
constexpr unsigned kFirstBlockedTemplateTestCount = 1;
constexpr size_t kTrailingSlashCharacterCount = 1;
constexpr size_t kUrlPrefixSearchStart = 0;
constexpr size_t kNoHttpStatusCode = 0;
constexpr DWORD kWorkerThreadSuccess = 0;
constexpr size_t kFetchAbortCallbackResult = 0;
constexpr int kCStringComparisonEqual = 0;
constexpr unsigned kNativeNavigationSampleBoundary = 0;
constexpr UINT kUtf8ConversionFlags = 0;
constexpr int kNullTerminatedInputLength = -1;
constexpr int kConversionSizeQueryOnly = 0;
constexpr int kNullTerminatorCharacterCount = 1;
constexpr wchar_t kComponentFixtureIndexURL[] = L"http://192.168.1.69:8002/index.html";
constexpr wchar_t kComponentFixtureSuiteURL[] = L"http://192.168.1.69:8002/index.html?uwp-suite=1";
constexpr wchar_t kComponentFixtureSecondURL[] = L"http://192.168.1.69:8002/second.html";

// Windows debugger-print exception records emitted by OutputDebugString.
constexpr DWORD kDbgPrintExceptionC = 0x40010006;
constexpr DWORD kDbgPrintExceptionWideC = 0x4001000a;

// Network failure code and suite completion result shared with engine code/log protocol.
constexpr unsigned long kCancelledNavigationErrorCode = WKNavigationFailureCancelledUWP;
constexpr unsigned long kFixtureSuiteSuccessResult = 1;
constexpr char kFixtureSuiteCompletedStage[] = "fixture-suite-completed";
constexpr long kHttpSuccessfulStatusFirst = 200;
constexpr long kHttpSuccessfulStatusPastLast = 300;
constexpr UINT64 kInitialComponentNavigationId = 0;
constexpr UINT32 kInitialComponentStringLength = 0;
constexpr INT32 kInitialComponentOutcome = WebKit::WebKitNavigationOutcome_Completed;
constexpr INT32 kNoComponentErrorCode = 0;
constexpr wchar_t kAboutDemoURL[] = L"about:demo";
constexpr wchar_t kAboutSecondURL[] = L"about:second";
constexpr wchar_t kAboutPlainURL[] = L"about:plain";
constexpr wchar_t kAboutHeavyURL[] = L"about:heavy";
constexpr char kAboutSecondHtmlLabel[] = "about:second";
constexpr wchar_t kBlockedTemplateFilename[] = L"blocked.html";
constexpr wchar_t kBlockedTemplateOverrideFilename[] = L"blocked-override.html";
constexpr unsigned kFirstDemoSectionIndex = 1;
constexpr unsigned kPlainDemoSectionCount = 30;
constexpr unsigned kHeavyDemoSectionCount = 20;
constexpr unsigned kScrollableDemoSectionCount = 18;
constexpr size_t kNativeFaultPathLogCapacity = 512;
constexpr size_t kNativeFaultPathEscapeReserve = 4;
constexpr ULONG kMaxExceptionParametersToLog = 4;
constexpr LONG kFaultLogLockAcquired = 1;
constexpr LONG kFaultLogLockReleased = 0;
constexpr unsigned kAsciiCodePointLimit = 128;
constexpr char kNonAsciiPathReplacement = '?';

// Latest failed main-frame navigation, stashed here by the engine log callback
// and consumed on the UI Rendering tick. Cancelled codes never display (a real
// navigation follows); only certificate failures offer the consented proceed
// control, everything else just explains itself.
static std::string s_failedNavUrl;
static unsigned long s_failedNavCode = 0;
// TEMPORARY blocked-template test gate: the fixture suite owns the view until
// it completes. Remove before acceptance.
static bool s_suiteDone = false;

// Appends one diagnostic line to the device log and stashes failed main-frame
// navigations for the blocked-page display on the UI tick.
static void __stdcall logResult(const char* stage, unsigned long result)
{
    char message[1024];
    int length = sprintf_s(message, "%s: 0x%08lx\r\n", stage, result);
    OutputDebugStringA(message);
    HANDLE file = CreateFile2(logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_ALWAYS, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(file, message, length, &written, nullptr);
        CloseHandle(file);
    }
    static const char kFailedNavPrefix[] = "navigation-provisional-error-url: ";
    static const size_t kFailedNavPrefixLength = sizeof(kFailedNavPrefix) - 1;
    // Cancelled navigations are superseded by design; the real navigation reports next.
    if (result != kCancelledNavigationErrorCode && strncmp(stage, kFailedNavPrefix, kFailedNavPrefixLength) == kCStringComparisonEqual) {
        s_failedNavUrl.assign(stage + kFailedNavPrefixLength);
        s_failedNavCode = result;
    }

    // TEMPORARY blocked-template test gate; remove before acceptance.
    // PageUWP logs fixture-suite-completed with 1 on success, 0 on timeout.
    if (result == kFixtureSuiteSuccessResult && strcmp(stage, kFixtureSuiteCompletedStage) == kCStringComparisonEqual)
        s_suiteDone = true;
}

// Temporary native-fault evidence when Device Portal has no fresh WER dump.
// Observe faults only; preserve Windows' normal exception/crash handling.
// Writes an already-formatted line without touching the stack-heavy sprintf
// path, so this stays usable while diagnosing stack exhaustion.
static void __stdcall logLine(const char* text)
{
    HANDLE file = CreateFile2(logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_ALWAYS, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(file, text, static_cast<DWORD>(strlen(text)), &written, nullptr);
        CloseHandle(file);
    }
}

// Logs the image path, module base and relative address for a native fault.
static void __stdcall logModuleIdentity(const char* stage, ULONG_PTR address)
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(address), &module)
        || !module)
        return;
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(module, path, MAX_PATH);
    auto base = reinterpret_cast<ULONG_PTR>(module);
    logResult(stage, static_cast<unsigned long>(base));
    logResult("native-fault-module-rva", static_cast<unsigned long>(address - base));
    char line[kNativeFaultPathLogCapacity] = {};
    size_t used = 0;
    for (size_t i = 0; path[i] && used + kNativeFaultPathEscapeReserve < sizeof(line); i++)
        used += static_cast<size_t>(sprintf_s(line + used, sizeof(line) - used, "%c",
            path[i] < kAsciiCodePointLimit ? static_cast<char>(path[i]) : kNonAsciiPathReplacement));
    sprintf_s(line + used, sizeof(line) - used, "\r\n");
    logLine(line);
}

// Every exception code is logged, not just the access-violation family. A
// Release WebKit build resolves CRASH() to std::abort(), which terminates
// through STATUS_CONTROL_C_EXIT rather than faulting, so a filter that ignores
// non-access codes reports nothing for an assertion or allocation failure.
static LONG CALLBACK logNativeFault(EXCEPTION_POINTERS* exception)
{
    // OutputDebugString emits debugger notifications (DBG_PRINTEXCEPTION_C and
    // DBG_PRINTEXCEPTION_WIDE_C), not application faults.
    if (exception->ExceptionRecord->ExceptionCode == kDbgPrintExceptionC
        || exception->ExceptionRecord->ExceptionCode == kDbgPrintExceptionWideC)
        return EXCEPTION_CONTINUE_SEARCH;
    static LONG logging = 0;
    if (InterlockedCompareExchange(&logging, kFaultLogLockAcquired, kFaultLogLockReleased))
        return EXCEPTION_CONTINUE_SEARCH;
    auto* record = exception->ExceptionRecord;
    logResult("native-fault-code", record->ExceptionCode);
    logResult("native-fault-address", static_cast<unsigned long>(reinterpret_cast<ULONG_PTR>(record->ExceptionAddress)));
    logResult("native-fault-params", record->NumberParameters);
    for (ULONG i = 0; i < record->NumberParameters && i < kMaxExceptionParametersToLog; i++)
        logResult("native-fault-info", static_cast<unsigned long>(record->ExceptionInformation[i]));
    logModuleIdentity("native-fault-module-base", reinterpret_cast<ULONG_PTR>(record->ExceptionAddress));
    logResult("native-fault-webcore-base", static_cast<unsigned long>(reinterpret_cast<ULONG_PTR>(GetModuleHandleW(L"WebCore.dll"))));
    logResult("native-fault-jsc-base", static_cast<unsigned long>(reinterpret_cast<ULONG_PTR>(GetModuleHandleW(L"JavaScriptCore.dll"))));
    logResult("native-fault-libpng-base", static_cast<unsigned long>(reinterpret_cast<ULONG_PTR>(GetModuleHandleW(L"libpng16.dll"))));
    logResult("native-fault-sqlite-base", static_cast<unsigned long>(reinterpret_cast<ULONG_PTR>(GetModuleHandleW(L"sqlite3.dll"))));
    logResult("native-fault-msvcp-base", static_cast<unsigned long>(reinterpret_cast<ULONG_PTR>(GetModuleHandleW(L"msvcp140.dll"))));
    logResult("native-fault-icuuc-base", static_cast<unsigned long>(reinterpret_cast<ULONG_PTR>(GetModuleHandleW(L"icuuc78.dll"))));
    logResult("native-fault-ucrt-base", static_cast<unsigned long>(reinterpret_cast<ULONG_PTR>(GetModuleHandleW(L"ucrtbase.dll"))));
    logResult("native-fault-vcruntime-base", static_cast<unsigned long>(reinterpret_cast<ULONG_PTR>(GetModuleHandleW(L"vcruntime140.dll"))));
    InterlockedExchange(&logging, kFaultLogLockReleased);
    return EXCEPTION_CONTINUE_SEARCH;
}

// Queries a WinRT StorageFolder for its filesystem path.
static HRESULT folderPath(Storage::IStorageFolder* folder, HString& path)
{
    ComPtr<Storage::IStorageItem> item;
    HRESULT hr = folder->QueryInterface(IID_PPV_ARGS(&item));
    return FAILED(hr) ? hr : item->get_Path(path.GetAddressOf());
}

static std::string escapeHtml(const std::string& text)
{
    std::string escaped;
    for (char c : text) {
        switch (c) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '"':
            escaped += "&quot;";
            break;
        default:
            escaped += c;
            break;
        }
    }
    return escaped;
}

static std::string urlHost(const std::string& url)
{
    size_t scheme = url.find("://");
    size_t start = scheme == std::string::npos ? 0 : scheme + 3;
    size_t end = url.find('/', start);
    return url.substr(start, end == std::string::npos ? end : end - start);
}

static std::string plainHTML()
{
    std::string html = "<!doctype html><html><head><meta charset='utf-8'><style>body{font:18px 'DejaVu Sans';background:white;color:black;margin:24px}h1{font-size:28px}div{margin:18px 0}</style></head><body>";
    html += "<h1>Plain baseline</h1><p>No sticky, fixed, gradient, radius or shadow. Scroll only.</p>";
    for (unsigned i = kFirstDemoSectionIndex; i <= kPlainDemoSectionCount; ++i)
        html += "<div>Plain section " + std::to_string(i) + ". Swipe up and down.</div>";
    html += "</body></html>";
    return html;
}

static std::string heavyHTML()
{
    std::string html = "<!doctype html><html><head><meta charset='utf-8'><style>body{font:18px 'DejaVu Sans';background:#101820;color:#f2f5f9;margin:24px}h1{font-size:28px}.heavy{margin:18px 0;padding:20px;border-radius:16px;background:linear-gradient(135deg,#1766a8,#20b878 60%,#f2b705);box-shadow:0 8px 24px rgba(0,0,0,.45);border:2px solid rgba(255,255,255,.35)}svg{display:block;width:100%;height:120px;margin:12px 0}img{width:48px;height:48px;margin:4px;vertical-align:middle}.spin{width:48px;height:48px;margin:12px auto;border-radius:50%;border:6px solid rgba(255,255,255,.25);border-top-color:#20b878;animation:spin 2s linear infinite}@keyframes spin{to{transform:rotate(360deg)}}</style></head><body>";
    html += "<h1>Heavy baseline</h1><p>Gradients, radius, shadow, inline images and SVG. No external loads. Spinner moved to about:motion.</p>";
    html += "<div><img src='data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAADAAAAAwCAIAAADYYG7QAAAAU0lEQVR4nO3QsQkAMQwEQRnsvl368xVs6mAGofjYNXPPzDu3//eSbVBQqChUFCoKFYWKQkWholBRqChUFCoKFYWKQkWholBRqChUFCoKFYWKQhM+9sMDFLSjbq4AAAAASUVORK5CYII=' alt='png'/>";
    html += "<img src='data:image/webp;base64,UklGRuAAAABXRUJQVlA4INQAAAAQBwCdASowADAAPmkokEW0IqGhMcgCgA0JaD0cofHCJqj9T2TpWOF4Dky38AVj/kkFwpmKL0Q8KCqM4X3KWBL9MAD+/aR2H/VrlCS0G5f/mUnm5mm4mPb79uFbJv4JPQJL/EA2FvajpCXV26jrJwhP6HzviWA+9lfQW3pDD/8oG9//0yQsWULOXTqJz2ukNEmXlGfEhFlstmnhz0rTr4DkBQgOayGbXkspRulEH2qf/VX3/9MjbeRgl9F+OifI2MGP3iL5zk6nyQIprbf3/R9WERgAAA==' alt='webp'/>";
    html += "<img src='data:image/jpeg;base64,/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAYEBQYFBAYGBQYHBwYIChAKCgkJChQODwwQFxQYGBcUFhYaHSUfGhsjHBYWICwgIyYnKSopGR8tMC0oMCUoKSj/2wBDAQcHBwoIChMKChMoGhYaKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCj/wAARCAAwADADASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwD58gtOnFaEFp04rQgtPatCC09q+0qYk58Fi9jPgtOnFaEFp04rQgtPatCC09q8+piT63BYvYz4LTpxWhBadOK0ILT2q/Bae1cFTEn12Cxexx8Fp04rQgtOnFX4LT2rQgtPaipiT+fcFi9ihBadOK0ILTpxV+C09q0ILT2rgqYk+uwWL2KEFp04rQgtPar8Fp7VoQWntXBUxJ9dgsXscfBadOK0ILTpxV+C09q0ILT2oqYk/n3BYvYoQWnTitCC06cVfgtPatCC09q4KmJPrsFi9jPgtPatCC06cVoQWntWhBae1efUxJ9bgsXsf//Z' alt='jpg'/></div>";
    for (unsigned i = kFirstDemoSectionIndex; i <= kHeavyDemoSectionCount; ++i) {
        html += "<div class='heavy'>Heavy section " + std::to_string(i) + ". Swipe to test GPU raster.</div>";
        html += "<svg viewBox='0 0 300 120'><defs><linearGradient id='g' x1='0' y1='0' x2='1' y2='1'><stop offset='0' stop-color='#1766a8'/><stop offset='1' stop-color='#20b878'/></linearGradient></defs><rect x='4' y='4' width='292' height='112' rx='18' fill='url(#g)'/><circle cx='70' cy='60' r='34' fill='white' opacity='.85'/><rect x='130' y='24' width='140' height='72' rx='12' fill='black' opacity='.35'/></svg>";
    }
    html += "</body></html>";
    return html;
}

static std::string demoHTML(bool second)
{
    std::string html = "<!doctype html><html><head><meta charset='utf-8'><style>body{font:18px 'DejaVu Sans';background:#edf3fa;color:#14243a;margin:24px}h1{color:#1766a8;font-size:28px}button{font:20px 'DejaVu Sans';padding:16px;background:#20b878;color:#102d21;border:0;border-radius:8px}.card{padding:20px;background:white;margin:18px 0;border-radius:12px}footer{margin:30px 0}</style></head><body>";
    html += second ? "<h1>Second local page</h1><p>Use Back to return to the first page.</p>" : "<h1>WebKitWebViewUWP</h1><p>Persistent WebCore page inside a XAML SwapChainPanel.</p>";
    html += "<div style='position:sticky;top:0;background:#1766a8;color:white;padding:8px;z-index:2'>Sticky layer: stays at the top while scrolling</div><div style='position:fixed;right:6px;bottom:6px;background:#14243a;color:white;padding:6px;font-size:12px;z-index:3'>Fixed layer</div>";
    html += "<button onclick=\"this.textContent='Clicked '+(++window.clickCount)+' times'\">Tap this button</button><script>window.clickCount=0;</script>";
    for (unsigned i = kFirstDemoSectionIndex; i <= kScrollableDemoSectionCount; ++i)
        html += "<div class='card'>Scroll section " + std::to_string(i) + ". Swipe up and down to test persistent-page scrolling.</div>";
    html += "<footer>End of page. Try " + std::string(kAboutSecondHtmlLabel) + " in the address box, then Back and Reload.</footer></body></html>";
    return html;
}

// Interactive test shell hosting the native wrapper with toolbar navigation,
// fetch probe, blocked page and the temporary component diagnostic.
class MiniBrowserApp final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, Xaml::IApplicationOverrides, ComposableBase<>> {
    InspectableClass(L"WebKitWebView.MiniBrowser.App", BaseTrust);

public:
    // Releases the shell; engine teardown happens in the wrapper.
    ~MiniBrowserApp() { }

    // Composes the XAML application base and initializes the fetch lock.
    HRESULT RuntimeClassInitialize()
    {
        InitializeSRWLock(&m_fetchLock);
        InitializeSRWLock(&m_openLock);
        m_fetchInFlight = false;
        m_fetchReady = false;
        m_fetchGeneration = 0;
        ComPtr<Xaml::IApplicationFactory> factory;
        ComPtr<IInspectable> inner;
        ComPtr<Xaml::IApplication> application;
        HRESULT hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_UI_Xaml_Application).Get(), IID_PPV_ARGS(&factory));
        if (FAILED(hr) || FAILED(hr = factory->CreateInstance(this, &inner, &application)))
            return hr;
        m_application = application;
        return SetComposableBasePointers(inner.Get());
    }

    // XAML application lifecycle; launches open the window, other activations are ignored.
    HRESULT STDMETHODCALLTYPE OnLaunched(Activation::ILaunchActivatedEventArgs*) override { return openWindow(); }

    // Restores or creates the window for a generic activation.
    HRESULT STDMETHODCALLTYPE OnActivated(Activation::IActivatedEventArgs*) override { return openWindow(); }

    // File activation is not handled by the test shell.
    HRESULT STDMETHODCALLTYPE OnFileActivated(Activation::IFileActivatedEventArgs*) override { return S_OK; }

    // Search activation is not handled by the test shell.
    HRESULT STDMETHODCALLTYPE OnSearchActivated(Activation::ISearchActivatedEventArgs*) override { return S_OK; }

    // Share-target activation is not handled by the test shell.
    HRESULT STDMETHODCALLTYPE OnShareTargetActivated(Activation::IShareTargetActivatedEventArgs*) override { return S_OK; }

    // File-open picker activation is not handled by the test shell.
    HRESULT STDMETHODCALLTYPE OnFileOpenPickerActivated(Activation::IFileOpenPickerActivatedEventArgs*) override { return S_OK; }

    // File-save picker activation is not handled by the test shell.
    HRESULT STDMETHODCALLTYPE OnFileSavePickerActivated(Activation::IFileSavePickerActivatedEventArgs*) override { return S_OK; }

    // Cached-file updater activation is not handled by the test shell.
    HRESULT STDMETHODCALLTYPE OnCachedFileUpdaterActivated(Activation::ICachedFileUpdaterActivatedEventArgs*) override { return S_OK; }

    // No additional action is needed when XAML creates the window.
    HRESULT STDMETHODCALLTYPE OnWindowCreated(Xaml::IWindowCreatedEventArgs*) override { return S_OK; }

private:
    // Finds a named XAML element under the window root.
    HRESULT find(const wchar_t* name, IInspectable** object) { return m_root->FindName(HStringReference(name).Get(), object); }

    // Shows status text and logs the result for the device log.
    void status(const wchar_t* text, HRESULT result = S_OK)
    {
        if (m_status)
            m_status->put_Text(HStringReference(text).Get());
        logResult("minibrowser-status", result);
    }

    // Remote fetch probe via packaged libcurl/OpenSSL on a Win32 worker thread.
    // No COM or XAML in the worker; results are consumed on the UI Rendering
    // tick. Windows.Web.Http is deliberately not used: it crashes inside the
    // system HTTP/twinapi stack on this device (dumps 7956/5976/2520).
    // Outbound fetch parameters handed to the worker thread.
    struct FetchRequest {
        // Owning shell receiving the result on the UI tick.
        MiniBrowserApp* app;

        // UTF-8 URL passed to curl.
        std::string url;

        // UTF-8 CA bundle path for certificate verification.
        std::string caPath;

        // Original address-bar text for history and display.
        std::wstring uriW;

        // Whether a history entry is recorded on success.
        bool addHistory;

        // Supersede counter; stale worker results are dropped.
        unsigned generation;
    };

    // Worker result consumed on the UI Rendering tick.
    struct FetchResult {
        // Transport completed without a curl error.
        bool transportOk { false };

        // HTTP status when the transport completed.
        long httpCode { 0 };

        // Raw curl result for failure attribution.
        int curlCode { 0 };

        // Body exceeded the probe cap and was aborted.
        bool tooLarge { false };

        // Downloaded bytes, if any.
        std::string body;

        // Original address-bar text for history and display.
        std::wstring uriW;

        // Whether a history entry is recorded on success.
        bool addHistory { false };

        // Generation at dispatch; stale results are dropped.
        unsigned generation { 0 };
    };

    // Appends a curl data chunk; returns short to abort past the probe cap.
    static size_t fetchWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
    {
        auto* body = static_cast<std::string*>(userdata);
        size_t count = size * nmemb;
        // Probe downloads are diagnostic only; never buffer a full page here.
        constexpr size_t kFetchProbeCapBytes = 512 * 1024;
        if (body->size() + count > kFetchProbeCapBytes)
            return kFetchAbortCallbackResult; // abort: over probe cap
        body->append(ptr, count);
        return count;
    }

    // One-time curl global init shared by all worker threads.
    static INIT_ONCE s_curlInitOnce;

    // Runs curl_global_init once; runs on a worker thread.
    static BOOL CALLBACK curlInitOnceCallback(PINIT_ONCE, PVOID, PVOID*)
    {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        return TRUE;
    }

    // Secure default. A one-off insecure diagnostic (1.0.0.29) proved the
    // libcrypto LH_insert crash is NOT in the X509_STORE path (it faults
    // identically with verification off), so verification stays ON.
    static constexpr bool kFetchInsecureDiagnostic = false;
    static DWORD WINAPI fetchThreadProc(LPVOID lpParam)
    {
        auto* request = static_cast<FetchRequest*>(lpParam);
        FetchResult result;
        result.uriW = request->uriW;
        result.addHistory = request->addHistory;
        result.generation = request->generation;
        InitOnceExecuteOnce(&s_curlInitOnce, curlInitOnceCallback, nullptr, nullptr);
        if (CURL* handle = curl_easy_init()) {
            curl_easy_setopt(handle, CURLOPT_URL, request->url.c_str());
            curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 5L);
            curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 10L);
            curl_easy_setopt(handle, CURLOPT_TIMEOUT, 25L);
            curl_easy_setopt(handle, CURLOPT_USERAGENT, "WebKitWebViewUWP MiniBrowser");
            curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
            if (kFetchInsecureDiagnostic) {
                curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
                curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0L);
                logResult("minibrowser-fetch-insecure-diagnostic", 1);
            } else {
                curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
                curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
                curl_easy_setopt(handle, CURLOPT_CAINFO, request->caPath.c_str());
            }
            curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, fetchWriteCallback);
            curl_easy_setopt(handle, CURLOPT_WRITEDATA, &result.body);
            CURLcode code = curl_easy_perform(handle);
            result.curlCode = static_cast<int>(code);
            if (code == CURLE_OK) {
                result.transportOk = true;
                curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &result.httpCode);
            } else if (code == CURLE_WRITE_ERROR) {
                result.tooLarge = true;
            }
            logResult("minibrowser-fetch-curl-code", static_cast<unsigned>(code));
            logResult("minibrowser-fetch-bytes", static_cast<unsigned>(result.body.size()));
            logResult("minibrowser-fetch-http-code", static_cast<unsigned>(result.httpCode > 0 ? result.httpCode : kNoHttpStatusCode));
            curl_easy_cleanup(handle);
        } else {
            logResult("minibrowser-fetch-easy-init", E_FAIL);
        }
        AcquireSRWLockExclusive(&request->app->m_fetchLock);
        request->app->m_fetchResult = std::move(result);
        request->app->m_fetchReady = true;
        ReleaseSRWLockExclusive(&request->app->m_fetchLock);
        delete request;
        return kWorkerThreadSuccess;
    }

    // Blocked-page template override. Empty clears back to the default
    // lookup below. Lets a host app restyle the page without rebuilding.
    void setBlockedPageTemplate(const wchar_t* path)
    {
        m_blockedTemplateOverride.assign(path ? path : L"");
        logResult("minibrowser-blocked-template-override", path ? WKPageLogTrueUWP : WKPageLogFalseUWP);
    }

    // Reads a whole UTF-8 file; empty on any failure.
    static std::string readUtf8File(const wchar_t* path)
    {
        std::string content;
        HANDLE file = CreateFile2(path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return content;
        char chunk[4096];
        DWORD got = 0;
        while (ReadFile(file, chunk, sizeof(chunk), &got, nullptr) && got)
            content.append(chunk, got);
        CloseHandle(file);
        return content;
    }

    // Replaces every {{TOKEN}} with value.
    static void fillToken(std::string& page, const char* token, const std::string& value)
    {
        std::string key = std::string("{{") + token + "}}";
        for (size_t at = page.find(key); at != std::string::npos; at = page.find(key, at + value.size()))
            page.replace(at, key.size(), value);
    }

    // Resolves explicit path, LocalState override, packaged file, then fallback.
    // Applications can customize their warning page by writing LocalState/blocked.html.
    std::string loadBlockedTemplate()
    {
        // Fail-safe skeleton with the same tokens; renders only when neither
        // the override nor the packaged template is readable.
        static const char kFallback[] = "<!doctype html><html><head><meta charset='utf-8'></head><body>"
                                        "<h1>{{TITLE}}</h1><p>{{INTRO}}</p><p>{{URL}}</p>"
                                        "<p>{{REASON}}</p><p>{{TECHNICAL}}</p>{{PROCEED}}</body></html>";
        if (!m_blockedTemplateOverride.empty()) {
            std::string page = readUtf8File(m_blockedTemplateOverride.c_str());
            if (!page.empty()) {
                logResult("minibrowser-blocked-template", BlockedTemplateSourceExplicitPath);
                return page;
            }
        }

        // A host-provided LocalState file overrides the package's default design.
        std::wstring localState = logPath;
        size_t slash = localState.find_last_of(L"\\/");
        localState = (slash == std::wstring::npos ? L"" : localState.substr(0, slash)) + L"\\" + kBlockedTemplateFilename;
        std::string page = readUtf8File(localState.c_str());
        if (!page.empty()) {
            logResult("minibrowser-blocked-template", BlockedTemplateSourceLocalStateFile);
            return page;
        }

        std::wstring packaged = m_packagePath + L"\\" + kBlockedTemplateFilename;
        page = readUtf8File(packaged.c_str());
        if (!page.empty()) {
            logResult("minibrowser-blocked-template", BlockedTemplateSourcePackagedFile);
            return page;
        }
        logResult("minibrowser-blocked-template", BlockedTemplateSourceEmbeddedFallback);
        return kFallback;
    }

    // TEMPORARY blocked-template verification; remove before acceptance.
    // After the fixture suite frees the view: first a refused connection with
    // the packaged template, then the same failure with a LocalState override
    // template. A closed port on the reachable fixture PC refuses fast and
    // deterministically; loopback is off-limits (UWP loopback isolation
    // blocks 127.0.0.1 with a policy error before any transport runs).
    void tickBlockedTemplateTest()
    {
        if (!s_suiteDone || m_blockedTestStage == BlockedTemplateTestFinished)
            return;
        ++m_blockedTestTicks;
        if (m_blockedTestStage == BlockedTemplateTestNotStarted) {
            logResult("minibrowser-blocked-test-start", 0);
            navigate(L"http://192.168.1.69:9/first");
            m_blockedTestStage = BlockedTemplateTestPackagedPagePending;
            m_blockedTestTicks = 0;
        } else if (m_blockedTestStage == BlockedTemplateTestPackagedPagePending && m_interstitialCount >= kFirstBlockedTemplateTestCount) {
            // Packaged template rendered; arm the LocalState override.
            std::wstring dir = logPath;
            size_t slash = dir.find_last_of(L"\\/");
            std::wstring path = (slash == std::wstring::npos ? L"" : dir.substr(0, slash)) + L"\\" + kBlockedTemplateOverrideFilename;
            const char* alt = "<!doctype html><html><head><meta charset='utf-8'></head><body>"
                              "<h1>OVERRIDE {{TITLE}}</h1><p>{{INTRO}}</p><p>{{URL}}</p>"
                              "<p>{{REASON}}</p><p>{{TECHNICAL}}</p>{{PROCEED}}</body></html>";
            HANDLE file = CreateFile2(path.c_str(), GENERIC_WRITE, 0, CREATE_ALWAYS, nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                DWORD written = 0;
                WriteFile(file, alt, static_cast<DWORD>(strlen(alt)), &written, nullptr);
                CloseHandle(file);
            }
            setBlockedPageTemplate(path.c_str());
            navigate(L"http://192.168.1.69:9/second");
            m_blockedTestStage = BlockedTemplateTestOverridePagePending;
            m_blockedTestTicks = 0;
        } else if (m_blockedTestStage == BlockedTemplateTestOverridePagePending
            && (m_interstitialCount >= kExpectedBlockedTemplateTestCount || m_blockedTestTicks > kBlockedTemplateTestMaxTicks)) {
            // 1800 Rendering ticks is ~30 s at 60 Hz: long past a refused
            // connection, so stop waiting even if the override page never showed.
            setBlockedPageTemplate(nullptr);
            logResult("minibrowser-blocked-test-done", m_interstitialCount);
            m_blockedTestStage = BlockedTemplateTestFinished;
        }
    }

    // Application-layer blocked-navigation page. The engine already refused
    // the load (no commit, no retry); this only explains the failure that the
    // log recorded. Only certificate failures offer the consented proceed
    // control (a host-scoped, page-session exception); every other failure
    // just explains itself. Toolbar Back still works, because neither the
    // failed navigation nor this page creates a history entry.
    void showBlockedPage(const std::string& url, unsigned long code)
    {
        // Transport failures carry the curl result verbatim: WebKit's shared
        // curl port builds the ResourceError from the CURLcode, so the names
        // below come straight from <curl/curl.h>. Adapter-local codes (1
        // cancelled, 2 blocked, ...) fall through to the generic text.
        CURLcode curlCode = static_cast<CURLcode>(code);
        std::string title, intro, reason, technical, proceed;
        const wchar_t* statusText;
        if (curlCode == CURLE_PEER_FAILED_VERIFICATION) {
            title = "Connection not private";
            intro = "MiniBrowser could not verify this site, so nothing was loaded.";
            reason = "The site's certificate could not be verified. It may be expired, self-signed, or issued by an authority this phone does not trust.";
            technical = "Certificate verification failed (CURLE_PEER_FAILED_VERIFICATION).";
            statusText = L"Blocked: the site's certificate could not be verified.";
            proceed = "<p><a href=\"minibrowser:proceed\">Proceed to " + escapeHtml(urlHost(url))
                + " anyway (unsafe)</a></p><p>Only do this if you understand the risk: without verification, anyone on the network can read or modify the traffic.</p>";
        } else if (curlCode == CURLE_COULDNT_RESOLVE_HOST) {
            title = "Navigation failed";
            intro = "MiniBrowser could not load this page, so nothing was loaded.";
            reason = "The site's address could not be resolved.";
            technical = "Host resolution failed (CURLE_COULDNT_RESOLVE_HOST).";
            statusText = L"Navigation failed: host not found.";
        } else if (curlCode == CURLE_COULDNT_CONNECT) {
            title = "Navigation failed";
            intro = "MiniBrowser could not load this page, so nothing was loaded.";
            reason = "The phone could not connect to the site.";
            technical = "Connection failed (CURLE_COULDNT_CONNECT).";
            statusText = L"Navigation failed: connection failed.";
        } else if (curlCode == CURLE_OPERATION_TIMEDOUT) {
            title = "Navigation failed";
            intro = "MiniBrowser could not load this page, so nothing was loaded.";
            reason = "The connection timed out.";
            technical = "Operation timed out (CURLE_OPERATION_TIMEDOUT).";
            statusText = L"Navigation failed: timed out.";
        } else {
            char generic[64];
            sprintf_s(generic, "The page failed to load (error %lu).", code);
            title = "Navigation failed";
            intro = "MiniBrowser could not load this page, so nothing was loaded.";
            reason = generic;
            technical = reason;
            statusText = L"Navigation failed; see minibrowser.log.";
        }

        // Values below are page markup; everything interpolated into them is
        // escaped at the call site. Unknown tokens stay untouched so a custom
        // template visibly shows what it forgot.
        std::string page = loadBlockedTemplate();
        fillToken(page, "TITLE", title);
        fillToken(page, "INTRO", intro);
        fillToken(page, "URL", escapeHtml(url));
        fillToken(page, "REASON", escapeHtml(reason));
        fillToken(page, "TECHNICAL", escapeHtml(technical));
        fillToken(page, "PROCEED", proceed);
        HRESULT hr = m_view->loadHTML(page.c_str());
        if (FAILED(hr)) {
            status(L"Navigation failed; see minibrowser.log.", hr);
            return;
        }
        status(statusText, S_OK);
        ++m_interstitialCount; // TEMPORARY test sequencing; remove before acceptance.
        logResult("minibrowser-interstitial-shown", code);
    }

    // Engine reports failing URLs in normalized form (empty path becomes "/"),
    // while the typed URL may omit the trailing slash. Treat those as equal.
    static bool sameNavUrl(const std::string& a, const std::string& b)
    {
        if (a == b)
            return true;
        if (a.size() + kTrailingSlashCharacterCount == b.size() && b.back() == '/' && b.compare(kUrlPrefixSearchStart, a.size(), a) == kCStringComparisonEqual)
            return true;
        if (b.size() + kTrailingSlashCharacterCount == a.size() && a.back() == '/' && a.compare(kUrlPrefixSearchStart, b.size(), b) == kCStringComparisonEqual)
            return true;
        return false;
    }

    // UI tick pump: diagnostics, failure display, native sampling and fetch completion.
    void consumeFetchResult()
    {
        // UI thread only, from the Rendering tick.
        tickComponentDiagnostic();
        tickBlockedTemplateTest(); // TEMPORARY; remove before acceptance.
        if (!s_failedNavUrl.empty()) {
            // A main-frame navigation failed in the engine. Show the blocked
            // page only if it matches the latest navigation: anything else
            // means the user already moved on (a new navigate() clears the
            // stash). Suite startup URLs opt out.
            std::string url = std::move(s_failedNavUrl);
            s_failedNavUrl.clear();
            unsigned long code = s_failedNavCode;
            if (!m_lastNavUrl.empty() && sameNavUrl(m_lastNavUrl, url)
                && url.find("uwp-suite=1") == std::string::npos)
                showBlockedPage(url, code);
            else if (m_lastNavUrl.empty() || !sameNavUrl(m_lastNavUrl, url))
                logResult("minibrowser-stash-suppressed", code); // TEMPORARY race diagnosis; remove before acceptance.
        }
        if (m_nativeNavPending) {
            // An empty document can stop producing frames, so sample early and often.
            ++m_nativeNavTicks;
            if (m_nativeNavTicks <= kNativeNavigationSampleCount || (m_nativeNavTicks % kNativeNavigationSampleInterval) == kNativeNavigationSampleBoundary) {
                WKPageStateUWP state;
                if (SUCCEEDED(m_view->pageState(state))) {
                    logResult("minibrowser-native-content-height", state.contentHeight);
                    logResult("minibrowser-native-scroll-y", state.scrollY);
                }
            }
            if (m_nativeNavTicks > kNativeNavigationMaxTicks)
                m_nativeNavPending = false;
        }
        FetchResult result;
        AcquireSRWLockExclusive(&m_fetchLock);
        if (!m_fetchReady) {
            ReleaseSRWLockExclusive(&m_fetchLock);
            return;
        }
        result = std::move(m_fetchResult);
        m_fetchResult = FetchResult();
        m_fetchReady = false;
        m_fetchInFlight = false;
        ReleaseSRWLockExclusive(&m_fetchLock);
        if (result.generation != m_fetchGeneration)
            return; // superseded
        if (!result.transportOk) {
            status(L"Fetch failed; see minibrowser.log.", E_FAIL);
            return;
        }
        if (result.tooLarge) {
            status(L"Remote page too large for v0 probe (512KB cap).", E_FAIL);
            return;
        }
        if (result.httpCode < kHttpSuccessfulStatusFirst || result.httpCode >= kHttpSuccessfulStatusPastLast || result.body.empty()) {
            wchar_t message[128];
            swprintf_s(message, L"Remote fetch HTTP %ld.", result.httpCode);
            status(message, E_FAIL);
            return;
        }
        HRESULT hr = m_view->loadHTML(result.body.c_str());
        if (FAILED(hr)) {
            status(L"Remote HTML load failed; see log.", hr);
            return;
        }
        m_address->put_Text(HStringReference(result.uriW.c_str()).Get());
        status(L"Remote main HTML loaded. Subresources (CSS/JS/images) still pending.");
        WKPageStateUWP state;
        if (SUCCEEDED(m_view->pageState(state))) {
            logResult("minibrowser-content-height", state.contentHeight);
            logResult("minibrowser-scroll-y", state.scrollY);
        }
    }

    // Starts a worker-thread fetch of main HTML only; subresources stay pending.
    HRESULT fetchRemote(const std::wstring& uri, bool addHistory)
    {
        AcquireSRWLockExclusive(&m_fetchLock);
        if (m_fetchInFlight) {
            ReleaseSRWLockExclusive(&m_fetchLock);
            status(L"Fetch already in progress.", HRESULT_FROM_WIN32(ERROR_BUSY));
            return S_OK;
        }
        m_fetchInFlight = true;
        ++m_fetchGeneration;
        unsigned generation = m_fetchGeneration;
        ReleaseSRWLockExclusive(&m_fetchLock);
        status(L"Fetching remote page (main HTML only; subresources pending)...", S_OK);
        logResult("minibrowser-fetch-start", 0);
        int urlLength = WideCharToMultiByte(CP_UTF8, kUtf8ConversionFlags, uri.c_str(), kNullTerminatedInputLength, nullptr, kConversionSizeQueryOnly, nullptr, nullptr);
        std::wstring caPathW = m_packagePath + L"\\cacert.pem";
        int caLength = WideCharToMultiByte(CP_UTF8, kUtf8ConversionFlags, caPathW.c_str(), kNullTerminatedInputLength, nullptr, kConversionSizeQueryOnly, nullptr, nullptr);
        if (urlLength <= kNullTerminatorCharacterCount || caLength <= kNullTerminatorCharacterCount) {
            AcquireSRWLockExclusive(&m_fetchLock);
            m_fetchInFlight = false;
            ReleaseSRWLockExclusive(&m_fetchLock);
            status(L"Fetch failed: bad URL or install path.", E_INVALIDARG);
            return S_OK;
        }
        auto* request = new (std::nothrow) FetchRequest();
        if (!request) {
            AcquireSRWLockExclusive(&m_fetchLock);
            m_fetchInFlight = false;
            ReleaseSRWLockExclusive(&m_fetchLock);
            return E_OUTOFMEMORY;
        }
        request->app = this;
        request->url.assign(urlLength, '\0');
        int convertedUrlLength = WideCharToMultiByte(CP_UTF8, kUtf8ConversionFlags, uri.c_str(), kNullTerminatedInputLength, request->url.data(), urlLength, nullptr, nullptr);
        request->caPath.assign(caLength, '\0');
        int convertedCaLength = WideCharToMultiByte(CP_UTF8, kUtf8ConversionFlags, caPathW.c_str(), kNullTerminatedInputLength, request->caPath.data(), caLength, nullptr, nullptr);
        if (convertedUrlLength != urlLength || convertedCaLength != caLength) {
            delete request;
            AcquireSRWLockExclusive(&m_fetchLock);
            m_fetchInFlight = false;
            ReleaseSRWLockExclusive(&m_fetchLock);
            status(L"Fetch failed: UTF-8 conversion failed.", E_INVALIDARG);
            return S_OK;
        }
        request->url.resize(convertedUrlLength - kNullTerminatorCharacterCount);
        request->caPath.resize(convertedCaLength - kNullTerminatorCharacterCount);
        request->uriW = uri;
        request->addHistory = addHistory;
        request->generation = generation;
        if (GetFileAttributesW(caPathW.c_str()) == INVALID_FILE_ATTRIBUTES) {
            logResult("minibrowser-fetch-no-ca", E_FAIL);
            delete request;
            AcquireSRWLockExclusive(&m_fetchLock);
            m_fetchInFlight = false;
            ReleaseSRWLockExclusive(&m_fetchLock);
            status(L"Fetch failed: CA bundle missing from package.", E_FAIL);
            return S_OK;
        }
        HANDLE thread = CreateThread(nullptr, 0, fetchThreadProc, request, 0, nullptr);
        if (!thread) {
            HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
            logResult("minibrowser-fetch-thread", hr);
            delete request;
            AcquireSRWLockExclusive(&m_fetchLock);
            m_fetchInFlight = false;
            ReleaseSRWLockExclusive(&m_fetchLock);
            status(L"Fetch failed to start worker.", hr);
            return S_OK;
        }
        CloseHandle(thread);
        return S_OK;
    }

    // Navigates the engine to a remote URL or loads a local about: page.
    HRESULT navigate(const std::wstring& uri)
    {
        if (uri.rfind(L"http://", kUrlPrefixSearchStart) == kUrlPrefixSearchStart || uri.rfind(L"https://", kUrlPrefixSearchStart) == kUrlPrefixSearchStart) {
            status(L"Navigating with WebCore loader...", S_OK);
            logResult("minibrowser-navigate-remote-start", 0);
            int urlLength = WideCharToMultiByte(CP_UTF8, kUtf8ConversionFlags, uri.c_str(), kNullTerminatedInputLength, nullptr, kConversionSizeQueryOnly, nullptr, nullptr);
            std::wstring caPathW = m_packagePath + L"\\cacert.pem";
            if (urlLength <= kNullTerminatorCharacterCount) {
                status(L"Navigation failed: bad URL.", E_INVALIDARG);
                return S_OK;
            }
            std::string url(urlLength, '\0');
            int convertedUrlLength = WideCharToMultiByte(CP_UTF8, kUtf8ConversionFlags, uri.c_str(), kNullTerminatedInputLength, url.data(), urlLength, nullptr, nullptr);
            if (convertedUrlLength != urlLength) {
                status(L"Navigation failed: bad URL.", HRESULT_FROM_WIN32(GetLastError()));
                return S_OK;
            }
            url.resize(convertedUrlLength - kNullTerminatorCharacterCount);
            if (GetFileAttributesW(caPathW.c_str()) == INVALID_FILE_ATTRIBUTES) {
                logResult("minibrowser-fetch-no-ca", E_FAIL);
                status(L"Navigation failed: CA bundle missing from package.", E_FAIL);
                return S_OK;
            }
            HRESULT hr = S_OK;
            // Publish the target before starting the engine: a synchronous
            // failure (or a nested tick) must already see the new navigation,
            // otherwise the failure display below drops it as stale.
            m_lastNavUrl = url;
            s_failedNavUrl.clear(); // a new navigation supersedes any pending failure display
            hr = m_view->navigateRemote(url.c_str(), caPathW.c_str());
            if (FAILED(hr)) {
                status(L"Navigation failed to start; see minibrowser.log", hr);
                return hr;
            }
            logResult("minibrowser-native-nav-armed", 0);
            m_nativeNavPending = true;
            m_nativeNavTicks = 0;
            m_address->put_Text(HStringReference(uri.c_str()).Get());
            status(L"Navigation started; see minibrowser.log.");
            return S_OK;
        }
        if (uri != kAboutDemoURL && uri != kAboutSecondURL && uri != kAboutPlainURL && uri != kAboutHeavyURL) {
            status(L"Unknown local page. Try about:demo, about:plain, about:heavy, or an http(s) URL.", E_INVALIDARG);
            return S_OK;
        }
        AcquireSRWLockExclusive(&m_fetchLock);
        ++m_fetchGeneration;
        m_fetchReady = false;
        m_fetchInFlight = false;
        ReleaseSRWLockExclusive(&m_fetchLock);
        auto html = uri == kAboutPlainURL ? plainHTML() : uri == kAboutHeavyURL ? heavyHTML()
                                                                                : demoHTML(uri == kAboutSecondURL);
        HRESULT hr = m_view->loadHTML(html.c_str());
        if (FAILED(hr)) {
            status(L"Page load failed; see minibrowser.log", hr);
            return hr;
        }
        m_lastNavUrl.clear(); // app-supplied HTML cannot fail this way; never show the blocked page for it
        m_address->put_Text(HStringReference(uri.c_str()).Get());
        status(L"Local page loaded. Tap button, swipe to scroll.");
        WKPageStateUWP state;
        if (SUCCEEDED(m_view->pageState(state))) {
            logResult("minibrowser-content-height", state.contentHeight);
            logResult("minibrowser-scroll-y", state.scrollY);
        }
        return S_OK;
    }

    // Builds the window, toolbar and browser host on first launch; reactivates after.
    HRESULT openWindow()
    {
        // Launch and generic activation race on first run; without this lock both
        // threads create engines/windows and the loser trips the process owner gate.
        struct OpenWindowGuard {
            // Locks first-time creation; released on every exit path below.
            SRWLOCK* lock;
            // Acquire the exclusive creation lock before touching shared shell state.
            explicit OpenWindowGuard(SRWLOCK* value) : lock(value) { AcquireSRWLockExclusive(lock); }
            // Release the creation lock so a peer activates the created window.
            ~OpenWindowGuard() { ReleaseSRWLockExclusive(lock); }
        };
        OpenWindowGuard openGuard(&m_openLock);
        logResult("minibrowser-launched", 0);
        if (m_window)
            return m_window->Activate();
        ComPtr<Xaml::IWindowStatics> windows;
        HRESULT hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_UI_Xaml_Window).Get(), IID_PPV_ARGS(&windows));
        if (FAILED(hr) || FAILED(hr = windows->get_Current(&m_window)))
            return hr;
        // Scale content above the opaque system bars like other browsers: lay
        // out inside the visible bounds before any content exists, and keep the
        // navigation bar visible instead of a transparent overlay. No fullscreen.
        {
            namespace ViewManagement = ABI::Windows::UI::ViewManagement;
            ComPtr<ViewManagement::IApplicationViewStatics2> viewStatics;
            ComPtr<ViewManagement::IApplicationView> view;
            ComPtr<ViewManagement::IApplicationView2> view2;
            boolean usingVisible = false;
            if (SUCCEEDED(RoGetActivationFactory(HStringReference(RuntimeClass_Windows_UI_ViewManagement_ApplicationView).Get(), IID_PPV_ARGS(&viewStatics)))
                && SUCCEEDED(viewStatics->GetForCurrentView(&view))
                && SUCCEEDED(view.As(&view2))
                && SUCCEEDED(view2->SetDesiredBoundsMode(ViewManagement::ApplicationViewBoundsMode_UseVisible, &usingVisible))) {
                logResult("minibrowser-desired-bounds-visible", usingVisible ? 1UL : 0UL);
                // Keep the view alive for the process lifetime; the mode persists.
                // VisibleBoundsChanged subscription is omitted: its typed delegate
                // has no native MIDL specialization, and panel SizeChanged already
                // resizes the engine on every bounds change.
                m_appView = view2;
            } else
                logResult("minibrowser-desired-bounds-setup", static_cast<unsigned long>(E_FAIL));
        }
        ComPtr<ABI::Windows::UI::Xaml::Markup::IXamlReaderStatics> reader;
        ComPtr<IInspectable> root;
        hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_UI_Xaml_Markup_XamlReader).Get(), IID_PPV_ARGS(&reader));
        const wchar_t* markup = LR"(<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Background="#EDF3FA" RequestedTheme="Light">
<Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="*"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
<StackPanel Orientation="Horizontal"><Button x:Name="Back" Content="Back"/><Button x:Name="Forward" Content="Forward"/><Button x:Name="Reload" Content="Reload"/><TextBox x:Name="Address" Width="140"/><Button x:Name="Go" Content="Go"/><Button x:Name="Capture" Content="Capture"/></StackPanel>
<ContentControl x:Name="Browser" Grid.Row="1" HorizontalContentAlignment="Stretch" VerticalContentAlignment="Stretch"/>
<TextBlock x:Name="Status" Grid.Row="2" TextWrapping="Wrap" Margin="8" Text="Initializing WebKitWebViewUWP..."/>
</Grid>)";
        if (FAILED(hr) || FAILED(hr = reader->Load(HStringReference(markup).Get(), &root))
            || FAILED(hr = root.As(&m_root))) {
            logResult("minibrowser-xaml-root", hr);
            return hr;
        }
        ComPtr<IInspectable> control;
        if (FAILED(hr = find(L"Address", &control)) || FAILED(hr = control.As(&m_address)))
            return hr;
        if (FAILED(hr = m_address->put_Text(HStringReference(kAboutDemoURL).Get())))
            return hr;
        control.Reset();
        if (FAILED(hr = find(L"Status", &control)) || FAILED(hr = control.As(&m_status)))
            return hr;
        ComPtr<ABI::Windows::ApplicationModel::IPackageStatics> packages;
        ComPtr<ABI::Windows::ApplicationModel::IPackage> package;
        ComPtr<Storage::IStorageFolder> folder;
        HString packagePath;
        hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_ApplicationModel_Package).Get(), IID_PPV_ARGS(&packages));
        if (FAILED(hr) || FAILED(hr = packages->get_Current(&package)) || FAILED(hr = package->get_InstalledLocation(&folder))
            || FAILED(hr = folderPath(folder.Get(), packagePath)))
            return hr;
        wchar_t fonts[1024];
        swprintf_s(fonts, L"%ls\\Fonts", packagePath.GetRawBuffer(nullptr));
        {
            UINT32 packageLength = 0;
            const wchar_t* packageRaw = packagePath.GetRawBuffer(&packageLength);
            m_packagePath.assign(packageRaw ? packageRaw : L"", packageLength);
        }
        m_view = std::make_unique<WebKit::WebKitWebViewUWP>();
        hr = m_view->initialize(fonts, logResult);
        logResult("minibrowser-control-initialize", hr);
        if (FAILED(hr)) {
            status(L"Control initialization failed; see minibrowser.log", hr);
            return hr;
        }
        runComponentDiagnostic();
        control.Reset();
        ComPtr<Controls::IContentControl> browser;
        ComPtr<IInspectable> view;
        if (FAILED(hr = find(L"Browser", &control)) || FAILED(hr = control.As(&browser))
            || FAILED(hr = m_view->xamlElement()->QueryInterface(IID_PPV_ARGS(&view)))
            || FAILED(hr = browser->put_Content(view.Get())))
            return hr;
        auto go = Callback<Xaml::IRoutedEventHandler>([this](IInspectable*, Xaml::IRoutedEventArgs*) -> HRESULT {
            HString uri;
            HRESULT hr = m_address->get_Text(uri.GetAddressOf());
            return FAILED(hr) ? hr : navigate(uri.GetRawBuffer(nullptr));
        });
        auto reportHistoryState = [this](const char* stage, HRESULT hr) {
            WKPageStateUWP state;
            if (SUCCEEDED(hr) && SUCCEEDED(m_view->pageState(state))) {
                logResult(stage, 0);
                logResult("minibrowser-history-can-go-back", state.canGoBack);
                logResult("minibrowser-history-can-go-forward", state.canGoForward);
            }
            return hr;
        };
        auto reload = Callback<Xaml::IRoutedEventHandler>([this, reportHistoryState](IInspectable*, Xaml::IRoutedEventArgs*) -> HRESULT {
            return reportHistoryState("minibrowser-history-reload", m_view->reload());
        });
        auto back = Callback<Xaml::IRoutedEventHandler>([this, reportHistoryState](IInspectable*, Xaml::IRoutedEventArgs*) -> HRESULT {
            return reportHistoryState("minibrowser-history-back", m_view->goBack());
        });
        auto forward = Callback<Xaml::IRoutedEventHandler>([this, reportHistoryState](IInspectable*, Xaml::IRoutedEventArgs*) -> HRESULT {
            return reportHistoryState("minibrowser-history-forward", m_view->goForward());
        });
        auto capture = Callback<Xaml::IRoutedEventHandler>([this](IInspectable*, Xaml::IRoutedEventArgs*) -> HRESULT {
            std::wstring filename(logPath);
            filename.resize(filename.find_last_of(L"\\/") + 1);
            filename += L"page.png";
            HRESULT hr = m_view->savePNG(filename.c_str());
            status(SUCCEEDED(hr) ? L"Page image saved to LocalState/page.png" : L"Capture failed; see minibrowser.log", hr);
            return S_OK;
        });
        if (FAILED(hr = hookButton(L"Go", go.Get())) || FAILED(hr = hookButton(L"Reload", reload.Get()))
            || FAILED(hr = hookButton(L"Back", back.Get())) || FAILED(hr = hookButton(L"Forward", forward.Get())) || FAILED(hr = hookButton(L"Capture", capture.Get())))
            return hr;
        // Poll completed remote fetches on the UI thread each frame.
        ComPtr<Media::ICompositionTargetStatics> composition;
        if (FAILED(hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_UI_Xaml_Media_CompositionTarget).Get(), IID_PPV_ARGS(&composition)))) {
            logResult("minibrowser-fetch-poll-target", hr);
            return hr;
        }
        auto fetchTick = Callback<Foundation::IEventHandler<IInspectable*>>([this](IInspectable*, IInspectable*) -> HRESULT { consumeFetchResult(); return S_OK; });
        if (FAILED(hr = composition->add_Rendering(fetchTick.Get(), &m_fetchRenderToken))) {
            logResult("minibrowser-fetch-poll-tick", hr);
            return hr;
        }
        ComPtr<Xaml::IUIElement> element;
        if (FAILED(hr = root.As(&element)) || FAILED(hr = m_window->put_Content(element.Get())))
            return hr;
        // Hide the system status bar (clock/battery/signal) so it never covers
        // the app's top toolbar. Best effort; layout works even if hidden fails.
        {
            namespace ViewManagement = ABI::Windows::UI::ViewManagement;
            ComPtr<ViewManagement::IStatusBarStatics> statusStatics;
            ComPtr<ViewManagement::IStatusBar> statusBar;
            ComPtr<ABI::Windows::Foundation::IAsyncAction> hideOp;
            if (SUCCEEDED(RoGetActivationFactory(HStringReference(RuntimeClass_Windows_UI_ViewManagement_StatusBar).Get(), IID_PPV_ARGS(&statusStatics)))
                && SUCCEEDED(statusStatics->GetForCurrentView(&statusBar)) && statusBar
                && SUCCEEDED(statusBar->HideAsync(&hideOp)) && hideOp)
                logResult("minibrowser-statusbar-hide", 0);
            else
                logResult("minibrowser-statusbar-hide", E_FAIL);
        }
        hr = m_window->Activate();
        // Pause Rendering callbacks across OS suspension without closing the page.
        // The engine keeps its state; resume re-subscribes the single token set.
        {
            auto suspending = Callback<Xaml::ISuspendingEventHandler>([this](IInspectable*, ABI::Windows::ApplicationModel::ISuspendingEventArgs*) -> HRESULT {
                HRESULT hr = m_view ? m_view->setSuspended(true) : S_OK;
                logResult("minibrowser-suspending", hr);
                return S_OK;
            });
            auto resuming = Callback<Foundation::IEventHandler<IInspectable*>>([this](IInspectable*, IInspectable*) -> HRESULT {
                HRESULT hr = m_view ? m_view->setSuspended(false) : S_OK;
                logResult("minibrowser-resumed", hr);
                return S_OK;
            });
            if (FAILED(hr = m_application->add_Suspending(suspending.Get(), &m_suspendingToken))
                || FAILED(hr = m_application->add_Resuming(resuming.Get(), &m_resumingToken))) {
                logResult("minibrowser-suspend-subscribe", hr);
                return hr;
            }
        }
        // Do not call IDisplayRequest::RequestActive here. Holding the display
        // request kept the screen on forever, so the phone never auto-locked
        // while MiniBrowser was in the foreground. Normal UWP apps do not hold
        // it, and the device idle timeout now applies to this app as well.
        logResult("minibrowser-display-request-not-held", S_OK);
        // Temporary M9 default target so device runs exercise the native loader
        // without manual tapping. Restore about:demo when the probe is complete.
        return FAILED(hr) ? hr : navigate(L"http://192.168.1.69:8002/index.html?uwp-suite=1");
    }

    // TEMPORARY Phase-A component verification; remove before acceptance.
    // Activates WebKit.WebKitWebView in-process, drives fixture navigations,
    // and logs every component event for design §8.3 evidence.
    void runComponentDiagnostic()
    {
        ComPtr<IInspectable> component;
        HRESULT hr = RoActivateInstance(HStringReference(L"WebKit.WebKitWebView").Get(), &component);
        logResult("component-activate", hr);
        if (FAILED(hr))
            return;
        hr = component.As(&m_componentView);
        logResult("component-query-view", hr);
        if (FAILED(hr))
            return;
        m_component = component;
        auto logUrl = [](HSTRING value, const char* stage) {
            UINT32 length = kInitialComponentStringLength;
            WindowsGetStringRawBuffer(value, &length);
            logResult(stage, length);
        };
        auto starting = Callback<WebKit::IWebKitNavStartingHandler>(
            [this, logUrl](IInspectable*, WebKit::IWebKitNavigationStartingEventArgs* args) -> HRESULT {
                HString uri;
                UINT64 id = kInitialComponentNavigationId;
                if (FAILED(args->get_Uri(uri.GetAddressOf())))
                    return S_OK;
                args->get_NavigationId(&id);
                logUrl(uri.Get(), "component-starting-url-length");
                logResult("component-starting-id", static_cast<unsigned long>(id));
                if (m_componentVetoNext) {
                    args->put_Cancel(true);
                    logResult("component-starting-vetoed", static_cast<unsigned long>(id));
                }
                return S_OK;
            });
        auto sourceChanged = Callback<WebKit::IWebKitSourceChangedHandler>(
            [logUrl](IInspectable*, WebKit::IWebKitSourceChangedEventArgs* args) -> HRESULT {
                HString uri;
                boolean isNew = false;
                if (FAILED(args->get_Uri(uri.GetAddressOf())))
                    return S_OK;
                args->get_IsNewDocument(&isNew);
                logUrl(uri.Get(), "component-source-url-length");
                logResult("component-source-new-document", isNew);
                return S_OK;
            });
        auto terminal = [](const char* stage, WebKit::IWebKitNavigationEventArgs* args) -> HRESULT {
            HString url;
            UINT64 id = kInitialComponentNavigationId;
            INT32 outcome = kInitialComponentOutcome;
            INT32 code = kNoComponentErrorCode;
            if (FAILED(args->get_Url(url.GetAddressOf())))
                return S_OK;
            args->get_NavigationId(&id);
            args->get_Outcome(reinterpret_cast<WebKit::WebKitNavigationOutcome*>(&outcome));
            args->get_ErrorCode(&code);
            UINT32 length = kInitialComponentStringLength;
            WindowsGetStringRawBuffer(url.Get(), &length);
            logResult(stage, length);
            logResult("component-terminal-id", static_cast<unsigned long>(id));
            logResult("component-terminal-outcome", static_cast<unsigned long>(outcome));
            logResult("component-terminal-code", static_cast<unsigned long>(code));
            return S_OK;
        };
        auto completed = Callback<WebKit::IWebKitNavigationEventHandler>(
            [terminal](IInspectable*, WebKit::IWebKitNavigationEventArgs* args) -> HRESULT {
                return terminal("component-completed-url-length", args);
            });
        auto failed = Callback<WebKit::IWebKitNavigationEventHandler>(
            [terminal](IInspectable*, WebKit::IWebKitNavigationEventArgs* args) -> HRESULT {
                return terminal("component-failed-url-length", args);
            });
        auto history = Callback<WebKit::IWebKitNavigationEventHandler>(
            [terminal](IInspectable*, WebKit::IWebKitNavigationEventArgs* args) -> HRESULT {
                return terminal("component-history-url-length", args);
            });
        EventRegistrationToken token;
        hr = m_componentView->add_NavigationStarting(starting.Get(), &token);
        if (FAILED(hr)) {
            logResult("component-subscribe-starting", hr);
            return;
        }
        hr = m_componentView->add_SourceChanged(sourceChanged.Get(), &token);
        if (FAILED(hr)) {
            logResult("component-subscribe-source", hr);
            return;
        }
        hr = m_componentView->add_NavigationCompleted(completed.Get(), &token);
        if (FAILED(hr)) {
            logResult("component-subscribe-completed", hr);
            return;
        }
        hr = m_componentView->add_NavigationFailed(failed.Get(), &token);
        if (FAILED(hr)) {
            logResult("component-subscribe-failed", hr);
            return;
        }
        hr = m_componentView->add_HistoryChanged(history.Get(), &token);
        if (FAILED(hr)) {
            logResult("component-subscribe-history", hr);
            return;
        }
        logResult("component-subscribed", 0);
        hr = m_componentView->EnsureInitializedAsync(nullptr);
        logResult("component-ensure-initialized", hr);
        if (FAILED(hr))
            return;
        HString index;
        index.Set(HStringReference(kComponentFixtureIndexURL).Get());
        hr = m_componentView->put_Source(index.Get());
        logResult("component-navigate-index", hr);
        m_componentStage = ComponentDiagnosticIndexPending;
    }

    // Advances the staged diagnostic scenario on the UI tick.
    void tickComponentDiagnostic()
    {
        if (!m_componentView || m_componentStage == ComponentDiagnosticNotStarted || m_componentStage == ComponentDiagnosticFinished)
            return;
        ++m_componentTicks;
        if (m_componentStage == ComponentDiagnosticIndexPending && m_componentTicks > kComponentSecondNavigationTick) {
            HString second;
            second.Set(HStringReference(kComponentFixtureSecondURL).Get());
            logResult("component-navigate-second", m_componentView->put_Source(second.Get()));
            m_componentStage = ComponentDiagnosticSecondPending;
        } else if (m_componentStage == ComponentDiagnosticSecondPending && m_componentTicks > kComponentBackNavigationTick) {
            logResult("component-go-back", m_componentView->GoBack());
            m_componentStage = ComponentDiagnosticBackPending;
        } else if (m_componentStage == ComponentDiagnosticBackPending && m_componentTicks > kComponentVetoNavigationTick) {
            m_componentVetoNext = true;
            HString second;
            second.Set(HStringReference(kComponentFixtureSecondURL).Get());
            logResult("component-navigate-vetoed", m_componentView->put_Source(second.Get()));
            m_componentStage = ComponentDiagnosticVetoPending;
        } else if (m_componentStage == ComponentDiagnosticVetoPending && m_componentTicks > kComponentDiagnosticFinishTick) {
            m_componentVetoNext = false;
            logResult("component-diagnostic-done", 0);
            m_componentStage = ComponentDiagnosticFinished;
        }
    }

    // Subscribes a Click handler to the named toolbar button.
    HRESULT hookButton(const wchar_t* name, Xaml::IRoutedEventHandler* handler)
    {
        ComPtr<IInspectable> object;
        ComPtr<Controls::Primitives::IButtonBase> button;
        HRESULT hr = find(name, &object);
        if (FAILED(hr) || FAILED(hr = object.As(&button)))
            return hr;
        EventRegistrationToken token;
        return button->add_Click(handler, &token);
    }

    // XAML window hosting the toolbar and browser area.
    ComPtr<Xaml::IWindow> m_window;

    // Root Grid used by name lookup.
    ComPtr<Xaml::IFrameworkElement> m_root;

    // Address-bar input element.
    ComPtr<Controls::ITextBox> m_address;

    // Status-line output element.
    ComPtr<Controls::ITextBlock> m_status;

    // TEMPORARY: component activation identity retained during event testing.
    ComPtr<IInspectable> m_component;

    // TEMPORARY: activated component's navigation contract.
    ComPtr<WebKit::IWebKitWebView> m_componentView;

    // TEMPORARY: current step in the staged component event scenario.
    ComponentDiagnosticStage m_componentStage { ComponentDiagnosticNotStarted };

    // TEMPORARY: Rendering callbacks elapsed since the component scenario began.
    unsigned m_componentTicks { 0 };

    // TEMPORARY: arms the next policy callback to veto navigation.
    bool m_componentVetoNext { false };

    // Native engine wrapper hosted by the MiniBrowser shell.
    std::unique_ptr<WebKit::WebKitWebViewUWP> m_view;

    // Most recently requested remote URL for stale failure suppression.
    std::string m_lastNavUrl;

    // Installed package location used to locate fonts, CA bundle and templates.
    std::wstring m_packagePath;

    // Explicit blocked-page template override (empty: use blocked.html from
    // the install directory, then the embedded fallback). Lets a host app
    // restyle the page without rebuilding.
    std::wstring m_blockedTemplateOverride;

    // TEMPORARY blocked-template verification state; remove before acceptance.
    BlockedTemplateTestStage m_blockedTestStage { BlockedTemplateTestNotStarted };

    // TEMPORARY Rendering callback count used for the test timeout.
    unsigned m_blockedTestTicks { 0 };

    // TEMPORARY number of interstitials rendered during template verification.
    unsigned m_interstitialCount { 0 };

    // Protects fetch state shared between the worker and UI Rendering callback.
    SRWLOCK m_fetchLock;

    // True while the curl worker owns the current FetchRequest.
    bool m_fetchInFlight { false };

    // True when a worker result is ready for the UI callback.
    bool m_fetchReady { false };

    // Generation invalidating stale fetch results after new navigation.
    unsigned m_fetchGeneration { 0 };

    // True while the native remote navigation is being sampled for diagnostics.
    bool m_nativeNavPending { false };

    // Rendering callbacks sampled since the native navigation began.
    unsigned m_nativeNavTicks { 0 };

    // Completed worker result consumed by the UI callback.
    FetchResult m_fetchResult;

    // Rendering subscription that pumps fetch completion and diagnostics.
    EventRegistrationToken m_fetchRenderToken {};

    // XAML application owning lifecycle Suspending/Resuming subscriptions.
    ComPtr<Xaml::IApplication> m_application;

    // Lifecycle subscriptions; process-lifetime, revoked never before teardown.
    EventRegistrationToken m_suspendingToken {}, m_resumingToken {};

    // Serializes first-time window creation across launch/activation threads.
    SRWLOCK m_openLock;

    // Application view for visible-bounds layout; kept alive once configured.
    ComPtr<ABI::Windows::UI::ViewManagement::IApplicationView2> m_appView;
};

// One-time curl initialization state shared by all app instances.
INIT_ONCE MiniBrowserApp::s_curlInitOnce = INIT_ONCE_STATIC_INIT;

// UWP process entry: initializes WinRT, prepares the package log and runs XAML.
extern "C" void __stdcall WKMiniBrowserStartup()
{
    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(hr))
        ExitProcess(hr);
    ComPtr<Storage::IApplicationDataStatics> statics;
    ComPtr<Storage::IApplicationData> data;
    ComPtr<Storage::IStorageFolder> folder;
    HString path;
    hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Storage_ApplicationData).Get(), IID_PPV_ARGS(&statics));
    if (SUCCEEDED(hr) && SUCCEEDED(hr = statics->get_Current(&data)) && SUCCEEDED(hr = data->get_LocalFolder(&folder))
        && SUCCEEDED(hr = folderPath(folder.Get(), path))) {
        swprintf_s(logPath, L"%ls\\minibrowser.log", path.GetRawBuffer(nullptr));
        HANDLE file = CreateFile2(logPath, GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, nullptr);
        if (file != INVALID_HANDLE_VALUE)
            CloseHandle(file);
    }
    using AddFaultHandler = PVOID(WINAPI*)(ULONG, PVECTORED_EXCEPTION_HANDLER);
    auto addFaultHandler = reinterpret_cast<AddFaultHandler>(GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "AddVectoredExceptionHandler"));
    constexpr ULONG kVectoredExceptionHandlerFirstPriority = 1;
    logResult("native-fault-handler-installed", addFaultHandler && addFaultHandler(kVectoredExceptionHandlerFirstPriority, logNativeFault) ? WKPageLogTrueUWP : WKPageLogFalseUWP);
    logResult("minibrowser-startup", hr);
    ComPtr<Xaml::IApplicationStatics> application;
    hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_UI_Xaml_Application).Get(), IID_PPV_ARGS(&application));
    if (SUCCEEDED(hr)) {
        auto initialize = Callback<Xaml::IApplicationInitializationCallback>([](Xaml::IApplicationInitializationCallbackParams*) -> HRESULT {
            ComPtr<MiniBrowserApp> app;
            HRESULT hr = MakeAndInitialize<MiniBrowserApp>(&app);
            logResult("minibrowser-application-initialize", hr);
            if (SUCCEEDED(hr))
                app.Detach(); // Application lifetime, released at process teardown.
            return hr;
        });
        hr = application->Start(initialize.Get());
    }
    logResult("minibrowser-exit", hr);
    RoUninitialize();
    ExitProcess(hr);
}
