// Native WinRT diagnostic host. Results go to LocalState/runtime-smoke.txt.
#include "PageUWP.h"
#include "WebKitWebViewPresenterUWP.h"
#include <memory>
#include <process.h>
#include <stdio.h>
#include <windows.applicationmodel.core.h>
#include <windows.h>
#include <windows.storage.h>
#include <windows.system.display.h>
#include <windows.ui.core.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>

using namespace Microsoft::WRL;
using Microsoft::WRL::Wrappers::HString;
using Microsoft::WRL::Wrappers::HStringReference;
namespace Core = ABI::Windows::ApplicationModel::Core;
namespace UI = ABI::Windows::UI::Core;
namespace Storage = ABI::Windows::Storage;

// Diagnostic process exit conventions and temporary-log path capacity.
constexpr unsigned kDiagnosticSuccess = 0;
constexpr unsigned kDiagnosticFailure = 1;
constexpr unsigned kMissingSmokeExportFailureMask = 0xffffffffU;
constexpr DWORD kTemporaryPathCapacity = 1024;
constexpr DWORD kTemporaryPathReadLimit = 900;
constexpr size_t kSmokeLogLineCapacity = 160;
constexpr unsigned kPageBytesPerBGRA = 4;
constexpr float kMinimumVisibleExtent = 0.0f;
constexpr DWORD kDefaultPackagedLibraryFlags = 0;
constexpr DWORD kHeapFreeDefaultFlags = 0;
constexpr int kJavaScriptProbeStartingLine = 1;
constexpr double kJavaScriptConversionFailure = -1.0;
// Number of loop iterations used to exercise JavaScriptCore execution.
constexpr unsigned kJavaScriptProbeLoopCount = 100;
// Format string for the arithmetic expression evaluated by JavaScriptCore.
constexpr char kJavaScriptProbeFormat[] = "(() => { let sum = 0; for (let i = 0; i < %u; ++i) sum += i; return sum; })()";
constexpr size_t kJavaScriptProbeSourceCapacity = 128;

// Legacy one-frame bring-up state: temp log, presentation window and snapshot extent.
static HANDLE logHandle = INVALID_HANDLE_VALUE;
static wchar_t logFilename[kTemporaryPathCapacity];
static SRWLOCK logLock = SRWLOCK_INIT;
static UI::ICoreWindow* presentationWindow;
static UI::ICoreDispatcher* presentationDispatcher;
static WebKit::WebKitWebViewPresenterUWP* presenter;

// Snapshot extent for the one-frame render below.
constexpr unsigned kSnapshotWidth = 480;
constexpr unsigned kSnapshotHeight = 800;
static unsigned pageWidth = kSnapshotWidth;
static unsigned pageHeight = kSnapshotHeight;

// Opens the temp log before the package LocalState path is known.
// Opens a fallback temp log before the package LocalState path is available.
static void openEarlyLog()
{
    wchar_t filename[kTemporaryPathCapacity];
    DWORD length = GetTempPathW(kTemporaryPathReadLimit, filename);
    if (!length || length >= kTemporaryPathReadLimit)
        return;
    const wchar_t suffix[] = L"runtime-smoke.txt";
    for (unsigned i = 0; i < sizeof(suffix) / sizeof(*suffix); ++i)
        filename[length + i] = suffix[i];
    wcscpy_s(logFilename, filename);
    logHandle = CreateFile2(filename, GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, nullptr);
}

// Appends one diagnostic line to the temp log; safe from any thread.
static void __stdcall logResult(const char* stage, unsigned long result)
{
    char text[kSmokeLogLineCapacity];
    int length = sprintf_s(text, "%s: 0x%08lx\r\n", stage, result);
    OutputDebugStringA(text);
    AcquireSRWLockExclusive(&logLock);
    if (logHandle == INVALID_HANDLE_VALUE && *logFilename)
        logHandle = CreateFile2(logFilename, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_ALWAYS, nullptr);
    if (logHandle != INVALID_HANDLE_VALUE && length > 0) {
        DWORD written;
        WriteFile(logHandle, text, static_cast<DWORD>(length), &written, nullptr);
        FlushFileBuffers(logHandle);
    }
    if (logHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(logHandle);
        logHandle = INVALID_HANDLE_VALUE;
    }
    ReleaseSRWLockExclusive(&logLock);
}

