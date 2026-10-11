// Native smoke-payload driver for the C# RuntimeSmoke host (M10 8.5).
// Results are appended to the log file named by WKRunSmokeDiagnostics using
// the historical LocalState/runtime-smoke.txt line protocol. Sequencing,
// constants and log stage names are preserved from SmokeHost.cpp; only the
// application/activation/window ownership moved to C#, and the first-page
// render is now called through the declared WKRenderFirstPageUWPType instead
// of an inline reinterpret_cast.
#include "SmokeDiagnostics.h"

#include "PageW10M.h"
#include "WebKitWebViewPresenterW10M.h"
#include <memory>
#include <stdio.h>
#include <windows.h>
#include <windows.ui.core.h>
#include <wrl.h>

using namespace Microsoft::WRL;
namespace UI = ABI::Windows::UI::Core;

// The BGRA stride unit below must stay the ABI bytes-per-pixel value.
static_assert(W10MBytesPerBGRA == 4, "smoke snapshot assumes 4-byte BGRA pixels");

// Diagnostic process result conventions: zero is success.
constexpr unsigned kDiagnosticSuccess = 0;

// Diagnostic process result conventions: nonzero is failure.
constexpr unsigned kDiagnosticFailure = 1;

// Failure mask reported when the smoke export cannot be resolved.
constexpr unsigned kMissingSmokeExportFailureMask = 0xffffffffU;

// Capacity of the staged font-directory and log-path buffers.
constexpr DWORD kTemporaryPathCapacity = 1024;

// Capacity of one formatted diagnostic log line.
constexpr size_t kSmokeLogLineCapacity = 160;

// Default flags for the packaged-library loader below.
constexpr DWORD kDefaultPackagedLibraryFlags = 0;

// Default flags for releasing the snapshot pixel buffer.
constexpr DWORD kHeapFreeDefaultFlags = 0;

// Source line reported for the evaluated JavaScript probe.
constexpr int kJavaScriptProbeStartingLine = 1;

// Sentinel reported when JavaScript evaluation produced no usable number.
constexpr double kJavaScriptConversionFailure = -1.0;

// Number of loop iterations used to exercise JavaScriptCore execution.
constexpr unsigned kJavaScriptProbeLoopCount = 100;

// Format string for the arithmetic expression evaluated by JavaScriptCore.
constexpr char kJavaScriptProbeFormat[] = "(() => { let sum = 0; for (let i = 0; i < %u; ++i) sum += i; return sum; })()";

// Capacity of the formatted JavaScript probe source.
constexpr size_t kJavaScriptProbeSourceCapacity = 128;

// Append target for diagnostic lines, supplied by the C# host per run.
static HANDLE logHandle = INVALID_HANDLE_VALUE;

// Append target path for diagnostic lines, supplied by the C# host per run.
static wchar_t logFilename[kTemporaryPathCapacity];

// Serializes diagnostic line appends; the driver runs on one thread today.
static SRWLOCK logLock = SRWLOCK_INIT;

// Appends one diagnostic line to the host log; safe from any thread.
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

// Runs the CRT, JavaScriptCore and WebCore probes plus the one-frame snapshot
// render and D3D11 presentation on the calling UI/engine thread. See
// SmokeDiagnostics.h for the ownership contract of each parameter.
extern "C" __declspec(dllexport) unsigned __stdcall WKRunSmokeDiagnostics(
    const wchar_t* fontDirectory,
    const wchar_t* logPath,
    unsigned width,
    unsigned height,
    void* coreWindowUnknown)
{
    if (logPath) {
        // The C# host creates the file; this driver only appends through it.
        wcscpy_s(logFilename, logPath);
    }
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
    // The historical "worker-started" label is retained for protocol
    // stability; since version 1.0.0.10 this sequence runs on the owning
    // UI/engine thread rather than a background worker.
    logResult("worker-started", WKPageLogFalseW10M);
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
    logResult("load-webcore", webCore ? WKPageLogFalseW10M : GetLastError());
    if (!webCore)
        return kDiagnosticFailure;
    // Declared signature (SmokeDiagnostics.h); no inline reinterpret_cast.
    auto render = reinterpret_cast<WKRenderFirstPageUWPType>(GetProcAddress(webCore, "WKRenderFirstPageUWP"));
    if (!render) {
        logResult("page-render-export", ERROR_PROC_NOT_FOUND);
        return kDiagnosticFailure;
    }
    if (!fontDirectory) {
        logResult("page-font-path", E_INVALIDARG);
        return kDiagnosticFailure;
    }
    wchar_t fonts[kTemporaryPathCapacity];
    swprintf_s(fonts, L"%ls", fontDirectory);

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
    pixels->bytes = static_cast<unsigned char*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, width * height * W10MBytesPerBGRA));
    if (!pixels->bytes) {
        logResult("page-pixel-allocation", ERROR_NOT_ENOUGH_MEMORY);
        return kDiagnosticFailure;
    }
    unsigned renderResult = render(fonts, pixels->bytes, width, height, logResult);
    logResult("page-render-result", renderResult);
    if (renderResult)
        return kDiagnosticFailure;
    // One-frame bring-up runs on the owning UI/engine thread. The CoreWindow
    // arrives from the C# host; without it presentation is skipped but every
    // earlier stage above still counts. The presenter lives for the process
    // lifetime, matching the retired native host.
    ComPtr<UI::ICoreWindow> window;
    HRESULT windowResult = coreWindowUnknown
        ? reinterpret_cast<IUnknown*>(coreWindowUnknown)->QueryInterface(IID_PPV_ARGS(&window))
        : E_POINTER;
    auto* presenter = new WebKit::WebKitWebViewPresenterW10M;
    HRESULT result = SUCCEEDED(windowResult)
        ? presenter->initializeForCoreWindow(window.Get(), width, height)
        : windowResult;
    logResult("page-d3d11-initialize", result);
    if (SUCCEEDED(result)) {
        result = presenter->present(pixels->bytes, width * W10MBytesPerBGRA);
        logResult("page-d3d11-present", result);
    }
    return kDiagnosticSuccess;
}
