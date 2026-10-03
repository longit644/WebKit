// Load in an ARM-UWP host and call WKRunRuntimeSmoke on a background thread.
// Returns a failure bitmask; zero is success. No engine dependency, so CRT
// failures can be diagnosed before loading JavaScriptCore/WebCore.
#include <windows.h>
#include <process.h>
#include <stdint.h>

static volatile LONG globalConstructed;
static volatile LONG tlsConstructed;
static volatile LONG tlsDestroyed;
static volatile LONG staticConstructed;
static SRWLOCK smokeLock = SRWLOCK_INIT;

struct GlobalProbe {
    GlobalProbe() { InterlockedIncrement(&globalConstructed); }
};
static GlobalProbe globalProbe;

struct TLSProbe {
    unsigned value;
    TLSProbe() : value(73) { InterlockedIncrement(&tlsConstructed); }
    ~TLSProbe() { InterlockedIncrement(&tlsDestroyed); }
};
static thread_local TLSProbe tlsProbe;
static thread_local unsigned podProbe = 19;

struct StaticProbe {
    unsigned value;
    StaticProbe() : value(91)
    {
        InterlockedIncrement(&staticConstructed);
        Sleep(20); // Other threads should contend on the compiler guard.
    }
};
__declspec(noinline) static unsigned readStatic()
{
    static StaticProbe probe;
    return probe.value;
}

__declspec(noinline) static bool arithmetic()
{
    volatile long long signedValue = -100000000007LL;
    volatile long long signedDivisor = 97;
    if (signedValue / signedDivisor != -1030927835LL || signedValue % signedDivisor != -12)
        return false;
    volatile unsigned long long unsignedValue = 100000000007ULL;
    volatile unsigned long long unsignedDivisor = 97;
    if (unsignedValue / unsignedDivisor != 1030927835ULL || unsignedValue % unsignedDivisor != 12)
        return false;
    volatile int smallValue = -1007;
    volatile int divisor = 17;
    if (smallValue / divisor != -59 || smallValue % divisor != -4)
        return false;
    volatile unsigned smallUnsigned = 1007;
    volatile unsigned divisorUnsigned = 17;
    if (smallUnsigned / divisorUnsigned != 59 || smallUnsigned % divisorUnsigned != 4)
        return false;
    volatile float single = 4294967296.0f;
    volatile unsigned long long integer = 4294967296ULL;
    return static_cast<unsigned long long>(single) == integer && static_cast<float>(integer) == single;
}

struct ThreadProbe {
    HANDLE start;
    unsigned value;
    unsigned failures;
};
static unsigned __stdcall worker(void* context)
{
    auto& probe = *static_cast<ThreadProbe*>(context);
    WaitForSingleObjectEx(probe.start, INFINITE, FALSE);
    if (tlsProbe.value != 73 || podProbe != 19)
        probe.failures |= 2;
    tlsProbe.value = probe.value;
    podProbe = probe.value + 1;
    if (readStatic() != 91)
        probe.failures |= 4;
    Sleep(10);
    if (tlsProbe.value != probe.value || podProbe != probe.value + 1)
        probe.failures |= 8;
    if (!arithmetic())
        probe.failures |= 16;
    return 0;
}

extern "C" __declspec(dllexport) unsigned __stdcall WKRunRuntimeSmoke()
{
    AcquireSRWLockExclusive(&smokeLock);
    unsigned failures = globalConstructed == 1 ? 0 : 1;
    LONG constructedBefore = tlsConstructed;
    LONG destroyedBefore = tlsDestroyed;
    HANDLE start = CreateEventExW(nullptr, nullptr, CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS);
    if (!start) {
        ReleaseSRWLockExclusive(&smokeLock);
        return failures | 32;
    }
    ThreadProbe probes[4] = { };
    HANDLE threads[4] = { };
    unsigned created = 0;
    for (unsigned i = 0; i < 4; ++i) {
        probes[i] = { start, i + 100, 0 };
        threads[i] = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, worker, &probes[i], 0, nullptr));
        if (!threads[i]) {
            failures |= 32;
            break;
        }
        ++created;
    }
    SetEvent(start);
    for (unsigned i = 0; i < created; ++i) {
        // The thread callback owns stack-backed probe data: join before return.
        WaitForSingleObjectEx(threads[i], INFINITE, FALSE);
        CloseHandle(threads[i]);
        failures |= probes[i].failures;
    }
    CloseHandle(start);
    if (tlsConstructed - constructedBefore != static_cast<LONG>(created)
        || tlsDestroyed - destroyedBefore != static_cast<LONG>(created))
        failures |= 64;
    if (staticConstructed != 1)
        failures |= 128;
    ReleaseSRWLockExclusive(&smokeLock);
    return failures;
}
