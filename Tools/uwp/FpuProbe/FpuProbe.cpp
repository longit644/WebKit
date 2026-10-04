// Isolates ARM32 setjmp/longjmp and the Windows CRT frame argument.
// 0xc0000028 is STATUS_BAD_STACK, not a floating-point exception.
// Explicit CRT calls below bypass the frame adapter for controlled comparison.
#include <windows.applicationmodel.core.h>
#include <windows.h>
#include <windows.storage.h>
#include <windows.ui.core.h>
#include <wrl.h>

#include <setjmp.h>
#include <stdio.h>

#include <string>

using namespace Microsoft::WRL;
using Microsoft::WRL::Wrappers::HString;
using Microsoft::WRL::Wrappers::HStringReference;
namespace Core = ABI::Windows::ApplicationModel::Core;
namespace UICore = ABI::Windows::UI::Core;
namespace Storage = ABI::Windows::Storage;

// Boolean values written to probe log fields.
enum ProbeLogBoolean {
    // Probe condition was false.
    ProbeLogFalse = 0,

    // Probe condition was true.
    ProbeLogTrue = 1
};

// Probe frame, return and arithmetic values used to distinguish each failure path.
constexpr int kInitialSetjmpReturn = 0;
constexpr int kLongjmpReturnValue = 7;
constexpr unsigned long kFpscrTrapEnableMask = 0x1f00;
constexpr double kBaselineLeftOperand = 2.0;
constexpr double kBaselineRightOperand = 4.0;
constexpr double kInitialFloatingValue = 1.0;
constexpr double kInitialFloatingMultiplier = 2.0;
constexpr double kPreJumpFloatingMultiplier = 3.0;
constexpr double kPostJumpFloatingMultiplier = 5.0;
constexpr unsigned long kFloatingResultScale = 1000;
constexpr ULONG kVectoredHandlerFirstPriority = 1;

static wchar_t logFilename[1024];

// Appends one probe diagnostic line and flushes it to device storage.
static void __stdcall logResult(const char* stage, unsigned long result)
{
    char text[160];
    int length = sprintf_s(text, "%s: 0x%08lx\r\n", stage, result);
    OutputDebugStringA(text);
    if (!*logFilename)
        return;
    HANDLE file = CreateFile2(logFilename, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_ALWAYS, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(file, text, static_cast<DWORD>(length), &written, nullptr);
        FlushFileBuffers(file);
        CloseHandle(file);
    }
}

// Reads FPSCR for diagnostic context; the observed crash was STATUS_BAD_STACK.
static unsigned long readFPSCR()
{
    unsigned long value;
    __asm__ volatile("fmrx %0, fpscr" : "=r"(value));
    return value;
}

// Diagnostic mask for FPSCR floating-point trap-enable bits.

// Floating-point jmp buffer observed by the original longjmp reproducer.
static jmp_buf env;

// Integer jmp buffer used to compare compiler-generated setjmp behavior.
static jmp_buf intEnv;

extern "C" {
// Optional inbox CRT setjmp symbol resolved at runtime.
void* wkProbeCrtSetjmp = nullptr;

// Calls the CRT setjmp adapter with SP passed as its hidden frame argument.
__attribute__((returns_twice)) int wkProbeSetjmpSp(jmp_buf);

// Historical comparison adapter passing R11 as the hidden frame argument.
__attribute__((returns_twice)) int wkProbeSetjmpR11(jmp_buf);
}
using CrtLongjmp = void(__cdecl*)(jmp_buf, int);
static CrtLongjmp crtLongjmp;
static jmp_buf crtSpEnv;

// Arbitrary nonzero second-return values proving each longjmp landed.
constexpr int kCrtProbeReturn = 11;
constexpr int kIntProbeReturn = 9;

// Calls the inbox CRT setjmp with explicit SP, then returns through CRT longjmp.
static void runCrtFrameProbe()
{
    auto module = GetModuleHandleW(L"VCRUNTIME140.dll");
    wkProbeCrtSetjmp = reinterpret_cast<void*>(GetProcAddress(module, "__intrinsic_setjmp"));
    crtLongjmp = reinterpret_cast<CrtLongjmp>(GetProcAddress(module, "longjmp"));
    logResult("probe-crt-symbols-found", wkProbeCrtSetjmp && crtLongjmp);
    if (!wkProbeCrtSetjmp || !crtLongjmp)
        return;

    int result = wkProbeSetjmpSp(crtSpEnv);
    logResult("probe-crt-sp-return", result);
    if (result == kInitialSetjmpReturn) {
        logResult("probe-crt-sp-frame", reinterpret_cast<_JUMP_BUFFER*>(crtSpEnv)->Frame);
        logResult("probe-crt-sp-saved-sp", reinterpret_cast<_JUMP_BUFFER*>(crtSpEnv)->Sp);
        logResult("probe-crt-sp-entering-longjmp", ProbeLogTrue);
        crtLongjmp(crtSpEnv, kCrtProbeReturn);
    }
    logResult("probe-crt-sp-survived", result == kCrtProbeReturn);

    // The r11 variant was reproduced in 1.0.0.10 and deliberately crashes.
    // Keep its assembly entry available, but let the remaining checks run.
}

