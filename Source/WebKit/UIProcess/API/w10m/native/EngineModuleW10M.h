/*
 * EngineModuleW10M.h - consumer side of the frozen V1 WKW10M engine contract:
 * module resolution, process-lifetime pinning and per-page ownership.
 *
 * This is M10 task 3.3 of
 * openspec/changes/wkview-m10-uwp-cppcx-restructure/tasks.md. It consumes the
 * table that Source/WebCore/platform/w10m/WKW10MEngineAPI.cpp provides, following
 * the negotiation rules of artifact/native-abi.md ("Exact engine function
 * table" and "Shutdown, dispatcher loss and module pinning"). The frozen
 * declarations in Source/WebKit/Shared/w10m are not repeated or reinterpreted
 * here; this file only names them.
 *
 * Ownership rules this class enforces, in contract order:
 *
 *   - The engine module is resolved with the platform loader plus GetProcAddress
 *     on the unmangled discovery name, never by linking against it, so a module
 *     that stops exporting the entry point fails binding instead of failing to
 *     link. Inside an AppContainer the packaged loader is used, because the
 *     desktop loader entry point is unavailable there by design and the engine
 *     ships inside the app package.
 *   - A rejected binding writes nothing observable and releases the module
 *     immediately: only a successful binding pins. After a successful binding
 *     the module is never freed for the process lifetime, which is what keeps
 *     WebCore.dll resident in M10; hot-unload is deferred, not promised.
 *   - Pinning the module is not permission to retain views. Every page this
 *     object creates is registered, every close removes exactly one
 *     registration, and handles this object never created are rejected before
 *     they can reach the engine.
 *   - After beginShutdown reports success, page creation fails locally with
 *     ShuttingDown instead of starting new engine work.
 *
 * This object is owner-thread affine: it takes no locks and must be used from
 * one thread, which matches the contract's owner-thread execution rule. It
 * allocates nothing and holds no engine-owned memory; the negotiated table and
 * the build identity it borrows stay valid because the module that owns them is
 * pinned by this same object.
 *
 * This header deliberately includes no WebCore configuration, WTF, STL or
 * C++/CX type, so the consumer can be compiled and exercised with both boundary
 * compilers exactly like the frozen ABI check, instead of only inside one
 * configured build tree. Its future target wires it with the usual settings.
 */

#ifndef ENGINE_MODULE_W10M_H
#define ENGINE_MODULE_W10M_H

#include "WKW10MEngine.h"
#include "WKW10MNavigation.h"
#include "WKW10MPage.h"

#include <windows.h>

namespace WebKit {

// Maximum pages one module tracks before it refuses to create another.
//
// The registry is a fixed array, not a heap container, because no cross-module
// allocation may cross this boundary and because a bounded table makes
// exhaustion an explicit InvalidState instead of an allocation failure.
constexpr uint32_t kEngineModuleW10MMaxOpenPages = 64;

// Consumer-side engine module binding with process-lifetime pinning.
//
// Non-copyable because two owners of one pin would disagree about shutdown and
// page counts; share the object instead of copying it.
class EngineModuleW10M {
public:
    // Builds an unbound module; binding is an explicit step so a failed
    // negotiation can be reported with its originating result.
    EngineModuleW10M();

    // Releases nothing. The engine module stays loaded for the process
    // lifetime once bound; per-page state is integers only, so there is no
    // heap to drain and no callback that could still fire.
    ~EngineModuleW10M();

    // Non-copyable owner of the module pin and the page registry.
    EngineModuleW10M(const EngineModuleW10M&) = delete;

    // Non-copyable owner of the module pin and the page registry.
    EngineModuleW10M& operator=(const EngineModuleW10M&) = delete;

    // Loads modulePath, resolves the discovery export and negotiates the table.
    //
    // minimumMinor and requiredCapabilities are passed straight to discovery;
    // the major version is frozen at 1 because a different major is a hard
    // compatibility boundary, never a satisfiable request. A failed call leaves
    // this object unbound and frees the module it just loaded; a successful
    // call pins the module and never frees it again. Binding twice without an
    // intervening failure is idempotent and performs no second load.
    WKW10MResult bind(const wchar_t* modulePath, uint32_t minimumMinor, uint64_t requiredCapabilities);

