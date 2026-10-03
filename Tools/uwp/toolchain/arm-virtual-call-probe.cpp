// Compile with clang-cl --target=armv7-unknown-windows-msvc /O2 /c and
// inspect with llvm-objdump -d --demangle. No SDK/runtime dependencies.
// Clang 19.1.5's MSVC ARM vcall thunk below loads the target into r1,
// overwriting the explicit location argument. The direct call preserves r1.
struct UniformReceiver {
    virtual void setMatrix(int location, int count, unsigned char transpose, const float*) = 0;
};

extern "C" __declspec(noinline) void probeVirtualMemberPointer(UniformReceiver* receiver, int location, const float* matrix)
{
    auto method = &UniformReceiver::setMatrix;
    (receiver->*method)(location, 1, 0, matrix);
}

extern "C" __declspec(noinline) void probeDirectVirtualCall(UniformReceiver* receiver, int location, const float* matrix)
{
    receiver->setMatrix(location, 1, 0, matrix);
}
