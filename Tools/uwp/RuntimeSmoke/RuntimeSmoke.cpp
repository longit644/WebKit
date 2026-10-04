// Load in an ARM-UWP host and call WKRunRuntimeSmoke on a background thread.
// Returns a failure bitmask; zero is success. No engine dependency, so CRT
// failures can be diagnosed before loading JavaScriptCore/WebCore.
#include <process.h>
#include <stdint.h>
#include <windows.h>

static volatile LONG globalConstructed;
static volatile LONG tlsConstructed;
static volatile LONG tlsDestroyed;
static volatile LONG staticConstructed;
static SRWLOCK smokeLock = SRWLOCK_INIT;

// Deterministic CRT canaries and worker schedule for the runtime test.
constexpr unsigned kInitialTlsValue = 73;
constexpr unsigned kInitialPodTlsValue = 19;
constexpr unsigned kFunctionStaticValue = 91;
constexpr unsigned kWorkerCount = 4;
constexpr unsigned kWorkerIdBase = 100;
constexpr LONG kExpectedSingleConstruction = 1;
constexpr unsigned kNoWorkerFailures = 0;
constexpr unsigned kWorkerThreadSuccess = 0;
constexpr DWORD kStaticGuardContentionDelayMS = 20;
constexpr DWORD kWorkerTlsWritebackDelayMS = 10;

// Failure bitmask reported by WKRunRuntimeSmoke; zero is success.
enum RuntimeSmokeFailure {
    // Global constructor ran other than exactly once.
    RuntimeSmokeFailureGlobalInit = 1,

    // Thread-local canaries did not read back.
    RuntimeSmokeFailureTlsCanary = 2,

    // Function-local static did not initialize once.
    RuntimeSmokeFailureStaticInit = 4,

    // Thread-local writeback did not stick.
    RuntimeSmokeFailureTlsWriteback = 8,

    // CRT integer/float arithmetic miscomputed.
    RuntimeSmokeFailureArithmetic = 16,

    // Worker threads or the start event failed to spawn.
    RuntimeSmokeFailureThreadSpawn = 32,

    // Thread-local construction/destruction counts mismatch.
    RuntimeSmokeFailureTlsLifetime = 64,

    // Function-local static constructed other than exactly once.
    RuntimeSmokeFailureStaticOnce = 128
};

// Counts global constructions; must read exactly one.
struct GlobalProbe {
    GlobalProbe() { InterlockedIncrement(&globalConstructed); }
};

static GlobalProbe globalProbe;

// Thread-local canary plus a plain POD canary; both reset per worker thread.
struct TLSProbe {
    unsigned value;
    TLSProbe()
        : value(kInitialTlsValue)
    {
        InterlockedIncrement(&tlsConstructed);
    }
    ~TLSProbe() { InterlockedIncrement(&tlsDestroyed); }
};
static thread_local TLSProbe tlsProbe;
static thread_local unsigned podProbe = kInitialPodTlsValue;

// Function-local static canary; the delay makes workers contend on its init guard.
struct StaticProbe {
    unsigned value;
    StaticProbe()
        : value(kFunctionStaticValue)
    {
        InterlockedIncrement(&staticConstructed);
        Sleep(kStaticGuardContentionDelayMS);
    }
};

// Reads the function-local static canary, triggering guarded init once.
__declspec(noinline) static unsigned readStatic()
{
    static StaticProbe probe;
    return probe.value;
}

