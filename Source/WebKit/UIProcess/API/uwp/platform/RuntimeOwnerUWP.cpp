#include "RuntimeOwnerUWP.h"

#include <vector>

namespace WebKit {

// Own the pipeline for one page; ports move into the owned service.
RuntimeOwnerUWP::RuntimeOwnerUWP(WKUWPPageHandle page, std::unique_ptr<IServiceDispatcherUWP>&& dispatcher,
    std::unique_ptr<IClipboardTransportUWP>&& transport)
    : m_page(page)
{
    createClipboard(std::move(dispatcher), std::move(transport));
}

// Shut down first (every accepted completion acknowledged), then abandon.
RuntimeOwnerUWP::~RuntimeOwnerUWP()
{
    beginShutdown();
}

// Enforce owning-thread access before touching the registry or any provider.
HRESULT RuntimeOwnerUWP::checkThread() const
{
    return GetCurrentThreadId() == m_owner ? S_OK : RPC_E_WRONG_THREAD;
}

// Publish copied bytes; each acceptance opens one ticket.
HRESULT RuntimeOwnerUWP::copy(const WKUWPString* text, ClipboardRouteCompletion completion, void* context)
{
    return issue(false, false, text, completion, context);
}

// Publish cut bytes with the cut marker; selection deletion is engine work.
HRESULT RuntimeOwnerUWP::cut(const WKUWPString* text, ClipboardRouteCompletion completion, void* context)
{
    return issue(false, true, text, completion, context);
}

// Read system clipboard text; completion carries the ticket's operation ID.
HRESULT RuntimeOwnerUWP::paste(ClipboardRouteCompletion completion, void* context)
{
    return issue(true, false, nullptr, completion, context);
}

// Cancel every pending route operation; each ticket is acknowledged Closed.
HRESULT RuntimeOwnerUWP::closeClipboard()
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
HRESULT RuntimeOwnerUWP::recreateClipboard(std::unique_ptr<IServiceDispatcherUWP>&& dispatcher,
    std::unique_ptr<IClipboardTransportUWP>&& transport)
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
HRESULT RuntimeOwnerUWP::buildTable(WKUWPPlatformServices* table)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (!table)
        return E_INVALIDARG;
    table->header = { sizeof(*table), WKUWP_STRUCTURE_VERSION };
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
WKUWPResult RuntimeOwnerUWP::postWake()
{
    std::lock_guard<std::mutex> lock(m_lock);
    if (m_dispatcherLost || !m_wakePort)
        return WKUWPResultDispatcherUnavailable;
    return m_wakePort->post([] { }) ? WKUWPResultOK : WKUWPResultDispatcherUnavailable;
}

// Register one shutdown hook; revocation acknowledges it Closed exactly once.
HRESULT RuntimeOwnerUWP::registerHook(RuntimeHookCallback callback, void* context, uint64_t* hookId)
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
HRESULT RuntimeOwnerUWP::revokeHook(uint64_t hookId)
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
    owned->callback(owned->callerContext, hookId, WKUWPResultClosed);
    std::lock_guard<std::mutex> lock(m_lock);
    ++m_hooksRevokedTotal;
    return S_OK;
}

// Latch shutdown: acknowledge every ticket Closed, revoke every hook, lose
// the dispatcher, then forward engine shutdown when bound.
HRESULT RuntimeOwnerUWP::beginShutdown()
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
        WKUWPResult engineResult = m_module.beginShutdown();
        std::lock_guard<std::mutex> lock(m_lock);
        m_engineShutdownResult = engineResult;
        return static_cast<HRESULT>(engineResult);
    }
    return S_OK;
}

// Mark the dispatcher lost without shutting down: wakes fail from here on
// while synchronous cancellation still works. Idempotent; test and teardown
// use only, full shutdown owns the real teardown order.
HRESULT RuntimeOwnerUWP::loseDispatcher()
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    std::lock_guard<std::mutex> lock(m_lock);
    m_dispatcherLost = true;
    return S_OK;
}

