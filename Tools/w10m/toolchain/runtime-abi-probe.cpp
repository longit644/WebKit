// Compile to LLVM IR/assembly to check the ARM compiler-helper contracts.
// Each probe emits one compiler/runtime feature for inspection.

// Nontrivial object whose static and TLS lifetime helpers are inspected.
struct ProbeObject {
    // Out-of-line constructor forces guarded initialization code generation.
    ProbeObject();

    // Out-of-line destructor forces TLS destructor registration code generation.
    ~ProbeObject();

    // Observable payload returned by each probe.
    int value;
};

// Thread-local object forces the ARM PE TLS access path.
thread_local ProbeObject probeObject;

// Forces function-local static guard and destructor registration helpers.
int probeStatic()
{
    static ProbeObject object;
    return object.value;
}

// Forces compiler-generated thread-local access helpers.
int probeTLS() { return probeObject.value; }

// Forces float-to-unsigned-64 conversion helper emission.
unsigned long long probeFloatToUnsigned(float value) { return static_cast<unsigned long long>(value); }

// Forces unsigned-64-to-float conversion helper emission.
float probeUnsignedToFloat(unsigned long long value) { return static_cast<float>(value); }

// Forces signed 64-bit division helper emission.
long long probeSignedDivide(long long lhs, long long rhs) { return lhs / rhs; }

// Forces signed 64-bit remainder helper emission.
long long probeSignedRemainder(long long lhs, long long rhs) { return lhs % rhs; }