    // True after a successful bind; false after any rejection or before binding.
    bool isBound() const;

    // Borrowed negotiated table; null when unbound. Valid for the process
    // lifetime once bound, because the owning module is pinned by this object.
    const WKW10MEngineAPI* api() const;

    // Borrowed module handle for diagnostics; null when unbound. The handle is
    // for observation only: freeing it would break the process-lifetime pin.
    HMODULE moduleHandle() const;

    // Creates one page through the negotiated table and registers its handle.
    //
    // Fails NotInitialized when unbound, ShuttingDown once shutdown has begun,
    // InvalidArgument for a null option or output pointer, and InvalidState
    // when the fixed registry is full. An engine failure registers nothing, so
    // a rejected creation cannot leak a page count.
    WKW10MResult createPage(const WKW10MPageOptions* options, WKW10MPageHandle* page);

    // Closes a page this object created, removing exactly one registration.
    //
    // A zero handle or a handle this object never registered reports
    // InvalidArgument without reaching the engine. An engine OK or Closed
    // answer drops the registration, because the engine no longer owns the
    // page in either case; any other engine answer keeps it, because the page
    // still exists and a later close must still reach it.
    WKW10MResult closePage(WKW10MPageHandle page);

    // Number of pages this object currently owns; the close/recreate baseline.
    uint32_t openPageCount() const;

    // Installs a retained page callback table through the negotiated table.
    //
    // The engine copies the table and retains its context; this object keeps no
    // reference of its own. Fails NotInitialized when unbound, InvalidArgument
    // for a zero or unregistered page or a null table, and passes the engine's
    // answer through otherwise, including Pending and InvalidState.
    WKW10MResult setPageCallbacks(WKW10MPageHandle page, const WKW10MPageCallbacks* callbacks);

    // Deactivates a page's callback attachment through the negotiated table.
    //
    // When the engine answers Pending, the release acknowledging the detach
    // arrives after the active dispatch unwinds; this object's storage is the
    // caller's to free only after that release. Same validation as creation.
    WKW10MResult detachPageCallbacks(WKW10MPageHandle page);

    // Begins orderly engine shutdown and latches the shutdown state on success.
    //
    // The call is forwarded every time so the engine's own idempotence decides;
    // this object additionally fails fast locally once shutdown has begun. Fails
    // NotInitialized when unbound.
    WKW10MResult beginShutdown();

    // Forwards shutdown progress; fails NotInitialized when unbound.
    WKW10MResult pollShutdown();

    // True once beginShutdown has reported success; page creation then fails
    // locally instead of starting new engine work.
    bool isShuttingDown() const;

private:
    // The loaded engine module; null until a successful bind, never freed after.
    HMODULE m_module;

    // Negotiated table filled by discovery into this object's own storage.
    WKW10MEngineAPI m_api;

    // Whether bind has completed; only then are m_module and m_api usable.
    bool m_bound;

    // Latched once beginShutdown reports success; fails page creation fast.
    bool m_shutdown;

    // Handles this object created and has not yet closed, in creation order.
    WKW10MPageHandle m_pages[kEngineModuleW10MMaxOpenPages];

    // Entries of m_pages currently owned; the close/recreate baseline is zero.
    uint32_t m_pageCount;

    // True when every one of the 24 negotiated slots holds a callable address.
    //
    // A table with a null slot violates the contract's no-null-call rule, so a
    // provider that filled one is rejected as an incompatible identity.
    static bool everySlotAssigned(const WKW10MEngineAPI& api);

    // Position of page in the registry, or m_pageCount when absent.
    uint32_t findPage(WKW10MPageHandle page) const;
};

} // namespace WebKit

#endif // ENGINE_MODULE_W10M_H