// Forward engine shutdown progress; S_OK when unbound.
WKUWPResult RuntimeOwnerUWP::pollShutdown()
{
    auto owner = checkThread();
    if (FAILED(owner))
        return static_cast<WKUWPResult>(owner);
    if (!m_module.isBound())
        return WKUWPResultOK;
    return m_module.pollShutdown();
}

// No open tickets, no pending route operations, no registered hooks.
bool RuntimeOwnerUWP::quiescent() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    if (!m_tickets.empty() || !m_hooks.empty())
        return false;
    return !m_route || m_route->pendingCount() == 0;
}

// Bind and pin the engine module; ShuttingDown once shutdown has begun.
WKUWPResult RuntimeOwnerUWP::bindEngine(const wchar_t* modulePath, uint32_t minimumMinor,
    uint64_t requiredCapabilities)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return static_cast<WKUWPResult>(owner);
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_shuttingDown)
            return WKUWPResultShuttingDown;
    }
    return m_module.bind(modulePath, minimumMinor, requiredCapabilities);
}

// True after a successful bind; the pin survives this owner.
bool RuntimeOwnerUWP::isEngineBound() const
{
    return m_module.isBound();
}

// Current clipboard generation; recreateClipboard advances it.
uint64_t RuntimeOwnerUWP::generation() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return m_generation;
}

// Latched once beginShutdown runs.
bool RuntimeOwnerUWP::isShuttingDown() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return m_shuttingDown;
}

// Latched once the dispatcher is lost.
bool RuntimeOwnerUWP::dispatcherLost() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return m_dispatcherLost;
}

// Route-issued operations still awaiting completion.
unsigned RuntimeOwnerUWP::pendingCount() const
{
    return m_route ? m_route->pendingCount() : 0;
}

// Tickets accepted but not yet acknowledged.
unsigned RuntimeOwnerUWP::openTickets() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return static_cast<unsigned>(m_tickets.size());
}

// Hooks registered but not yet revoked.
unsigned RuntimeOwnerUWP::hookCount() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return static_cast<unsigned>(m_hooks.size());
}

// Borrowed-table retains the engine still owes.
unsigned RuntimeOwnerUWP::tableRefCount() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return m_tableRefs;
}

// Accepted operations acknowledged normally.
unsigned RuntimeOwnerUWP::completedTotal() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return m_completedTotal;
}

// Accepted operations acknowledged Closed through cancel or shutdown.
unsigned RuntimeOwnerUWP::canceledTotal() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return m_canceledTotal;
}

// Transport completions dropped for lack of a dispatcher enqueue, carried
// across clipboard recreations.
unsigned RuntimeOwnerUWP::droppedTotal() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    unsigned live = m_serviceView ? m_serviceView->droppedEnqueues() : 0;
    return m_droppedCarried + live;
}

// Hooks acknowledged through revoke or shutdown.
unsigned RuntimeOwnerUWP::hooksRevokedTotal() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return m_hooksRevokedTotal;
}

// Last forwarded engine shutdown result; OK when never forwarded.
WKUWPResult RuntimeOwnerUWP::engineShutdownResult() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return m_engineShutdownResult;
}

// Build one service plus route pair and refresh the non-owning views.
void RuntimeOwnerUWP::createClipboard(std::unique_ptr<IServiceDispatcherUWP> dispatcher,
    std::unique_ptr<IClipboardTransportUWP> transport)
{
    m_wakePort = dispatcher.get();
    auto service = std::make_unique<ClipboardServiceUWP>(std::move(dispatcher), std::move(transport));
    m_serviceView = service.get();
    m_route = std::make_unique<ClipboardRouteUWP>(m_page, std::move(service));
}

