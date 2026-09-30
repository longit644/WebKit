// WebKitView RTTI stubs for ARM32 Store CRT (v0 semantics, DOCUMENTED).
// MSVC 14.4x ships no native ARM32 vcruntime: __RTDynamicCast and the
// type_info vftable exist only in msvcurt.lib's MANAGED ti_inst.obj, which
// lld-link cannot parse ("should not refer to special section 0").
// Everything here is /GR- (no RTTI) builds only:
//  - __RTDynamicCast always fails: pointer casts yield nullptr (callers use
//    the fallback path), reference casts make the compiler throw bad_cast.
//    ICU call sites audited: they null-check or take documented fallbacks.
//  - ??_7type_info@@6B@ (type_info's own vftable) aliases a dummy; it is only
//    ever passed through to __RTDynamicCast, never dereferenced, because the
//    stub above ignores its arguments. Linked via
//    /ALTERNATENAME:??_7type_info@@6B@=__wk_typeinfo_dummy.
// Revisit with a real vcruntime if one ever ships for ARM32 Store.
#include <windows.h>

void* __RTDynamicCast(void* inptr, long vfDelta, void* srcType, void* targetType, int isReference)
{
    (void)inptr; (void)vfDelta; (void)srcType; (void)targetType; (void)isReference;
    return 0;
}
// NOTE: ??_7type_info@@6B@ lives in crtvft.S (quoted asm label; C cannot
// spell it). An earlier /ALTERNATENAME approach was dropped: resolving the
// alias forced lld to scan msvcurt's managed ti_inst.obj, which it cannot
// parse.
