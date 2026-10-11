#include "RuntimeOwnerW10M.h"

#include <vector>

namespace WebKit {

// Own the pipeline for one page; ports move into the owned service.
RuntimeOwnerW10M::RuntimeOwnerW10M(WKW10MPageHandle page, std::unique_ptr<IServiceDispatcherW10M>&& dispatcher,
    std::unique_ptr<IClipboardTransportW10M>&& transport)
    : m_page(page)
{
    createClipboard(std::move(dispatcher), std::move(transport));
}

// Shut down first (every accepted completion acknowledged), then abandon.
RuntimeOwnerW10M::~RuntimeOwnerW10M()
{
    beginShutdown();
}

// Enforce owning-thread access before touching the registry or any provider.
HRESULT RuntimeOwnerW10M::checkThread() const
{
    return GetCurrentThreadId() == m_owner ? S_OK : RPC_E_WRONG_THREAD;
}

// Publish copied bytes; each acceptance opens one ticket.
HRESULT RuntimeOwnerW10M::copy(const WKW10MString* text, ClipboardRouteCompletion completion, void* context)
{
    return issue(false, false, text, completion, context);
}

// Publish cut bytes with the cut marker; selection deletion is engine work.
HRESULT RuntimeOwnerW10M::cut(const WKW10MString* text, ClipboardRouteCompletion completion, void* context)
{
    return issue(false, true, text, completion, context);
}

// Read system clipboard text; completion carries the ticket's operation ID.
HRESULT RuntimeOwnerW10M::paste(ClipboardRouteCompletion completion, void* context)
{
    return issue(true, false, nullptr, completion, context);
}

// Cancel every pending route operation; each ticket is acknowledged Closed.
HRESULT RuntimeOwnerW10M::closeClipboard()
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (!m_route)
        return E_INVALIDARG;
    // Each cancel runs the route thunk synchronously, which acknowledges the
    // ticket before the next operation is canceled.
    return m_route->close();
}

// Close, then replace the service and route; totals carry, pending resets.
HRESULT RuntimeOwnerW10M::recreateClipboard(std::unique_ptr<IServiceDispatcherW10M>&& dispatcher,
    std::unique_ptr<IClipboardTransportW10M>&& transport)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_shuttingDown)
            return RO_E_CLOSED;
    }
    HRESULT closed = closeClipboard();
    if (FAILED(closed))
        return closed;
    if (m_serviceView)
        m_droppedCarried += m_serviceView->droppedEnqueues();
    createClipboard(std::move(dispatcher), std::move(transport));
    {
        std::lock_guard<std::mutex> lock(m_lock);
        ++m_generation;
    }
    return S_OK;
}

// Fill the engine-facing table with owner context and a working postWake.
HRESULT RuntimeOwnerW10M::buildTable(WKW10MPlatformServices* table)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (!table)
        return E_INVALIDARG;
    table->header = { sizeof(*table), WKW10M_STRUCTURE_VERSION };
    table->context = this;
    table->retainContext = &retainTableThunk;
    table->releaseContext = &releaseTableThunk;
    table->requestClipboard = &requestTableThunk;
    table->cancelClipboard = &cancelTableThunk;
    table->postWake = &postWakeTableThunk;
    return S_OK;
}

// Enqueue one owner wake; DispatcherUnavailable when lost or refused.
// Callable from any thread: waking the owner must never need the owner.
WKW10MResult RuntimeOwnerW10M::postWake()
{
    std::lock_guard<std::mutex> lock(m_lock);
    if (m_dispatcherLost || !m_wakePort)
        return WKW10MResultDispatcherUnavailable;
    return m_wakePort->post([] { }) ? WKW10MResultOK : WKW10MResultDispatcherUnavailable;
}

