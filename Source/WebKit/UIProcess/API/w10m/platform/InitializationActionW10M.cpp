#include "InitializationActionW10M.h"

#include <atomic>
#include <limits>
#include <utility>

using namespace Platform;
using namespace Windows::Foundation;

namespace WebKit {
namespace {

// AsyncInfo identities are process-unique and never wrap into a reused/zero value.
std::atomic<unsigned int> nextObservationId { 0 };

// Reserve a nonzero observation identity, reporting exhaustion rather than reuse.
unsigned int allocateObservationId()
{
    auto current = nextObservationId.load();
    do {
        if (current == std::numeric_limits<unsigned int>::max())
            throw Exception::CreateException(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW));
    } while (!nextObservationId.compare_exchange_weak(current, current + 1));
    return current + 1;
}

} // namespace

// Create one caller-owned observation and retain only dispatcher/weak-owner bookkeeping.
InitializationActionW10M::InitializationActionW10M(Windows::UI::Core::CoreDispatcher^ dispatcher, std::function<void(unsigned int)> detached)
    : m_id(allocateObservationId())
    , m_dispatcher(dispatcher)
    , m_detached(std::move(detached))
{
    if (!dispatcher)
        throw ref new InvalidArgumentException();
}

// Guard closed observations; the owning mutex is held by every caller.
void InitializationActionW10M::CheckOpenLocked()
{
    if (m_closed)
        throw ref new ObjectDisposedException();
}

// Retrieve the registered delegate through its context-safe marshaling wrapper.
AsyncActionCompletedHandler^ InitializationActionW10M::Completed::get()
{
    std::shared_ptr<Agile<AsyncActionCompletedHandler^>> completed;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        CheckOpenLocked();
        completed = m_completed;
    }
    return completed ? completed->Get() : nullptr;
}

// Register this caller's delegate once; terminal observations support late registration.
void InitializationActionW10M::Completed::set(AsyncActionCompletedHandler^ value)
{
    auto handler = value ? std::make_shared<Agile<AsyncActionCompletedHandler^>>(value) : nullptr;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        CheckOpenLocked();
        if (m_handlerAssigned)
            throw Exception::CreateException(E_ILLEGAL_DELEGATE_ASSIGNMENT);
        m_handlerAssigned = true;
        m_completed = std::move(handler);
    }
    Publish();
}

// Read the independent async identifier while enforcing IAsyncInfo.Close.
unsigned int InitializationActionW10M::Id::get()
{
    std::lock_guard<std::mutex> lock(m_lock);
    CheckOpenLocked();
    return m_id;
}

// Read private caller status, not the shared operation's eventual status.
AsyncStatus InitializationActionW10M::Status::get()
{
    std::lock_guard<std::mutex> lock(m_lock);
    CheckOpenLocked();
    return m_status;
}

// Read the exact original HRESULT associated with this observation.
HResult InitializationActionW10M::ErrorCode::get()
{
    std::lock_guard<std::mutex> lock(m_lock);
    CheckOpenLocked();
    HResult result;
    result.Value = m_error;
    return result;
}

// Retrieve void results with explicit pending/canceled/failed/closed ABI semantics.
void InitializationActionW10M::GetResults()
{
    HRESULT result;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        CheckOpenLocked();
        result = m_status == AsyncStatus::Started ? E_ILLEGAL_METHOD_CALL : m_error;
    }
    if (FAILED(result))
        throw Exception::CreateException(result);
}

// Freeze one caller's terminal status before any public completion invocation.
bool InitializationActionW10M::Resolve(HRESULT result, bool canceled)
{
    std::lock_guard<std::mutex> lock(m_lock);
    if (m_closed || m_status != AsyncStatus::Started)
        return false;
    m_error = canceled ? E_ABORT : result;
    m_status = canceled ? AsyncStatus::Canceled : FAILED(result) ? AsyncStatus::Error : AsyncStatus::Completed;
    return true;
}

// Remove ended producer ownership; a weak-owner callback does not cancel initialization.
void InitializationActionW10M::Detach()
{
    std::function<void(unsigned int)> detached;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        detached = m_detached;
    }
    try { if (detached) detached(m_id); }
    catch (...) { }
}

// Cancel only the caller action and leave the attempt usable by peers/Source.
void InitializationActionW10M::Cancel()
{
    {
        std::lock_guard<std::mutex> lock(m_lock);
        CheckOpenLocked();
        if (m_status != AsyncStatus::Started)
            return;
        m_status = AsyncStatus::Canceled;
        m_error = E_ABORT;
    }
    Detach();
    Publish();
}

// Release this caller's observation/handler independently, including while Started.
void InitializationActionW10M::Close()
{
    std::shared_ptr<Agile<AsyncActionCompletedHandler^>> released;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_closed)
            return;
        m_closed = true;
        released = std::move(m_completed);
    }
    Detach();
}

// Schedule completion on the owner dispatcher; status is already terminal on workers.
void InitializationActionW10M::Publish()
{
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_closed || m_status == AsyncStatus::Started || !m_handlerAssigned || m_deliveryQueued || m_delivered)
            return;
        m_deliveryQueued = true;
    }
    InitializationActionW10M^ lifetime = this;
    try {
        if (m_dispatcher->HasThreadAccess)
            Deliver();
        else
            m_dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal,
                ref new Windows::UI::Core::DispatchedHandler([lifetime] { lifetime->Deliver(); }));
    } catch (...) {
        std::lock_guard<std::mutex> lock(m_lock);
        m_deliveryQueued = false;
    }
}

// Invoke a terminal completion once, outside locks and with independent retained lifetime.
void InitializationActionW10M::Deliver()
{
    std::shared_ptr<Agile<AsyncActionCompletedHandler^>> completed;
    AsyncStatus status;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        m_deliveryQueued = false;
        if (m_closed || m_delivered || m_status == AsyncStatus::Started)
            return;
        completed = m_completed;
        if (!completed)
            return;
        m_delivered = true;
        status = m_status;
    }
    InitializationActionW10M^ lifetime = this;
    try { completed->Get()(this, status); }
    catch (...) { }
    (void)lifetime;
}

} // namespace WebKit