// Issue one clipboard operation with a retained ticket. The owner lock is
// never held across the route call: writes complete inline through the thunk,
// which takes the ticket lock, so nesting would deadlock on itself.
HRESULT RuntimeOwnerUWP::issue(bool isRead, bool cut, const WKUWPString* text,
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
std::shared_ptr<RuntimeOwnerUWP::Ticket> RuntimeOwnerUWP::takeTicket(Ticket* ticket)
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
void __stdcall RuntimeOwnerUWP::clipboardThunk(void* context, uint64_t operationId, uint32_t kind,
    bool cut, WKUWPResult result, const WKUWPString* text)
{
    auto* ticket = static_cast<Ticket*>(context);
    if (!ticket || !ticket->owner)
        return;
    auto owned = ticket->owner->takeTicket(ticket);
    if (!owned)
        return;
    {
        std::lock_guard<std::mutex> lock(ticket->owner->m_lock);
        if (result == WKUWPResultClosed)
            ++ticket->owner->m_canceledTotal;
        else
            ++ticket->owner->m_completedTotal;
    }
    owned->completion(owned->callerContext, operationId, kind, cut, result, text);
}

// Retain the borrowed engine table context; void per contract.
void RuntimeOwnerUWP::retainTable()
{
    std::lock_guard<std::mutex> lock(m_lock);
    if (m_tableRefs < 0xFFFFFFFFu)
        ++m_tableRefs;
}

// Release a previous table retain; never self-deletes.
void RuntimeOwnerUWP::releaseTable()
{
    std::lock_guard<std::mutex> lock(m_lock);
    if (m_tableRefs)
        --m_tableRefs;
}

// Retain routing; void per contract, so a null context is ignored.
void __stdcall RuntimeOwnerUWP::retainTableThunk(void* context)
{
    if (!context)
        return;
    static_cast<RuntimeOwnerUWP*>(context)->retainTable();
}

// Table release routing; a null context is ignored.
void __stdcall RuntimeOwnerUWP::releaseTableThunk(void* context)
{
    if (!context)
        return;
    static_cast<RuntimeOwnerUWP*>(context)->releaseTable();
}

// Engine request routing; ShuttingDown once the latch is set, because the
// engine is shutting down too and new requests after that are a bug.
WKUWPResult __stdcall RuntimeOwnerUWP::requestTableThunk(void* context, WKUWPPageHandle page,
    uint64_t operationId, uint32_t kind, const WKUWPString* text,
    WKUWPClipboardCompletion completion, void* completionContext)
{
    if (!context)
        return WKUWPResultInvalidArgument;
    auto* self = static_cast<RuntimeOwnerUWP*>(context);
    HRESULT owner = self->checkThread();
    if (FAILED(owner))
        return static_cast<WKUWPResult>(owner);
    {
        std::lock_guard<std::mutex> lock(self->m_lock);
        if (self->m_shuttingDown)
            return WKUWPResultShuttingDown;
        if (!self->m_serviceView)
            return WKUWPResultInvalidArgument;
    }
    return static_cast<WKUWPResult>(self->m_serviceView->requestClipboard(
        page, operationId, kind, text, completion, completionContext));
}

// Engine cancel routing; cancels are cleanup and always pass through.
WKUWPResult __stdcall RuntimeOwnerUWP::cancelTableThunk(void* context, uint64_t operationId)
{
    if (!context)
        return WKUWPResultInvalidArgument;
    auto* self = static_cast<RuntimeOwnerUWP*>(context);
    HRESULT owner = self->checkThread();
    if (FAILED(owner))
        return static_cast<WKUWPResult>(owner);
    if (!self->m_serviceView)
        return WKUWPResultInvalidArgument;
    return static_cast<WKUWPResult>(self->m_serviceView->cancelClipboard(operationId));
}

// Wake routing for the borrowed table.
WKUWPResult __stdcall RuntimeOwnerUWP::postWakeTableThunk(void* context)
{
    if (!context)
        return WKUWPResultInvalidArgument;
    return static_cast<RuntimeOwnerUWP*>(context)->postWake();
}

} // namespace WebKit
