#include "ClipboardRouteUWP.h"

#include <vector>

namespace WebKit {

// Own the service and bind route operations to one page.
ClipboardRouteUWP::ClipboardRouteUWP(WKUWPPageHandle page, std::unique_ptr<ClipboardServiceUWP>&& service)
    : m_service(std::move(service))
    , m_page(page)
{
}

// Close first, then abandon; late transport completions find no record.
ClipboardRouteUWP::~ClipboardRouteUWP()
{
    close();
}

// Enforce owning-thread access before touching the registry or any provider.
HRESULT ClipboardRouteUWP::checkThread() const
{
    return GetCurrentThreadId() == m_owner ? S_OK : RPC_E_WRONG_THREAD;
}

// Count route-issued operations still awaiting their single completion.
unsigned ClipboardRouteUWP::pendingCount() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return static_cast<unsigned>(m_tokens.size());
}

// Read the close latch set before any cancellation runs.
bool ClipboardRouteUWP::closed() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return m_closed;
}

// Publish copied bytes; selection handling belongs to the engine.
HRESULT ClipboardRouteUWP::copy(const WKUWPString* text, ClipboardRouteCompletion completion, void* context)
{
    return write(false, text, completion, context);
}

// Publish cut bytes; the engine deletes the selection, the host only publishes.
HRESULT ClipboardRouteUWP::cut(const WKUWPString* text, ClipboardRouteCompletion completion, void* context)
{
    return write(true, text, completion, context);
}

// Read system clipboard text into the single completion.
HRESULT ClipboardRouteUWP::paste(ClipboardRouteCompletion completion, void* context)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (!completion || !m_service)
        return E_INVALIDARG;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_closed)
            return RO_E_CLOSED;
    }
    auto token = std::make_shared<Token>();
    token->route = this;
    token->completion = completion;
    token->callerContext = context;
    token->kind = WKUWPClipboardRead;
    token->cut = false;
    uint64_t operationId = 0;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_closed)
            return RO_E_CLOSED;
        operationId = m_nextId++;
        m_tokens.emplace(operationId, token);
    }
    WKUWPString empty { nullptr, 0 };
    HRESULT accepted = m_service->requestClipboard(m_page, operationId, WKUWPClipboardRead,
        &empty, &completionThunk, token.get());
    if (FAILED(accepted)) {
        std::lock_guard<std::mutex> lock(m_lock);
        auto found = m_tokens.find(operationId);
        if (found != m_tokens.end() && found->second == token)
            m_tokens.erase(found);
        return accepted;
    }
    return S_OK;
}

// Cancel every pending route operation with exactly-once Closed completion.
HRESULT ClipboardRouteUWP::close()
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (!m_service)
        return E_INVALIDARG;
    std::vector<uint64_t> pending;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_closed)
            return S_OK;
        m_closed = true;
        for (const auto& entry : m_tokens)
            pending.push_back(entry.first);
    }

    // Each cancel runs the service completion synchronously, which erases the
    // token and delivers Closed before the next ID is canceled. A completion
    // that reenters close() meets the latched flag and returns at once.
    for (uint64_t operationId : pending) {
        HRESULT canceled = m_service->cancelClipboard(operationId);
        if (FAILED(canceled) && canceled != E_INVALIDARG)
            return canceled;
    }
    return S_OK;
}

// Fill a borrowed table from the owned service; route Close leaves engine
// operations on that table untouched because the engine owns their contexts.
HRESULT ClipboardRouteUWP::buildTable(WKUWPPlatformServices* table)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (!m_service)
        return E_INVALIDARG;
    return m_service->buildTable(table);
}

// Issue one validated write with a retained token.
HRESULT ClipboardRouteUWP::write(bool cut, const WKUWPString* text, ClipboardRouteCompletion completion, void* context)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (!completion || !m_service)
        return E_INVALIDARG;
    if (!text)
        return E_INVALIDARG;
    if (text->length && !text->data)
        return E_INVALIDARG;
    auto token = std::make_shared<Token>();
    token->route = this;
    token->completion = completion;
    token->callerContext = context;
    token->kind = WKUWPClipboardWrite;
    token->cut = cut;
    uint64_t operationId = 0;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_closed)
            return RO_E_CLOSED;
        operationId = m_nextId++;
        m_tokens.emplace(operationId, token);
    }

    // A rejected service request must never complete later, so the token is
    // withdrawn here and the caller sees only the originating failure.
    HRESULT accepted = m_service->requestClipboard(m_page, operationId, WKUWPClipboardWrite,
        text, &completionThunk, token.get());
    if (FAILED(accepted)) {
        std::lock_guard<std::mutex> lock(m_lock);
        auto found = m_tokens.find(operationId);
        if (found != m_tokens.end() && found->second == token)
            m_tokens.erase(found);
        return accepted;
    }
    return S_OK;
}

// Erase and return one token; null when already completed or canceled.
std::shared_ptr<ClipboardRouteUWP::Token> ClipboardRouteUWP::takeToken(uint64_t operationId, Token* token)
{
    std::lock_guard<std::mutex> lock(m_lock);
    auto found = m_tokens.find(operationId);
    if (found == m_tokens.end() || found->second.get() != token)
        return nullptr;
    auto owned = found->second;
    m_tokens.erase(found);
    return owned;
}

// Deliver through the retained token; the borrowed span is copied before the
// caller completion runs because it is valid only during this call. The token
// is already erased here, so the completion may close the route or issue new
// operations, but must not destroy the route (see the typedef contract).
void __stdcall ClipboardRouteUWP::completionThunk(void* context, uint64_t operationId,
    WKUWPResult result, const WKUWPString* text)
{
    auto* token = static_cast<Token*>(context);
    if (!token || !text || !token->route)
        return;
    if (text->length && !text->data)
        return;
    auto owned = token->route->takeToken(operationId, token);
    if (!owned)
        return;
    std::string bytes;
    if (text->length)
        bytes.assign(reinterpret_cast<const char*>(text->data), text->length);
    // An empty span carries a null data pointer; length zero keeps it valid.
    const uint8_t* data = bytes.empty() ? nullptr : reinterpret_cast<const uint8_t*>(bytes.data());
    WKUWPString span { data, static_cast<uint32_t>(bytes.size()) };
    owned->completion(owned->callerContext, operationId, owned->kind, owned->cut, result, &span);
}
} // namespace WebKit