// Creates the append target in package LocalState and replaces the temp log handle.
// Creates the runtime-smoke log in package LocalState.
static HRESULT openLog()
{
    ComPtr<Storage::IApplicationDataStatics> statics;
    HRESULT result = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Storage_ApplicationData).Get(), IID_PPV_ARGS(&statics));
    if (FAILED(result))
        return result;
    ComPtr<Storage::IApplicationData> data;
    result = statics->get_Current(&data);
    if (FAILED(result))
        return result;
    ComPtr<Storage::IStorageFolder> folder;
    result = data->get_LocalFolder(&folder);
    if (FAILED(result))
        return result;
    ComPtr<Storage::IStorageItem> item;
    result = folder.As(&item);
    if (FAILED(result))
        return result;
    HString path;
    result = item->get_Path(path.GetAddressOf());
    if (FAILED(result))
        return result;
    wchar_t filename[kTemporaryPathCapacity];
    swprintf_s(filename, L"%ls\\runtime-smoke.txt", path.GetRawBuffer(nullptr));
    HANDLE localLog = CreateFile2(filename, GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, nullptr);
    if (localLog == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());
    if (logHandle != INVALID_HANDLE_VALUE)
        CloseHandle(logHandle);
    logHandle = localLog;
    wcscpy_s(logFilename, filename);
    return S_OK;
}

