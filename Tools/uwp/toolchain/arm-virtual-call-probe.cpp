// Compile with clang-cl --target=armv7-unknown-windows-msvc /O2 /c and
// inspect with llvm-objdump -d --demangle. No SDK/runtime dependencies.
// Clang 19.1.5's MSVC ARM vcall thunk below loads the target into r1,
// overwriting the explicit location argument. The direct call preserves r1.
// Both probes share the same single-matrix call arguments.
constexpr int kSingleMatrixCount = 1;
constexpr unsigned char kDoNotTranspose = 0;

// Uniform-like receiver exposing the vtable member pointer under test.
struct UniformReceiver {
    // Virtual matrix upload signature used to inspect ARM thunk argument registers.
    virtual void setMatrix(int location, int count, unsigned char transpose, const float*) = 0;
};

// Emits the MSVC virtual member-function-pointer call thunk.
extern "C" __declspec(noinline) void probeVirtualMemberPointer(UniformReceiver* receiver, int location, const float* matrix)
{
    auto method = &UniformReceiver::setMatrix;
    (receiver->*method)(location, kSingleMatrixCount, kDoNotTranspose, matrix);
}

// Emits the direct virtual call as a control for the member-pointer thunk.
extern "C" __declspec(noinline) void probeDirectVirtualCall(UniformReceiver* receiver, int location, const float* matrix)
{
    receiver->setMatrix(location, kSingleMatrixCount, kDoNotTranspose, matrix);
}
