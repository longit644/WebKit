/*
 * EngineModuleW10M.cpp - consumer side of the frozen V1 WKW10M engine contract.
 *
 * Implements the binding, pinning and page-ownership rules documented in
 * EngineModuleW10M.h. The only operating-system calls here are the module
 * loader and the negotiated table itself; there is deliberately no WebCore,
 * WTF, STL or C++/CX dependency, so this translation unit stays compilable and
 * testable with both boundary compilers. See the header for why config.h is
 * absent here as well.
 */

#include "EngineModuleW10M.h"

#include <string.h>

namespace WebKit {

// Discovery entry signature, resolved by name so a provider that stops
// exporting it fails binding instead of failing to link.
typedef WKW10MResult(__stdcall* GetEngineAPIFunction)(uint32_t minimumMajor, uint32_t minimumMinor,
    uint64_t requiredCapabilities, WKW10MEngineAPI* outAPI);

// Unmangled discovery export name; any other spelling resolves nothing.
constexpr char kGetEngineAPIName[] = "WKW10MGetEngineAPI";

// The major version this consumer negotiates; a different major is a hard
// compatibility boundary, never a satisfiable request.
constexpr uint32_t kRequiredAbiMajor = WKW10M_ABI_MAJOR_VERSION;

// Zero is not a page: handles are process-unique nonzero identifiers.
constexpr WKW10MPageHandle kInvalidPageHandle = 0;

// Loads the engine module for this execution environment.
//
// Inside an AppContainer the desktop loader entry point is unavailable by
// design: only packaged modules may be loaded, through the packaged loader.
// The engine ships inside the app package, so that is exactly the right
// mechanism for the real consumer, while desktop test builds keep the ordinary
// loader. Both spellings return the same module handle contract.
#if defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_PC_APP)
HMODULE loadEngineModule(const wchar_t* modulePath)
{
    return LoadPackagedLibrary(modulePath, 0);
}
#else
HMODULE loadEngineModule(const wchar_t* modulePath)
{
    return LoadLibraryW(modulePath);
}
#endif

// See EngineModuleW10M.h for the ownership rules implemented below.
EngineModuleW10M::EngineModuleW10M()
    : m_module(nullptr)
    , m_bound(false)
    , m_shutdown(false)
    , m_pageCount(0)
{
    // The negotiated table must read as untouched until discovery fills it, so
    // an identity check can never pass on uninitialized bytes.
    memset(&m_api, 0, sizeof(m_api));
    memset(m_pages, 0, sizeof(m_pages));
}

// See EngineModuleW10M.h: the pin is process lifetime, so there is nothing to undo.
EngineModuleW10M::~EngineModuleW10M() = default;

// See EngineModuleW10M.h: a null slot would let a call jump nowhere.
bool EngineModuleW10M::everySlotAssigned(const WKW10MEngineAPI& api)
{
    return api.initialize && api.service && api.setSuspended && api.createPage && api.closePage
        && api.setCallbacks && api.detachCallbacks && api.navigate && api.loadHtml && api.goBack
        && api.goForward && api.reload && api.stop && api.getState && api.proceedWithCertificateFailure
        && api.sendInput && api.setFocus && api.resize && api.paint && api.attachCompositor
        && api.detachCompositor && api.renderCompositor && api.beginShutdown && api.pollShutdown;
}

// See EngineModuleW10M.h for the registry layout; linear search is exact here
// because the table is bounded and holds each owned handle once.
uint32_t EngineModuleW10M::findPage(WKW10MPageHandle page) const
{
    for (uint32_t index = 0; index < m_pageCount; ++index) {
        if (m_pages[index] == page)
            return index;
    }
    return m_pageCount;
}

// See EngineModuleW10M.h for the bind, pin and rejection contract.
WKW10MResult EngineModuleW10M::bind(const wchar_t* modulePath, uint32_t minimumMinor, uint64_t requiredCapabilities)
{
    // Binding twice is not a second load; the pin and the table are already held.
    if (m_bound)
        return WKW10MResultOK;

    // A null or empty path is the caller's own bug, reported before any load.
    if (!modulePath || !modulePath[0])
        return WKW10MResultInvalidArgument;

    HMODULE module = loadEngineModule(modulePath);
    if (!module) {
        // A missing or unloadable engine is an originating platform failure, so
        // it travels as a negative HRESULT unchanged, not as a contract code.
        HRESULT failure = HRESULT_FROM_WIN32(GetLastError());
        return static_cast<WKW10MResult>(static_cast<int32_t>(failure));
    }

    GetEngineAPIFunction getEngineAPI = reinterpret_cast<GetEngineAPIFunction>(
        reinterpret_cast<void*>(GetProcAddress(module, kGetEngineAPIName)));
    if (!getEngineAPI) {
        // A module without the discovery export is not a V1 engine at all; it
        // is released here because only a successful binding pins.
        FreeLibrary(module);
        return WKW10MResultAbiMismatch;
    }

    WKW10MEngineAPI negotiated;
    memset(&negotiated, 0, sizeof(negotiated));
    negotiated.header.structureSize = sizeof(negotiated);
    negotiated.header.structureVersion = WKW10M_STRUCTURE_VERSION;

    WKW10MResult result = getEngineAPI(kRequiredAbiMajor, minimumMinor, requiredCapabilities, &negotiated);
    if (result != WKW10MResultOK) {
        // The provider's own rejection is the answer; an incompatible engine is
        // never pinned, so its module is released before returning.
        FreeLibrary(module);
        return result;
    }

    // Discovery succeeding is not enough: the filled table must actually cover
    // every slot this object will call, name the frozen major, and hold no null
    // call. Anything less is an incompatible identity, rejected before the pin.
    if (negotiated.header.structureSize < sizeof(negotiated) || negotiated.abiMajor != kRequiredAbiMajor
        || !everySlotAssigned(negotiated)) {
        FreeLibrary(module);
        return WKW10MResultAbiMismatch;
    }

    // Committed: from here the module is never freed for the process lifetime.
    m_module = module;
    m_api = negotiated;
    m_bound = true;
    return WKW10MResultOK;
}

// See EngineModuleW10M.h: bound state is a single flag, never a handle guess.
bool EngineModuleW10M::isBound() const
{
    return m_bound;
}

// See EngineModuleW10M.h: the table is borrowed from the pinned module.
const WKW10MEngineAPI* EngineModuleW10M::api() const
{
    return m_bound ? &m_api : nullptr;
}

// See EngineModuleW10M.h: observation only, freeing it would break the pin.
HMODULE EngineModuleW10M::moduleHandle() const
{
    return m_bound ? m_module : nullptr;
}

// See EngineModuleW10M.h for the creation and registration contract.
WKW10MResult EngineModuleW10M::createPage(const WKW10MPageOptions* options, WKW10MPageHandle* page)
{
    if (!m_bound)
        return WKW10MResultNotInitialized;
    if (m_shutdown)
        return WKW10MResultShuttingDown;
    if (!options || !page)
        return WKW10MResultInvalidArgument;
    if (m_pageCount >= kEngineModuleW10MMaxOpenPages)
        return WKW10MResultInvalidState;

    WKW10MResult result = m_api.createPage(options, page);
    if (result != WKW10MResultOK)
        return result;

    // The contract promises a nonzero handle on success; a zero handle means
    // the provider broke its own promise, so nothing is registered.
    if (*page == kInvalidPageHandle)
        return WKW10MResultInvalidState;

    // The engine guarantees unique handles, so a duplicate would be its bug;
    // keeping a single registration still returns the baseline to zero later.
    if (findPage(*page) == m_pageCount)
        m_pages[m_pageCount++] = *page;
    return WKW10MResultOK;
}

// See EngineModuleW10M.h for the close and deregistration contract.
WKW10MResult EngineModuleW10M::closePage(WKW10MPageHandle page)
{
    if (!m_bound)
        return WKW10MResultNotInitialized;
    if (page == kInvalidPageHandle)
        return WKW10MResultInvalidArgument;

    uint32_t index = findPage(page);
    if (index == m_pageCount)
        return WKW10MResultInvalidArgument;

    WKW10MResult result = m_api.closePage(page);
    if (result == WKW10MResultOK || result == WKW10MResultClosed) {
        // The engine no longer owns the page in either case, so the
        // registration ends here; any other answer keeps it, because the page
        // still exists and a later close must still reach it.
        for (uint32_t move = index; move + 1 < m_pageCount; ++move)
            m_pages[move] = m_pages[move + 1];
        m_pages[--m_pageCount] = kInvalidPageHandle;
    }
    return result;
}

// See EngineModuleW10M.h: the close/recreate baseline is a zero page count.
uint32_t EngineModuleW10M::openPageCount() const
{
    return m_pageCount;
}

// See EngineModuleW10M.h for the callback installation contract.
WKW10MResult EngineModuleW10M::setPageCallbacks(WKW10MPageHandle page, const WKW10MPageCallbacks* callbacks)
{
    if (!m_bound)
        return WKW10MResultNotInitialized;
    if (page == kInvalidPageHandle || !callbacks)
        return WKW10MResultInvalidArgument;
    if (findPage(page) == m_pageCount)
        return WKW10MResultInvalidArgument;
    return m_api.setCallbacks(page, callbacks);
}

// See EngineModuleW10M.h for the detach acknowledgment contract.
WKW10MResult EngineModuleW10M::detachPageCallbacks(WKW10MPageHandle page)
{
    if (!m_bound)
        return WKW10MResultNotInitialized;
    if (page == kInvalidPageHandle)
        return WKW10MResultInvalidArgument;
    if (findPage(page) == m_pageCount)
        return WKW10MResultInvalidArgument;
    return m_api.detachCallbacks(page);
}

// See EngineModuleW10M.h: shutdown latches locally only on engine success.
WKW10MResult EngineModuleW10M::beginShutdown()
{
    if (!m_bound)
        return WKW10MResultNotInitialized;

    WKW10MResult result = m_api.beginShutdown();
    if (result == WKW10MResultOK)
        m_shutdown = true;
    return result;
}

// See EngineModuleW10M.h: progress is the engine's answer, passed through.
WKW10MResult EngineModuleW10M::pollShutdown()
{
    if (!m_bound)
        return WKW10MResultNotInitialized;
    return m_api.pollShutdown();
}

// See EngineModuleW10M.h: latched shutdown fails page creation fast.
bool EngineModuleW10M::isShuttingDown() const
{
    return m_shutdown;
}

} // namespace WebKit

