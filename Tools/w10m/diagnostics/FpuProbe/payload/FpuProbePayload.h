// Native FPU/ARM probe payload contract (M10 task 8.6).
// The application host is C#; this payload stays native and behavior-identical
// to the historical probe sequence from Tools/uwp/FpuProbe/FpuProbe.cpp.
// That source now lives beside this header as payload/FpuProbe.cpp. Only the
// host (Application/activation/window/event loop) moved to C#.
// Retained verbatim: all arithmetic constants below, the CrtFrameProbe.S frame-
// argument variants (wkProbeSetjmpSp with SP, wkProbeSetjmpR11 with R11), the
// wkProbe* names, and the "stage: 0x%08lx" report protocol written to
// LocalState\fpuprobe.log. Retired with the WRL host: the CoreApplication::Run
// event loop, window activation lines (probe-activate-hresult,
// probe-skipping-fault-handler) and ExitProcess; the C# host now owns lifetime
// and reads back the same log file.
#pragma once

#include <setjmp.h>

// Boolean values written to probe log fields.
enum ProbeLogBoolean {
    // Probe condition was false.
    ProbeLogFalse = 0,

    // Probe condition was true.
    ProbeLogTrue = 1
};

// Initial setjmp return distinguishing first entry from a longjmp landing.
constexpr int kInitialSetjmpReturn = 0;
// Arbitrary nonzero FP second-return value proving the longjmp landed.
constexpr int kLongjmpReturnValue = 7;
// FPSCR floating-point trap-enable bits (IDE/IXE/UFE/OFE/DZE/IOE).
constexpr unsigned long kFpscrTrapEnableMask = 0x1f00;
// Left operand of the no-longjmp FP baseline.
constexpr double kBaselineLeftOperand = 2.0;
// Right operand of the no-longjmp FP baseline.
constexpr double kBaselineRightOperand = 4.0;
// FP value carried into the setjmp round trip.
constexpr double kInitialFloatingValue = 1.0;
// Multiplier applied before setjmp.
constexpr double kInitialFloatingMultiplier = 2.0;
// Multiplier applied inside the jump, before longjmp.
constexpr double kPreJumpFloatingMultiplier = 3.0;
// Multiplier applied after the jump, proving FP still works.
constexpr double kPostJumpFloatingMultiplier = 5.0;
// Scale keeping report values integral in the log protocol.
constexpr unsigned long kFloatingResultScale = 1000;
// Priority of the diagnostic vectored exception handler.
constexpr unsigned long kVectoredHandlerFirstPriority = 1;

// Arbitrary nonzero CRT second-return value proving the longjmp landed.
constexpr int kCrtProbeReturn = 11;
// Arbitrary nonzero integer second-return value proving the longjmp landed.
constexpr int kIntProbeReturn = 9;

// Integer probe start value.
constexpr int kInitialIntegerValue = 1;
// Integer increment applied before setjmp.
constexpr int kBeforeJumpIncrement = 2;
// Integer increment applied inside the jump.
constexpr int kInsideJumpIncrement = 4;

// Baseline expectation: 2.0*4.0*1000 = 8000 (0x1F40).
constexpr unsigned long kExpectedBaselineX1000 = 8000;
// Inside expectation: 2.0*3.0*1000 = 6000 (0x1770).
constexpr unsigned long kExpectedInsideX1000 = 6000;
// Final expectation: 6.0*5.0*1000 = 30000 (0x7530).
constexpr unsigned long kExpectedFinalX1000 = 30000;
// Integer expectation: 1+2+4 = 7.
constexpr int kExpectedIntegerBeforeLongjmp = 7;

extern "C" {
// Optional inbox CRT setjmp symbol resolved at runtime.
extern void* wkProbeCrtSetjmp;

// MSVC has no returns_twice spelling; clang needs it for the probe adapters.
#ifdef _MSC_VER
#define WK_PROBE_RETURNS_TWICE
#else
#define WK_PROBE_RETURNS_TWICE __attribute__((returns_twice))
#endif

// Calls the CRT setjmp adapter with SP passed as its hidden frame argument.
WK_PROBE_RETURNS_TWICE int wkProbeSetjmpSp(jmp_buf);

// Historical comparison adapter passing R11 as the hidden frame argument.
WK_PROBE_RETURNS_TWICE int wkProbeSetjmpR11(jmp_buf);

// Runs the full probe sequence (CRT frame, integer, floating-point) on the
// calling thread and appends every report line to LocalState\fpuprobe.log.
// Returns 0 when the sequence completes, 1 when the CRT symbols are missing.
int __stdcall WKFpuProbeRun();
}
