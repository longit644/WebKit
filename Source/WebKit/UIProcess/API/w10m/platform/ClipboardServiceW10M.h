#pragma once

#include "WKW10MEngine.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <windows.h>

namespace WebKit {

// Clipboard access kind mirroring the frozen V1 values.
enum class ClipboardKindW10M : uint32_t {
    // Read system clipboard text into an owned buffer.
    Read = WKW10MClipboardRead,
    // Publish copied UTF-8 bytes to the system clipboard.
    Write = WKW10MClipboardWrite
};

// UI-thread text transport behind the service. Write completes synchronously;
// read invokes its completion exactly once on an unspecified thread, so the
// service marshals delivery back to the owner.
class IClipboardTransportW10M {
public:
    // Publish UTF-8 text; the transport copies everything it retains.
    virtual HRESULT writeText(const std::string& utf8) = 0;
    // Begin an asynchronous read; invoke done exactly once with UTF-8 bytes.
    // Report S_OK or a negative HRESULT only: positive codes collide with
    // contract results, so an empty clipboard completes S_OK with no bytes.
    virtual void readText(std::function<void(HRESULT, std::string)> done) = 0;
    // Destroy through the concrete transport; never delete via a pending call.
    virtual ~IClipboardTransportW10M() = default;
};

// Owner-thread task hop behind completion delivery.
class IServiceDispatcherW10M {
public:
    // Report whether the caller runs on the owning dispatcher thread.
    virtual bool hasThreadAccess() = 0;
    // Enqueue one task; false enters the dropped-enqueue count, never a callback.
    virtual bool post(std::function<void()>&& task) = 0;
    // Destroy through the concrete dispatcher.
    virtual ~IServiceDispatcherW10M() = default;
};

// Host-side V1 clipboard service with neutral callbacks and no app objects.
// Exactly-once completion holds across acceptance, transport failure and
// cancellation; a canceled operation completes Closed synchronously. Pending
// operation IDs are unique; reuse after completion is allowed. Transports must
// not throw: this domain compiles without exception unwinding on ARM.
class ClipboardServiceW10M {
public:
    // Bind borrowed dispatcher/transport ports owned by the service owner.
    ClipboardServiceW10M(std::unique_ptr<IServiceDispatcherW10M>&& dispatcher,
        std::unique_ptr<IClipboardTransportW10M>&& transport);
    // Service state has exactly one owner.
    ClipboardServiceW10M(const ClipboardServiceW10M&) = delete;
    // Prevent duplicated operation registries.
    ClipboardServiceW10M& operator=(const ClipboardServiceW10M&) = delete;
    // Pending operations are abandoned; in-flight transport completions are ignored.
    ~ClipboardServiceW10M();

    // Accept one validated request; reads complete through the owner dispatcher.
    HRESULT requestClipboard(WKW10MPageHandle page, uint64_t operationId, uint32_t kind,
        const WKW10MString* text, WKW10MClipboardCompletion completion, void* completionContext);
    // Cancel one accepted request with a single synchronous Closed completion.
    HRESULT cancelClipboard(uint64_t operationId);
    // Fill a borrowed platform-services table; the caller owns this instance (see 7.5).
    HRESULT buildTable(WKW10MPlatformServices* table);
    // Check owner affinity before any registry or provider access.
    HRESULT checkThread() const;
    // Number of operations still awaiting completion.
    unsigned pendingCount() const;
    // Transport completions dropped for lack of a dispatcher enqueue.
    unsigned droppedEnqueues() const;
    // Current context retain count for lifetime verification.
    unsigned contextRefCount() const;

private:
    // One accepted operation; erased before its single completion runs.
    struct Operation {
        // Owning page recorded for diagnostics; never dereferenced here.
        WKW10MPageHandle page { 0 };
        // Frozen access kind for transport routing.
        uint32_t kind { 0 };
        // Engine completion invoked exactly once with a borrowed text span.
        WKW10MClipboardCompletion completion { nullptr };
        // Engine context retained by the engine until that completion returns.
        void* completionContext { nullptr };
    };

    // Complete and erase one record on the owner thread; ignored when stale.
    HRESULT completeOperation(const std::shared_ptr<Operation>& operation, uint64_t operationId,
        WKW10MResult result, const std::string& text);
    // Retain the borrowed table context once per successful initialize (see 7.5).
    void retain();
    // Release a previous retain; the owner destroys this instance, never self-delete.
    void release();
    // Route the C table entries to this instance; retain/release return void per contract.
    static void __stdcall retainThunk(void* context);
    // Release routing for the C table; a null context is ignored.
    static void __stdcall releaseThunk(void* context);
    // Request routing for the C table.
    static WKW10MResult __stdcall requestThunk(void* context, WKW10MPageHandle page, uint64_t operationId,
        uint32_t kind, const WKW10MString* text, WKW10MClipboardCompletion completion, void* completionContext);
    // Cancel routing for the C table.
    static WKW10MResult __stdcall cancelThunk(void* context, uint64_t operationId);

    // Guards the registry against worker-thread transport completions.
    mutable std::mutex m_lock;
    // Owner thread for all registry mutation and completion delivery.
    DWORD m_owner { GetCurrentThreadId() };
    // Borrowed completion hop to the owner thread.
    std::unique_ptr<IServiceDispatcherW10M> m_dispatcher;
    // Borrowed UI-thread text transport.
    std::unique_ptr<IClipboardTransportW10M> m_transport;
    // Accepted operations awaiting their single completion, by operation ID.
    std::map<uint64_t, std::shared_ptr<Operation>> m_operations;
    // Failed dispatcher enqueues that left no delayed callback behind.
    unsigned m_droppedEnqueues { 0 };
    // Borrowed-table context retains; destroyed with this instance.
    unsigned m_contextRefs { 1 };
};
} // namespace WebKit

