#include "ClipboardServiceUWP.h"

namespace WebKit {

// Bind borrowed ports; either missing port makes the service unusable.
ClipboardServiceUWP::ClipboardServiceUWP(std::unique_ptr<IServiceDispatcherUWP>&& dispatcher,
    std::unique_ptr<IClipboardTransportUWP>&& transport)
    : m_dispatcher(std::move(dispatcher))
    , m_transport(std::move(transport))
{
}

// Abandon pending work; late transport completions find no record and stop.
ClipboardServiceUWP::~ClipboardServiceUWP() = default;

// Enforce owning-thread access before touching the registry or any provider.
HRESULT ClipboardServiceUWP::checkThread() const
{
    return GetCurrentThreadId() == m_owner ? S_OK : RPC_E_WRONG_THREAD;
}

// Count operations still awaiting their single completion.
unsigned ClipboardServiceUWP::pendingCount() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return static_cast<unsigned>(m_operations.size());
}

// Count transport completions lost to a refused dispatcher enqueue.
unsigned ClipboardServiceUWP::droppedEnqueues() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return m_droppedEnqueues;
}

// Read the borrowed-table retain count for lifetime verification.
unsigned ClipboardServiceUWP::contextRefCount() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return m_contextRefs;
}

// Record one more borrowed-table retain; the owner destroys this instance.
void ClipboardServiceUWP::retain()
{
    std::lock_guard<std::mutex> lock(m_lock);
    if (m_contextRefs < 0xFFFFFFFFu)
        ++m_contextRefs;
}

// Release a previous retain without self-deleting; shutdown owns teardown.
void ClipboardServiceUWP::release()
{
    std::lock_guard<std::mutex> lock(m_lock);
    if (m_contextRefs)
        --m_contextRefs;
}

// Erase the record first, then invoke its single completion outside the lock.
HRESULT ClipboardServiceUWP::completeOperation(const std::shared_ptr<Operation>& operation,
    uint64_t operationId, WKUWPResult result, const std::string& text)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    std::shared_ptr<Operation> owned;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        auto found = m_operations.find(operationId);
        if (found == m_operations.end() || found->second != operation)
            return S_FALSE;
        owned = found->second;
        m_operations.erase(found);
    }
    WKUWPString span { reinterpret_cast<const uint8_t*>(text.data()), static_cast<uint32_t>(text.size()) };
    owned->completion(owned->completionContext, operationId, result, &span);
    return S_OK;
}

// Validate and accept one request; writes finish inline, reads ride the dispatcher.
HRESULT ClipboardServiceUWP::requestClipboard(WKUWPPageHandle page, uint64_t operationId, uint32_t kind,
    const WKUWPString* text, WKUWPClipboardCompletion completion, void* completionContext)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (!completion || !m_dispatcher || !m_transport)
        return E_INVALIDARG;
    if (kind != WKUWPClipboardRead && kind != WKUWPClipboardWrite)
        return E_INVALIDARG;
    if (!text)
        return E_INVALIDARG;
    if (text->length && !text->data)
        return E_INVALIDARG;
    auto operation = std::make_shared<Operation>();
    operation->page = page;
    operation->kind = kind;
    operation->completion = completion;
    operation->completionContext = completionContext;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_operations.find(operationId) != m_operations.end())
            return E_INVALIDARG;
        m_operations.emplace(operationId, operation);
    }
    if (kind == WKUWPClipboardWrite) {
        std::string bytes;
        if (text->length)
            bytes.assign(reinterpret_cast<const char*>(text->data), text->length);
        HRESULT written = m_transport->writeText(bytes);
        if (FAILED(written)) {
            completeOperation(operation, operationId, static_cast<WKUWPResult>(written), std::string());
            return written;
        }
        std::string empty;
        HRESULT completed = completeOperation(operation, operationId, WKUWPResultOK, empty);
        return FAILED(completed) ? completed : S_OK;
    }
    std::weak_ptr<Operation> weak = operation;
    m_transport->readText([this, weak, operationId](HRESULT result, std::string bytes) {
        auto record = weak.lock();
        if (!record)
            return;
        if (GetCurrentThreadId() == m_owner) {
            // The transport reports S_OK or a negative HRESULT, both valid results.
            completeOperation(record, operationId, static_cast<WKUWPResult>(result), bytes);
            return;
        }
        bool posted = m_dispatcher->post([this, record, operationId, result, text = std::move(bytes)]() mutable {
            completeOperation(record, operationId, static_cast<WKUWPResult>(result), text);
        });
        if (!posted) {
            std::lock_guard<std::mutex> lock(m_lock);
            ++m_droppedEnqueues;
        }
    });
    return S_OK;
}

// Cancel one accepted request with a synchronous Closed completion; unknown IDs fail.
HRESULT ClipboardServiceUWP::cancelClipboard(uint64_t operationId)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    std::shared_ptr<Operation> operation;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        auto found = m_operations.find(operationId);
        if (found == m_operations.end())
            return E_INVALIDARG;
        operation = found->second;
    }
    std::string empty;
    HRESULT completed = completeOperation(operation, operationId, WKUWPResultClosed, empty);
    return FAILED(completed) ? completed : S_OK;
}

// Fill a borrowed table; postWake stays null until the runtime owner (7.5) exists.
HRESULT ClipboardServiceUWP::buildTable(WKUWPPlatformServices* table)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (!table)
        return E_INVALIDARG;
    table->header = { sizeof(*table), WKUWP_STRUCTURE_VERSION };
    table->context = this;
    table->retainContext = &retainThunk;
    table->releaseContext = &releaseThunk;
    table->requestClipboard = &requestThunk;
    table->cancelClipboard = &cancelThunk;
    table->postWake = nullptr;
    return S_OK;
}

// Retain routing; void per contract, so a null context is ignored.
void __stdcall ClipboardServiceUWP::retainThunk(void* context)
{
    if (!context)
        return;
    static_cast<ClipboardServiceUWP*>(context)->retain();
}

// Release routing; shutdown owns teardown, so this never self-deletes.
void __stdcall ClipboardServiceUWP::releaseThunk(void* context)
{
    if (!context)
        return;
    static_cast<ClipboardServiceUWP*>(context)->release();
}

// Request routing; S_OK becomes OK and failures keep their originating value.
WKUWPResult __stdcall ClipboardServiceUWP::requestThunk(void* context, WKUWPPageHandle page,
    uint64_t operationId, uint32_t kind, const WKUWPString* text,
    WKUWPClipboardCompletion completion, void* completionContext)
{
    if (!context)
        return WKUWPResultInvalidArgument;
    return static_cast<WKUWPResult>(static_cast<ClipboardServiceUWP*>(context)->requestClipboard(
        page, operationId, kind, text, completion, completionContext));
}

// Cancel routing with originating-HRESULT preservation.
WKUWPResult __stdcall ClipboardServiceUWP::cancelThunk(void* context, uint64_t operationId)
{
    if (!context)
        return WKUWPResultInvalidArgument;
    return static_cast<WKUWPResult>(
        static_cast<ClipboardServiceUWP*>(context)->cancelClipboard(operationId));
}

} // namespace WebKit
