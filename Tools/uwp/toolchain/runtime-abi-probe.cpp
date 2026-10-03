// Compile to LLVM IR/assembly to check the ARM compiler-helper contracts.
struct ProbeObject {
    ProbeObject();
    ~ProbeObject();
    int value;
};
thread_local ProbeObject probeObject;
int probeStatic()
{
    static ProbeObject object;
    return object.value;
}
int probeTLS() { return probeObject.value; }
unsigned long long probeFloatToUnsigned(float value) { return static_cast<unsigned long long>(value); }
float probeUnsignedToFloat(unsigned long long value) { return static_cast<float>(value); }
long long probeSignedDivide(long long lhs, long long rhs) { return lhs / rhs; }
long long probeSignedRemainder(long long lhs, long long rhs) { return lhs % rhs; }
