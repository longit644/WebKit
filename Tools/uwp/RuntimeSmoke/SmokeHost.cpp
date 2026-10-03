// Native WinRT diagnostic host. Results go to LocalState/runtime-smoke.txt.
#include <windows.h>
#include <windows.applicationmodel.core.h>
#include <windows.storage.h>
#include <windows.ui.core.h>
#include <windows.system.display.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <stdio.h>
#include <process.h>
#include <memory>
#include "WebKitWebViewPresenterUWP.h"

using namespace Microsoft::WRL;
using Microsoft::WRL::Wrappers::HString;
using Microsoft::WRL::Wrappers::HStringReference;
namespace Core = ABI::Windows::ApplicationModel::Core;
namespace UI = ABI::Windows::UI::Core;
namespace Storage = ABI::Windows::Storage;

static HANDLE logHandle = INVALID_HANDLE_VALUE;
static wchar_t logFilename[1024];
static SRWLOCK logLock = SRWLOCK_INIT;
static UI::ICoreWindow* presentationWindow;
static UI::ICoreDispatcher* presentationDispatcher;
static WebKit::WebKitWebViewPresenterUWP* presenter;
static unsigned pageWidth = 480;
static unsigned pageHeight = 800;
static void openEarlyLog()
{
    wchar_t filename[1024];
    DWORD length = GetTempPathW(900, filename);
    if (!length || length >= 900)
        return;
    const wchar_t suffix[] = L"runtime-smoke.txt";
    for (unsigned i = 0; i < sizeof(suffix) / sizeof(*suffix); ++i)
        filename[length + i] = suffix[i];
    wcscpy_s(logFilename, filename);
    logHandle = CreateFile2(filename, GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, nullptr);
}
static void __stdcall logResult(const char* stage, unsigned long result)
{
    char text[160];
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
    wchar_t filename[1024];
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

static unsigned __stdcall runDiagnostics(void*)
{
    HRESULT apartment = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) {
        logResult("initialize-worker-apartment", apartment);
        return 1;
    }
    struct ApartmentScope {
        bool ownsInitialization;
        ~ApartmentScope() { if (ownsInitialization) RoUninitialize(); }
    } apartmentScope { SUCCEEDED(apartment) };
    logResult("worker-started", 0);
    HMODULE smoke = LoadPackagedLibrary(L"WebKitRuntimeSmoke.dll", 0);
    if (!smoke) {
        logResult("load-runtime-smoke", GetLastError());
        return 1;
    }
    auto runSmoke = reinterpret_cast<unsigned (__stdcall*)()>(GetProcAddress(smoke, "WKRunRuntimeSmoke"));
    unsigned failures = runSmoke ? runSmoke() : 0xffffffff;
    logResult("runtime-smoke-failure-mask", failures);
    // Keep it loaded: the loader/TLS probe precedes all engine work.
    if (failures)
        return 1;
    HMODULE engine = LoadPackagedLibrary(L"JavaScriptCore.dll", 0);
    if (!engine) {
        logResult("load-javascriptcore", GetLastError());
        return 1;
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
        return 1;
    }
    Context context = createContext(nullptr);
    if (!context) {
        logResult("create-js-context", ERROR_NOT_ENOUGH_MEMORY);
        return 1;
    }
    String script = createString("(() => { let sum = 0; for (let i = 0; i < 100; ++i) sum += i; return sum; })()");
    Value exception = nullptr;
    Value value = evaluate(context, script, nullptr, nullptr, 1, &exception);
    double number = value && !exception ? toNumber(context, value, &exception) : -1;
    logResult("javascript-evaluation", !exception && number == 4950 ? 0 : 1);
    releaseString(script);
    releaseContext(context);
    HMODULE webCore = LoadPackagedLibrary(L"WebCore.dll", 0);
    logResult("load-webcore", webCore ? 0 : GetLastError());
    if (!webCore)
        return 1;
    auto render = reinterpret_cast<unsigned (__stdcall*)(const wchar_t*, unsigned char*, unsigned, unsigned, void (__stdcall*)(const char*, unsigned long))>(GetProcAddress(webCore, "WKRenderFirstPageUWP"));
    if (!render) {
        logResult("page-render-export", ERROR_PROC_NOT_FOUND);
        return 1;
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
        return 1;
    }
    wchar_t fonts[1024];
    swprintf_s(fonts, L"%ls\\Fonts", path.GetRawBuffer(nullptr));
    struct PixelFrame {
        unsigned char* bytes;
        ~PixelFrame() { if (bytes) HeapFree(GetProcessHeap(), 0, bytes); }
    };
    auto pixels = std::make_shared<PixelFrame>();
    pixels->bytes = static_cast<unsigned char*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, pageWidth * pageHeight * 4));
    if (!pixels->bytes) {
        logResult("page-pixel-allocation", ERROR_NOT_ENOUGH_MEMORY);
        return 1;
    }
    unsigned renderResult = render(fonts, pixels->bytes, pageWidth, pageHeight, logResult);
    logResult("page-render-result", renderResult);
    if (renderResult)
        return 1;
    // One-frame bring-up runs on the owning UI/engine thread. Keep that
    // engine thread alive for the CoreWindow lifetime and present directly.
    presenter = new WebKit::WebKitWebViewPresenterUWP;
    result = presenter->initializeForCoreWindow(presentationWindow, pageWidth, pageHeight);
    logResult("page-d3d11-initialize", result);
    if (SUCCEEDED(result)) {
        result = presenter->present(pixels->bytes, pageWidth * 4);
        logResult("page-d3d11-present", result);
    }
    return 0;
}