// Verifies an integer local remains valid across compiler-generated longjmp.
static void runIntegerProbe()
{
    // Verify the compiler-generated setjmp path through the frame adapter.
    // volatile keeps a modified local defined across longjmp.
    constexpr int kInitialIntegerValue = 1;
    constexpr int kBeforeJumpIncrement = 2;
    constexpr int kInsideJumpIncrement = 4;
    volatile int value = kInitialIntegerValue;
    value += kBeforeJumpIncrement;

    int round = setjmp(intEnv);
    logResult("probe-int-setjmp-returned", static_cast<unsigned long>(round));
    if (round == kInitialSetjmpReturn) {
        value += kInsideJumpIncrement;
        logResult("probe-int-before-longjmp", static_cast<unsigned long>(value));
        longjmp(intEnv, kIntProbeReturn);
        logResult("probe-int-longjmp-returned-unexpectedly", ProbeLogTrue);
    }

    logResult("probe-int-after-longjmp", static_cast<unsigned long>(round));
    logResult("probe-int-value", static_cast<unsigned long>(value));
}

// No-inline floating operation prevents the compiler from folding probe work.
__declspec(noinline) static double multiply(double a, double b)
{
    return a * b;
}

// Resolved from kernelbase: the phone's inbox kernel32 predates the
// statically-linked entry point, same as the MiniBrowser shell does.
static LONG CALLBACK vectoredFault(EXCEPTION_POINTERS* info)
{
    logResult("probe-fault-code", info->ExceptionRecord->ExceptionCode);
    logResult("probe-fault-address", static_cast<unsigned long>(reinterpret_cast<ULONG_PTR>(info->ExceptionRecord->ExceptionAddress)));
    return EXCEPTION_CONTINUE_SEARCH;
}

// Resolves AddVectoredExceptionHandler from kernelbase for this phone SDK.
static void installFaultHandler()
{
    using AddFaultHandler = PVOID(WINAPI*)(ULONG, PVECTORED_EXCEPTION_HANDLER);
    auto addFaultHandler = reinterpret_cast<AddFaultHandler>(GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "AddVectoredExceptionHandler"));
    logResult("probe-fault-handler-installed", addFaultHandler && addFaultHandler(kVectoredHandlerFirstPriority, vectoredFault) ? ProbeLogTrue : ProbeLogFalse);
}

// Exercises floating-point work before, across and after setjmp/longjmp.
static void runProbe()
{
    // A baseline with no longjmp in between: proves plain FP works at all, so a
    // later fault is attributable to the longjmp rather than to FP support.
    logResult("probe-baseline-multiply", static_cast<unsigned long>(multiply(kBaselineLeftOperand, kBaselineRightOperand) * kFloatingResultScale));

    // A bare setjmp/longjmp round trip that carries an FP value across the
    // jump. A volatile local and noinline multiply prevent constant folding
    // and keep its post-jump value defined.
    volatile double value = kInitialFloatingValue;
    value = multiply(value, kInitialFloatingMultiplier);

    int round = setjmp(env);
    logResult("probe-setjmp-returned", static_cast<unsigned long>(round));
    if (round == kInitialSetjmpReturn) {
        value = multiply(value, kPreJumpFloatingMultiplier);
        logResult("probe-inside-before-longjmp", static_cast<unsigned long>(value * kFloatingResultScale));
        logResult("probe-entering-longjmp", ProbeLogTrue);
        longjmp(env, kLongjmpReturnValue);
        logResult("probe-longjmp-returned-unexpectedly", ProbeLogTrue);
    }

    logResult("probe-after-longjmp", static_cast<unsigned long>(round));
    unsigned long fpscr = readFPSCR();
    logResult("probe-fpscr-readback", fpscr);
    logResult("probe-fpscr-trap-bits", fpscr & kFpscrTrapEnableMask);

    // Verify that real floating-point work continues after the jump.
    value = multiply(value, kPostJumpFloatingMultiplier);
    logResult("probe-fp-op-after-longjmp-survived", ProbeLogTrue);
    logResult("probe-fp-result-x1000", static_cast<unsigned long>(value * kFloatingResultScale));
}

