// Host-runnable arithmetic proof for the FpuProbe payload (M10 task 8.6).
// Compiles as plain x64 C++ with MSVC; it exercises the same constants and
// the same volatile/noinline operation shapes as FpuProbePayload, so the
// expected report values (8000/6000/30000, integer 7/9) are proven runnable on
// the host. ARM ABI behavior (frame variants, FPSCR) is verified separately by
// inspecting the ARM payload DLL, never by executing it here.
#include "FpuProbePayload.h"

#include <setjmp.h>
#include <stdio.h>

// No-inline floating operation mirrors the payload's folding guard.
__declspec(noinline) static double checkMultiply(double a, double b)
{
    return a * b;
}

static jmp_buf checkEnv;
static jmp_buf checkIntEnv;

static int checkFailures = 0;

// Prints one actual-vs-expected line and records any mismatch.
static void checkLine(const char* stage, unsigned long actual, unsigned long expected)
{
    printf("%s: 0x%08lx (expected 0x%08lx) %s\n", stage, actual, expected,
        actual == expected ? "PASS" : "FAIL");
    if (actual != expected)
        ++checkFailures;
}

// Entry point: proves the shared payload constants and arithmetic on the host.
int main()
{
    // Compile-time proof that the shared constants match the inventory.
    static_assert(kInitialSetjmpReturn == 0, "initial setjmp return");
    static_assert(kLongjmpReturnValue == 7, "FP longjmp value");
    static_assert(kFpscrTrapEnableMask == 0x1f00, "FPSCR trap mask");
    static_assert(kFloatingResultScale == 1000, "result scale");
    static_assert(kCrtProbeReturn == 11, "CRT probe return");
    static_assert(kIntProbeReturn == 9, "integer probe return");
    static_assert(kExpectedBaselineX1000 == 8000, "baseline expectation");
    static_assert(kExpectedInsideX1000 == 6000, "inside expectation");
    static_assert(kExpectedFinalX1000 == 30000, "final expectation");
    static_assert(kExpectedIntegerBeforeLongjmp == 7, "integer expectation");

    checkLine("probe-baseline-multiply",
        static_cast<unsigned long>(checkMultiply(kBaselineLeftOperand, kBaselineRightOperand) * kFloatingResultScale),
        kExpectedBaselineX1000);

    volatile double value = kInitialFloatingValue;
    value = checkMultiply(value, kInitialFloatingMultiplier);
    int round = setjmp(checkEnv);
    if (round == kInitialSetjmpReturn) {
        value = checkMultiply(value, kPreJumpFloatingMultiplier);
        checkLine("probe-inside-before-longjmp",
            static_cast<unsigned long>(value * kFloatingResultScale),
            kExpectedInsideX1000);
        longjmp(checkEnv, kLongjmpReturnValue);
    }
    checkLine("probe-after-longjmp", static_cast<unsigned long>(round),
        static_cast<unsigned long>(kLongjmpReturnValue));
    value = checkMultiply(value, kPostJumpFloatingMultiplier);
    checkLine("probe-fp-result-x1000",
        static_cast<unsigned long>(value * kFloatingResultScale),
        kExpectedFinalX1000);

    volatile int intValue = kInitialIntegerValue;
    intValue += kBeforeJumpIncrement;
    int intRound = setjmp(checkIntEnv);
    if (intRound == kInitialSetjmpReturn) {
        intValue += kInsideJumpIncrement;
        checkLine("probe-int-before-longjmp", static_cast<unsigned long>(intValue),
            static_cast<unsigned long>(kExpectedIntegerBeforeLongjmp));
        longjmp(checkIntEnv, kIntProbeReturn);
    }
    checkLine("probe-int-after-longjmp", static_cast<unsigned long>(intRound),
        static_cast<unsigned long>(kIntProbeReturn));
    checkLine("probe-int-value", static_cast<unsigned long>(intValue),
        static_cast<unsigned long>(kExpectedIntegerBeforeLongjmp));

    printf("fpuprobe-host-check: %s (%d failures)\n", checkFailures == 0 ? "PASS" : "FAIL", checkFailures);
    return checkFailures == 0 ? 0 : 1;
}