class SmokeView final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, Core::IFrameworkView> {
    InspectableClass(L"WebKitRuntimeSmoke.View", BaseTrust);
public:
    HRESULT STDMETHODCALLTYPE Initialize(Core::ICoreApplicationView* view) override
    {
        auto activated = Callback<ABI::Windows::Foundation::ITypedEventHandler<Core::CoreApplicationView*, ABI::Windows::ApplicationModel::Activation::IActivatedEventArgs*>>(
            [this](Core::ICoreApplicationView*, ABI::Windows::ApplicationModel::Activation::IActivatedEventArgs*) -> HRESULT {
                logResult("view-activated", 0);
                return m_window->Activate();
            });
        EventRegistrationToken token;
        return view->add_Activated(activated.Get(), &token);
    }
    HRESULT STDMETHODCALLTYPE SetWindow(UI::ICoreWindow* window) override { m_window = window; return S_OK; }
    HRESULT STDMETHODCALLTYPE Load(HSTRING) override { return S_OK; }
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
            if (bounds.Width > 0 && bounds.Width <= 2048)
                pageWidth = static_cast<unsigned>(bounds.Width);
            if (bounds.Height > 0 && bounds.Height <= 2048)
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
    HRESULT STDMETHODCALLTYPE Uninitialize() override
    {
        if (m_displayActive)
            m_displayRequest->RequestRelease();
        return S_OK;
    }
private:
    ComPtr<UI::ICoreWindow> m_window;
    ComPtr<ABI::Windows::System::Display::IDisplayRequest> m_displayRequest;
    bool m_displayActive { false };
};

class SmokeViewSource final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, Core::IFrameworkViewSource> {
    InspectableClass(L"WebKitRuntimeSmoke.ViewSource", BaseTrust);
public:
    HRESULT STDMETHODCALLTYPE CreateView(Core::IFrameworkView** view) override { return Make<SmokeView>().CopyTo(view); }
};

extern "C" void __stdcall WKSmokeStartup()
{
    // No global C++ constructors or TLS variables in this host; the probe DLL
    // exercises the custom startup path separately before engine loading.
    openEarlyLog();
    logResult("entrypoint-reached", 0);
    HRESULT result = RoInitialize(RO_INIT_MULTITHREADED);
    logResult("initialize-main-apartment", result);
    if (SUCCEEDED(result)) {
        logResult("open-log", openLog());
        logResult("host-started", 0);
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
