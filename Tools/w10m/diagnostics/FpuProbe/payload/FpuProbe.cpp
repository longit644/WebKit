// Isolates ARM32 setjmp/longjmp and the Windows CRT frame argument.
// 0xc0000028 is STATUS_BAD_STACK, not a floating-point exception.
// Explicit CRT calls below bypass the frame adapter for controlled comparison.
// M10 task 8.6: native payload DLL for the C# FpuProbe host. The WRL
// IFrameworkView host is gone; WKFpuProbeRun owns the same probe sequence and
// the same LocalState\fpuprobe.log protocol, and the C# host owns activation,
// lifetime and result verification.
#include "FpuProbePayload.h"

#include <windows.h>
#include <windows.storage.h>

#include <roapi.h>
#include <wrl.h>

#include <setjmp.h>
#include <stdio.h>

#include <string>

using namespace Microsoft::WRL;
using Microsoft::WRL::Wrappers::HString;
using Microsoft::WRL::Wrappers::HStringReference;
namespace Storage = ABI::Windows::Storage;

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

// Floating-point jmp buffer observed by the original longjmp reproducer.
static jmp_buf env;

// Integer jmp buffer used to compare compiler-generated setjmp behavior.
static jmp_buf intEnv;

extern "C" {
// Optional inbox CRT setjmp symbol resolved at runtime.
void* wkProbeCrtSetjmp = nullptr;
}
using CrtLongjmp = void(__cdecl*)(jmp_buf, int);
static CrtLongjmp crtLongjmp;
static jmp_buf crtSpEnv;

// Calls the inbox CRT setjmp with explicit SP, then returns through CRT longjmp.
// The R11 adapter remains exported for comparison but is deliberately not run:
// its crash was reproduced in package 1.0.0.10 and would terminate the host.
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

// Payload entry point for the C# host: truncates the log first so an empty
// file proves a pre-probe crash, then runs the unchanged probe sequence.
// Returns 0 when the sequence completes, 1 when the CRT symbols are missing.
extern "C" int __stdcall WKFpuProbeRun()
{
    HRESULT result = RoInitialize(RO_INIT_MULTITHREADED);
    logResult("fpuprobe-startup", result);
    if (FAILED(result))
        return 1;
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
            // Raw UTF-16 folder path used to create the LocalState log file.
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

    // Log before anything else can fail, so a truncated log tells us
    // whether the payload was ever entered.
    logResult("probe-run-entered", ProbeLogTrue);
    // The vectored-handler install itself appears to fault before it can log,
    // so bypass it for now. File logging flushes after every line, and the
    // probe sequence below is the signal we need.
    runCrtFrameProbe();
    runIntegerProbe();
    runProbe();
    logResult("fpuprobe-completed", ProbeLogTrue);
    RoUninitialize();
    return (wkProbeCrtSetjmp && crtLongjmp) ? 0 : 1;
}