// Register one shutdown hook; revocation acknowledges it Closed exactly once.
HRESULT RuntimeOwnerW10M::registerHook(RuntimeHookCallback callback, void* context, uint64_t* hookId)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (!callback || !hookId)
        return E_INVALIDARG;
    auto hook = std::make_shared<Hook>();
    hook->callback = callback;
    hook->callerContext = context;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_shuttingDown)
            return RO_E_CLOSED;
        *hookId = m_nextHookId++;
        m_hooks.emplace(*hookId, hook);
    }
    return S_OK;
}

// Revoke one hook now with a single Closed acknowledgment.
HRESULT RuntimeOwnerW10M::revokeHook(uint64_t hookId)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    std::shared_ptr<Hook> owned;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        auto found = m_hooks.find(hookId);
        if (found == m_hooks.end())
            return E_INVALIDARG;
        owned = found->second;
        m_hooks.erase(found);
    }
    owned->callback(owned->callerContext, hookId, WKW10MResultClosed);
    std::lock_guard<std::mutex> lock(m_lock);
    ++m_hooksRevokedTotal;
    return S_OK;
}

// Latch shutdown: acknowledge every ticket Closed, revoke every hook, lose
// the dispatcher, then forward engine shutdown when bound.
HRESULT RuntimeOwnerW10M::beginShutdown()
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    std::vector<uint64_t> hooks;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_shuttingDown)
            return S_OK;
        m_shuttingDown = true;
        for (const auto& entry : m_hooks)
            hooks.push_back(entry.first);
    }

    // Route cancellation is synchronous and needs no dispatcher, so every
    // ticket is acknowledged even with the dispatcher already lost.
    if (m_route) {
        HRESULT closed = m_route->close();
        if (FAILED(closed))
            return closed;
    }
    for (uint64_t hookId : hooks) {
        HRESULT revoked = revokeHook(hookId);
        if (FAILED(revoked))
            return revoked;
    }
    {
        std::lock_guard<std::mutex> lock(m_lock);
        m_dispatcherLost = true;
    }
    if (m_module.isBound()) {
        WKW10MResult engineResult = m_module.beginShutdown();
        std::lock_guard<std::mutex> lock(m_lock);
        m_engineShutdownResult = engineResult;
        return static_cast<HRESULT>(engineResult);
    }
    return S_OK;
}

// Mark the dispatcher lost without shutting down: wakes fail from here on
// while synchronous cancellation still works. Idempotent; test and teardown
// use only, full shutdown owns the real teardown order.
HRESULT RuntimeOwnerW10M::loseDispatcher()
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    std::lock_guard<std::mutex> lock(m_lock);
    m_dispatcherLost = true;
    return S_OK;
}

// Forward engine shutdown progress; S_OK when unbound.
WKW10MResult RuntimeOwnerW10M::pollShutdown()
{
    auto owner = checkThread();
    if (FAILED(owner))
        return static_cast<WKW10MResult>(owner);
    if (!m_module.isBound())
        return WKW10MResultOK;
    return m_module.pollShutdown();
}

// No open tickets, no pending route operations, no registered hooks.
bool RuntimeOwnerW10M::quiescent() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    if (!m_tickets.empty() || !m_hooks.empty())
        return false;
    return !m_route || m_route->pendingCount() == 0;
}

// Bind and pin the engine module; ShuttingDown once shutdown has begun.
WKW10MResult RuntimeOwnerW10M::bindEngine(const wchar_t* modulePath, uint32_t minimumMinor,
    uint64_t requiredCapabilities)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return static_cast<WKW10MResult>(owner);
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_shuttingDown)
            return WKW10MResultShuttingDown;
    }
    return m_module.bind(modulePath, minimumMinor, requiredCapabilities);
}

// True after a successful bind; the pin survives this owner.
bool RuntimeOwnerW10M::isEngineBound() const
{
    return m_module.isBound();
}

// Current clipboard generation; recreateClipboard advances it.
uint64_t RuntimeOwnerW10M::generation() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return m_generation;
}

// Latched once beginShutdown runs.
bool RuntimeOwnerW10M::isShuttingDown() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return m_shuttingDown;
}