// Minimal XAML view hosting the FPU probe run; activation faults surface here.
class FpuView final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, Core::IFrameworkView> {
    InspectableClass(L"WebKitFpuProbe.View", BaseTrust);

public:
    // No application-view initialization is needed by this probe.
    HRESULT STDMETHODCALLTYPE Initialize(Core::ICoreApplicationView*) override { return S_OK; }

    // Stores the CoreWindow used for activation and event pumping.
    HRESULT STDMETHODCALLTYPE SetWindow(UICore::ICoreWindow* window) override
    {
        m_window = window;
        return S_OK;
    }

    // No package resources need loading before Run.
    HRESULT STDMETHODCALLTYPE Load(HSTRING) override { return S_OK; }

    // Activates the window, runs all CRT probes and keeps the event loop alive.
    HRESULT STDMETHODCALLTYPE Run() override
    {
        // Log before anything else can fail, so a truncated log tells us
        // whether Run was ever entered or whether the fault is in activation.
        logResult("probe-run-entered", ProbeLogTrue);
        HRESULT result = m_window ? m_window->Activate() : S_OK;
        logResult("probe-activate-hresult", static_cast<unsigned long>(result));
        if (FAILED(result))
            return result;
        // The vectored-handler install itself appears to fault before it can log,
        // so bypass it for now. File logging flushes after every line, and the
        // probe sequence below is the signal we need.
        logResult("probe-skipping-fault-handler", ProbeLogTrue);
        runCrtFrameProbe();
        runIntegerProbe();
        runProbe();
        logResult("fpuprobe-completed", ProbeLogTrue);
        ComPtr<UICore::ICoreDispatcher> events;
        if (SUCCEEDED(m_window->get_Dispatcher(&events)))
            return events->ProcessEvents(UICore::CoreProcessEventsOption_ProcessUntilQuit);
        return S_OK;
    }

    // No teardown work is required by the probe view.
    HRESULT STDMETHODCALLTYPE Uninitialize() override { return S_OK; }

private:
    // Window hosting the probe's event dispatcher.
    ComPtr<UICore::ICoreWindow> m_window;
};

// View factory named by the probe manifest's EntryPoint.
class FpuViewSource final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, Core::IFrameworkViewSource> {
    InspectableClass(L"WebKitFpuProbe.ViewSource", BaseTrust);

public:
    // Creates the probe view requested by CoreApplication::Run.
    HRESULT STDMETHODCALLTYPE CreateView(Core::IFrameworkView** view) override { return Make<FpuView>().CopyTo(view); }
};

// Probe entry point: truncates the log first so an empty file proves a pre-probe crash.
extern "C" void __stdcall WKFpuProbeStartup()
{
    HRESULT result = RoInitialize(RO_INIT_MULTITHREADED);
    logResult("fpuprobe-startup", result);
    if (SUCCEEDED(result)) {
        ComPtr<Storage::IApplicationDataStatics> statics;
        ComPtr<Storage::IApplicationData> data;
        ComPtr<Storage::IStorageFolder> folder;
        HString path;
        if (SUCCEEDED(RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Storage_ApplicationData).Get(), IID_PPV_ARGS(&statics)))
            && SUCCEEDED(statics->get_Current(&data)) && SUCCEEDED(data->get_LocalFolder(&folder))) {
            ComPtr<Storage::IStorageItem> item;
            HString folderPath;
            if (SUCCEEDED(folder.As(&item)) && SUCCEEDED(item->get_Path(folderPath.GetAddressOf()))) {
                UINT32 folderLength = 0;
                const wchar_t* folderRaw = folderPath.GetRawBuffer(&folderLength);
                std::wstring filename(folderRaw ? folderRaw : L"", folderLength);
                filename += L"\\fpuprobe.log";
                wcscpy_s(logFilename, filename.c_str());
                HANDLE file = CreateFile2(logFilename, GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, nullptr);
                if (file != INVALID_HANDLE_VALUE)
                    CloseHandle(file);
                // Written after the truncation above, so an empty file means the
                // crash happened before this point rather than mid-probe.
                logResult("probe-log-opened", ProbeLogTrue);
            }
        }
        ComPtr<Core::ICoreApplication> application;
        result = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_ApplicationModel_Core_CoreApplication).Get(), IID_PPV_ARGS(&application));
        if (SUCCEEDED(result))
            result = application->Run(Make<FpuViewSource>().Get());
        RoUninitialize();
    }
    ExitProcess(static_cast<UINT>(result));
}