// Loads CRT, JavaScriptCore and WebCore probes while the owning XAML thread is current.
static unsigned __stdcall runDiagnostics(void*)
{
    HRESULT apartment = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) {
        logResult("initialize-worker-apartment", apartment);
        return kDiagnosticFailure;
    }

    // Balances RoInitialize only when this call created the apartment.
    struct ApartmentScope {
        // Whether this scope owns a successful RoInitialize call.
        bool ownsInitialization;

        // Uninitializes the apartment only when this scope initialized it.
        ~ApartmentScope()
        {
            if (ownsInitialization)
                RoUninitialize();
        }
    } apartmentScope { SUCCEEDED(apartment) };
    logResult("worker-started", WKPageLogFalseUWP);
    HMODULE smoke = LoadPackagedLibrary(L"WebKitRuntimeSmoke.dll", kDefaultPackagedLibraryFlags);
    if (!smoke) {
        logResult("load-runtime-smoke", GetLastError());
        return kDiagnosticFailure;
    }
    auto runSmoke = reinterpret_cast<unsigned(__stdcall*)()>(GetProcAddress(smoke, "WKRunRuntimeSmoke"));
    unsigned failures = runSmoke ? runSmoke() : kMissingSmokeExportFailureMask;
    logResult("runtime-smoke-failure-mask", failures);
    // Keep it loaded: the loader/TLS probe precedes all engine work.
    if (failures)
        return kDiagnosticFailure;
    HMODULE engine = LoadPackagedLibrary(L"JavaScriptCore.dll", kDefaultPackagedLibraryFlags);
    if (!engine) {
        logResult("load-javascriptcore", GetLastError());
        return kDiagnosticFailure;
    }
    using Context = const void*;
    using String = const void*;
    using Value = const void*;
    auto createContext = reinterpret_cast<Context (*)(const void*)>(GetProcAddress(engine, "JSGlobalContextCreate"));
    auto releaseContext = reinterpret_cast<void (*)(Context)>(GetProcAddress(engine, "JSGlobalContextRelease"));
    auto createString = reinterpret_cast<String (*)(const char*)>(GetProcAddress(engine, "JSStringCreateWithUTF8CString"));
    auto releaseString = reinterpret_cast<void (*)(String)>(GetProcAddress(engine, "JSStringRelease"));
    auto evaluate = reinterpret_cast<Value (*)(Context, String, const void*, String, int, Value*)>(GetProcAddress(engine, "JSEvaluateScript"));
    auto toNumber = reinterpret_cast<double (*)(Context, Value, Value*)>(GetProcAddress(engine, "JSValueToNumber"));
    if (!createContext || !releaseContext || !createString || !releaseString || !evaluate || !toNumber) {
        logResult("javascriptcore-exports", ERROR_PROC_NOT_FOUND);
        return kDiagnosticFailure;
    }
    Context context = createContext(nullptr);
    if (!context) {
        logResult("create-js-context", ERROR_NOT_ENOUGH_MEMORY);
        return kDiagnosticFailure;
    }
    char scriptSource[kJavaScriptProbeSourceCapacity];
    sprintf_s(scriptSource, kJavaScriptProbeSourceCapacity, kJavaScriptProbeFormat, kJavaScriptProbeLoopCount);
    String script = createString(scriptSource);
    Value exception = nullptr;
    Value value = evaluate(context, script, nullptr, nullptr, kJavaScriptProbeStartingLine, &exception);
    double number = value && !exception ? toNumber(context, value, &exception) : kJavaScriptConversionFailure;
    // Expected probe result: the sum of 0..99.
    constexpr double kExpectedProbeSum = kJavaScriptProbeLoopCount * (kJavaScriptProbeLoopCount - 1) / 2;
    logResult("javascript-evaluation", !exception && number == kExpectedProbeSum ? kDiagnosticSuccess : kDiagnosticFailure);
    releaseString(script);
    releaseContext(context);
    HMODULE webCore = LoadPackagedLibrary(L"WebCore.dll", kDefaultPackagedLibraryFlags);
    logResult("load-webcore", webCore ? WKPageLogFalseUWP : GetLastError());
    if (!webCore)
        return kDiagnosticFailure;
    auto render = reinterpret_cast<unsigned(__stdcall*)(const wchar_t*, unsigned char*, unsigned, unsigned, void(__stdcall*)(const char*, unsigned long))>(GetProcAddress(webCore, "WKRenderFirstPageUWP"));
    if (!render) {
        logResult("page-render-export", ERROR_PROC_NOT_FOUND);
        return kDiagnosticFailure;
    }
    ComPtr<ABI::Windows::ApplicationModel::IPackageStatics> packages;
    ComPtr<ABI::Windows::ApplicationModel::IPackage> package;
    ComPtr<Storage::IStorageFolder> installed;
    ComPtr<Storage::IStorageItem> installedItem;
    HString path;
    HRESULT result = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_ApplicationModel_Package).Get(), IID_PPV_ARGS(&packages));
    if (FAILED(result) || FAILED(result = packages->get_Current(&package))
        || FAILED(result = package->get_InstalledLocation(&installed))
        || FAILED(result = installed.As(&installedItem)) || FAILED(result = installedItem->get_Path(path.GetAddressOf()))) {
        logResult("page-font-path", result);
        return kDiagnosticFailure;
    }
    wchar_t fonts[kTemporaryPathCapacity];
    swprintf_s(fonts, L"%ls\\Fonts", path.GetRawBuffer(nullptr));

    // Heap-backed snapshot pixels retained until the UI frame is presented.
    struct PixelFrame {
        // Allocated BGRA image passed between render and present.
        unsigned char* bytes;

        // Frees the snapshot pixel buffer when its shared owner exits.
        ~PixelFrame()
        {
            if (bytes)
                HeapFree(GetProcessHeap(), kHeapFreeDefaultFlags, bytes);
        }
    };
    auto pixels = std::make_shared<PixelFrame>();
    // BGRA backing store for the one-frame render below.
    pixels->bytes = static_cast<unsigned char*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, pageWidth * pageHeight * kPageBytesPerBGRA));
    if (!pixels->bytes) {
        logResult("page-pixel-allocation", ERROR_NOT_ENOUGH_MEMORY);
        return kDiagnosticFailure;
    }
    unsigned renderResult = render(fonts, pixels->bytes, pageWidth, pageHeight, logResult);
    logResult("page-render-result", renderResult);
    if (renderResult)
        return kDiagnosticFailure;
    // One-frame bring-up runs on the owning UI/engine thread. Keep that
    // engine thread alive for the CoreWindow lifetime and present directly.
    presenter = new WebKit::WebKitWebViewPresenterUWP;
    result = presenter->initializeForCoreWindow(presentationWindow, pageWidth, pageHeight);
    logResult("page-d3d11-initialize", result);
    if (SUCCEEDED(result)) {
        result = presenter->present(pixels->bytes, pageWidth * kPageBytesPerBGRA);
        logResult("page-d3d11-present", result);
    }
    return kDiagnosticSuccess;
}

// Minimal XAML view hosting the runtime-smoke run.
class SmokeView final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, Core::IFrameworkView> {
    InspectableClass(L"WebKitRuntimeSmoke.View", BaseTrust);

public:
    // Subscribes to activation so the CoreWindow exists before smoke work starts.
    HRESULT STDMETHODCALLTYPE Initialize(Core::ICoreApplicationView* view) override
    {
        auto activated = Callback<ABI::Windows::Foundation::ITypedEventHandler<Core::CoreApplicationView*, ABI::Windows::ApplicationModel::Activation::IActivatedEventArgs*>>(
            [this](Core::ICoreApplicationView*, ABI::Windows::ApplicationModel::Activation::IActivatedEventArgs*) -> HRESULT {
                logResult("view-activated", WKPageLogFalseUWP);
                return m_window->Activate();
            });
        EventRegistrationToken token;
        return view->add_Activated(activated.Get(), &token);
    }