// Latched once the dispatcher is lost.
bool RuntimeOwnerW10M::dispatcherLost() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return m_dispatcherLost;
}

// Route-issued operations still awaiting completion.
unsigned RuntimeOwnerW10M::pendingCount() const
{
    return m_route ? m_route->pendingCount() : 0;
}

// Tickets accepted but not yet acknowledged.
unsigned RuntimeOwnerW10M::openTickets() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return static_cast<unsigned>(m_tickets.size());
}

// Hooks registered but not yet revoked.
unsigned RuntimeOwnerW10M::hookCount() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return static_cast<unsigned>(m_hooks.size());
}

// Borrowed-table retains the engine still owes.
unsigned RuntimeOwnerW10M::tableRefCount() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return m_tableRefs;
}

// Accepted operations acknowledged normally.
unsigned RuntimeOwnerW10M::completedTotal() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return m_completedTotal;
}

// Accepted operations acknowledged Closed through cancel or shutdown.
unsigned RuntimeOwnerW10M::canceledTotal() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return m_canceledTotal;
}

// Transport completions dropped for lack of a dispatcher enqueue, carried
// across clipboard recreations.
unsigned RuntimeOwnerW10M::droppedTotal() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    unsigned live = m_serviceView ? m_serviceView->droppedEnqueues() : 0;
    return m_droppedCarried + live;
}

// Hooks acknowledged through revoke or shutdown.
unsigned RuntimeOwnerW10M::hooksRevokedTotal() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return m_hooksRevokedTotal;
}

// Last forwarded engine shutdown result; OK when never forwarded.
WKW10MResult RuntimeOwnerW10M::engineShutdownResult() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return m_engineShutdownResult;
}

// Build one service plus route pair and refresh the non-owning views.
void RuntimeOwnerW10M::createClipboard(std::unique_ptr<IServiceDispatcherW10M> dispatcher,
    std::unique_ptr<IClipboardTransportW10M> transport)
{
    m_wakePort = dispatcher.get();
    auto service = std::make_unique<ClipboardServiceW10M>(std::move(dispatcher), std::move(transport));
    m_serviceView = service.get();
    m_route = std::make_unique<ClipboardRouteW10M>(m_page, std::move(service));
}

// Issue one clipboard operation with a retained ticket. The owner lock is
// never held across the route call: writes complete inline through the thunk,
// which takes the ticket lock, so nesting would deadlock on itself.
HRESULT RuntimeOwnerW10M::issue(bool isRead, bool cut, const WKW10MString* text,
    ClipboardRouteCompletion completion, void* context)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (!completion || !m_route)
        return E_INVALIDARG;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_shuttingDown)
            return RO_E_CLOSED;
    }
    auto ticket = std::make_shared<Ticket>();
    ticket->owner = this;
    ticket->completion = completion;
    ticket->callerContext = context;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_shuttingDown)
            return RO_E_CLOSED;
        ticket->generation = m_generation;
        m_tickets.emplace(ticket.get(), ticket);
    }
    HRESULT accepted = isRead
        ? m_route->paste(&clipboardThunk, ticket.get())
        : (cut ? m_route->cut(text, &clipboardThunk, ticket.get())
               : m_route->copy(text, &clipboardThunk, ticket.get()));
    if (FAILED(accepted)) {
        std::lock_guard<std::mutex> lock(m_lock);
        auto found = m_tickets.find(ticket.get());
        if (found != m_tickets.end() && found->second == ticket)
            m_tickets.erase(found);
        return accepted;
    }
    return S_OK;
}

// Erase and return one ticket; null when already acknowledged.
std::shared_ptr<RuntimeOwnerW10M::Ticket> RuntimeOwnerW10M::takeTicket(Ticket* ticket)
{
    std::lock_guard<std::mutex> lock(m_lock);
    auto found = m_tickets.find(ticket);
    if (found == m_tickets.end() || found->second.get() != ticket)
        return nullptr;
    auto owned = found->second;
    m_tickets.erase(found);
    return owned;
}