// Verifies 64-bit division, modulo and float/integer round-trips against fixed expectations.
__declspec(noinline) static bool arithmetic()
{
    constexpr long long kSignedDividend = -100000000007LL;
    constexpr long long kLargeDivisor = 97;
    constexpr long long kSignedQuotient = -1030927835LL;
    constexpr long long kSignedRemainder = -12;
    volatile long long signedValue = kSignedDividend;
    volatile long long signedDivisor = kLargeDivisor;
    if (signedValue / signedDivisor != kSignedQuotient || signedValue % signedDivisor != kSignedRemainder)
        return false;
    constexpr unsigned long long kUnsignedDividend = 100000000007ULL;
    constexpr unsigned long long kUnsignedQuotient = 1030927835ULL;
    constexpr unsigned long long kUnsignedRemainder = 12;
    volatile unsigned long long unsignedValue = kUnsignedDividend;
    volatile unsigned long long unsignedDivisor = kLargeDivisor;
    if (unsignedValue / unsignedDivisor != kUnsignedQuotient || unsignedValue % unsignedDivisor != kUnsignedRemainder)
        return false;
    constexpr int kSmallSignedDividend = -1007;
    constexpr int kSmallDivisor = 17;
    constexpr int kSmallSignedQuotient = -59;
    constexpr int kSmallSignedRemainder = -4;
    volatile int smallValue = kSmallSignedDividend;
    volatile int divisor = kSmallDivisor;
    if (smallValue / divisor != kSmallSignedQuotient || smallValue % divisor != kSmallSignedRemainder)
        return false;
    constexpr unsigned kSmallUnsignedDividend = 1007;
    constexpr unsigned kSmallUnsignedQuotient = 59;
    constexpr unsigned kSmallUnsignedRemainder = 4;
    volatile unsigned smallUnsigned = kSmallUnsignedDividend;
    volatile unsigned divisorUnsigned = kSmallDivisor;
    if (smallUnsigned / divisorUnsigned != kSmallUnsignedQuotient || smallUnsigned % divisorUnsigned != kSmallUnsignedRemainder)
        return false;
    constexpr float kExactlyRepresentableFloat = 4294967296.0f;
    constexpr unsigned long long kExactlyRepresentableInteger = 4294967296ULL;
    volatile float single = kExactlyRepresentableFloat;
    volatile unsigned long long integer = kExactlyRepresentableInteger;
    return static_cast<unsigned long long>(single) == integer && static_cast<float>(integer) == single;
}

// Worker parameters and per-thread failure bits for the smoke run.
struct ThreadProbe {
    // Start event gating all workers to race static init.
    HANDLE start;

    // Distinct canary written to TLS by this worker.
    unsigned value;

    // RuntimeSmokeFailure bits recorded by this worker.
    unsigned failures;
};

// Worker body exercising TLS isolation and guarded static initialization.
static unsigned __stdcall worker(void* context)
{
    auto& probe = *static_cast<ThreadProbe*>(context);
    WaitForSingleObjectEx(probe.start, INFINITE, FALSE);
    if (tlsProbe.value != kInitialTlsValue || podProbe != kInitialPodTlsValue)
        probe.failures |= RuntimeSmokeFailureTlsCanary;
    tlsProbe.value = probe.value;
    podProbe = probe.value + 1;
    if (readStatic() != kFunctionStaticValue)
        probe.failures |= RuntimeSmokeFailureStaticInit;
    Sleep(kWorkerTlsWritebackDelayMS);
    if (tlsProbe.value != probe.value || podProbe != probe.value + 1)
        probe.failures |= RuntimeSmokeFailureTlsWriteback;
    if (!arithmetic())
        probe.failures |= RuntimeSmokeFailureArithmetic;
    return kWorkerThreadSuccess;
}

// Runs the CRT smoke suite on racing workers; returns the failure bitmask.
extern "C" __declspec(dllexport) unsigned __stdcall WKRunRuntimeSmoke()
{
    AcquireSRWLockExclusive(&smokeLock);
    unsigned failures = globalConstructed == kExpectedSingleConstruction ? kNoWorkerFailures : RuntimeSmokeFailureGlobalInit;
    LONG constructedBefore = tlsConstructed;
    LONG destroyedBefore = tlsDestroyed;
    HANDLE start = CreateEventExW(nullptr, nullptr, CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS);
    if (!start) {
        ReleaseSRWLockExclusive(&smokeLock);
        return failures | RuntimeSmokeFailureThreadSpawn;
    }
    ThreadProbe probes[kWorkerCount] = {};
    HANDLE threads[kWorkerCount] = {};
    unsigned created = kNoWorkerFailures;
    for (unsigned i = 0; i < kWorkerCount; ++i) {
        probes[i] = { start, i + kWorkerIdBase, kNoWorkerFailures };
        threads[i] = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, worker, &probes[i], 0, nullptr));
        if (!threads[i]) {
            failures |= RuntimeSmokeFailureThreadSpawn;
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
        failures |= RuntimeSmokeFailureTlsLifetime;
    if (staticConstructed != kExpectedSingleConstruction)
        failures |= RuntimeSmokeFailureStaticOnce;
    ReleaseSRWLockExclusive(&smokeLock);
    return failures;
}