    // Stores the CoreWindow that owns the smoke UI.
    HRESULT STDMETHODCALLTYPE SetWindow(UI::ICoreWindow* window) override
    {
        m_window = window;
        return S_OK;
    }

    // No packaged XAML page needs to load before Run.
    HRESULT STDMETHODCALLTYPE Load(HSTRING) override { return S_OK; }

    // Activates the window, runs the diagnostics, presents one frame and pumps events.
    HRESULT STDMETHODCALLTYPE Run() override
    {
        HRESULT result = m_window->Activate();
        if (FAILED(result))
            return result;
        ComPtr<UI::ICoreDispatcher> dispatcher;
        result = m_window->get_Dispatcher(&dispatcher);
        if (FAILED(result))
            return result;
        presentationWindow = m_window.Get();
        presentationDispatcher = dispatcher.Get();
        ABI::Windows::Foundation::Rect bounds;
        if (SUCCEEDED(m_window->get_Bounds(&bounds))) {
            if (bounds.Width > kMinimumVisibleExtent && bounds.Width <= UWPMaxPageDimension)
                pageWidth = static_cast<unsigned>(bounds.Width);
            if (bounds.Height > kMinimumVisibleExtent && bounds.Height <= UWPMaxPageDimension)
                pageHeight = static_cast<unsigned>(bounds.Height);
        }
        logResult("page-viewport-width", pageWidth);
        logResult("page-viewport-height", pageHeight);
        ComPtr<IInspectable> display;
        if (SUCCEEDED(RoActivateInstance(HStringReference(RuntimeClass_Windows_System_Display_DisplayRequest).Get(), &display))
            && SUCCEEDED(display.As(&m_displayRequest)))
            m_displayActive = SUCCEEDED(m_displayRequest->RequestActive());
        runDiagnostics(nullptr);
        return dispatcher->ProcessEvents(UI::CoreProcessEventsOption_ProcessUntilQuit);
    }

    // Releases the display keep-awake request when the CoreWindow closes.
    HRESULT STDMETHODCALLTYPE Uninitialize() override
    {
        if (m_displayActive)
            m_displayRequest->RequestRelease();
        return S_OK;
    }

private:
    // Window owned by CoreApplication for this view.
    ComPtr<UI::ICoreWindow> m_window;

    // Keeps the display awake while the one-frame smoke app runs.
    ComPtr<ABI::Windows::System::Display::IDisplayRequest> m_displayRequest;

    // True only when RequestActive succeeded and a release is required.
    bool m_displayActive { false };
};

// View factory named by the smoke manifest's EntryPoint.
class SmokeViewSource final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, Core::IFrameworkViewSource> {
    InspectableClass(L"WebKitRuntimeSmoke.ViewSource", BaseTrust);

public:
    // Creates the native smoke view requested by the manifest entry point.
    HRESULT STDMETHODCALLTYPE CreateView(Core::IFrameworkView** view) override { return Make<SmokeView>().CopyTo(view); }
};

// Package entry point: initializes WinRT and runs the diagnostic XAML view.
extern "C" void __stdcall WKSmokeStartup()
{
    // No global C++ constructors or TLS variables in this host; the probe DLL
    // exercises the custom startup path separately before engine loading.
    openEarlyLog();
    logResult("entrypoint-reached", WKPageLogFalseUWP);
    HRESULT result = RoInitialize(RO_INIT_MULTITHREADED);
    logResult("initialize-main-apartment", result);
    if (SUCCEEDED(result)) {
        logResult("open-log", openLog());
        logResult("host-started", WKPageLogFalseUWP);
        ComPtr<Core::ICoreApplication> application;
        result = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_ApplicationModel_Core_CoreApplication).Get(), IID_PPV_ARGS(&application));
        logResult("core-application-factory", result);
        if (SUCCEEDED(result))
            result = application->Run(Make<SmokeViewSource>().Get());
        logResult("core-application-run-returned", result);
        RoUninitialize();
    }
    ExitProcess(static_cast<UINT>(result));
}