// Acknowledge the ticket, then deliver through it. The borrowed span belongs
// to the route call enclosing this thunk, so it is forwarded untouched.
void __stdcall RuntimeOwnerW10M::clipboardThunk(void* context, uint64_t operationId, uint32_t kind,
    bool cut, WKW10MResult result, const WKW10MString* text)
{
    auto* ticket = static_cast<Ticket*>(context);
    if (!ticket || !ticket->owner)
        return;
    auto owned = ticket->owner->takeTicket(ticket);
    if (!owned)
        return;
    {
        std::lock_guard<std::mutex> lock(ticket->owner->m_lock);
        if (result == WKW10MResultClosed)
            ++ticket->owner->m_canceledTotal;
        else
            ++ticket->owner->m_completedTotal;
    }
    owned->completion(owned->callerContext, operationId, kind, cut, result, text);
}

// Retain the borrowed engine table context; void per contract.
void RuntimeOwnerW10M::retainTable()
{
    std::lock_guard<std::mutex> lock(m_lock);
    if (m_tableRefs < 0xFFFFFFFFu)
        ++m_tableRefs;
}

// Release a previous table retain; never self-deletes.
void RuntimeOwnerW10M::releaseTable()
{
    std::lock_guard<std::mutex> lock(m_lock);
    if (m_tableRefs)
        --m_tableRefs;
}

// Retain routing; void per contract, so a null context is ignored.
void __stdcall RuntimeOwnerW10M::retainTableThunk(void* context)
{
    if (!context)
        return;
    static_cast<RuntimeOwnerW10M*>(context)->retainTable();
}

// Table release routing; a null context is ignored.
void __stdcall RuntimeOwnerW10M::releaseTableThunk(void* context)
{
    if (!context)
        return;
    static_cast<RuntimeOwnerW10M*>(context)->releaseTable();
}

// Engine request routing; ShuttingDown once the latch is set, because the
// engine is shutting down too and new requests after that are a bug.
WKW10MResult __stdcall RuntimeOwnerW10M::requestTableThunk(void* context, WKW10MPageHandle page,
    uint64_t operationId, uint32_t kind, const WKW10MString* text,
    WKW10MClipboardCompletion completion, void* completionContext)
{
    if (!context)
        return WKW10MResultInvalidArgument;
    auto* self = static_cast<RuntimeOwnerW10M*>(context);
    HRESULT owner = self->checkThread();
    if (FAILED(owner))
        return static_cast<WKW10MResult>(owner);
    {
        std::lock_guard<std::mutex> lock(self->m_lock);
        if (self->m_shuttingDown)
            return WKW10MResultShuttingDown;
        if (!self->m_serviceView)
            return WKW10MResultInvalidArgument;
    }
    return static_cast<WKW10MResult>(self->m_serviceView->requestClipboard(
        page, operationId, kind, text, completion, completionContext));
}

// Engine cancel routing; cancels are cleanup and always pass through.
WKW10MResult __stdcall RuntimeOwnerW10M::cancelTableThunk(void* context, uint64_t operationId)
{
    if (!context)
        return WKW10MResultInvalidArgument;
    auto* self = static_cast<RuntimeOwnerW10M*>(context);
    HRESULT owner = self->checkThread();
    if (FAILED(owner))
        return static_cast<WKW10MResult>(owner);
    if (!self->m_serviceView)
        return WKW10MResultInvalidArgument;
    return static_cast<WKW10MResult>(self->m_serviceView->cancelClipboard(operationId));
}

// Wake routing for the borrowed table.
WKW10MResult __stdcall RuntimeOwnerW10M::postWakeTableThunk(void* context)
{
    if (!context)
        return WKW10MResultInvalidArgument;
    return static_cast<RuntimeOwnerW10M*>(context)->postWake();
}

} // namespace WebKit

