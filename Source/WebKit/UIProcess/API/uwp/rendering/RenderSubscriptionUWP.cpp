#include "RenderSubscriptionUWP.h"

namespace WebKit {

// Enforce owning-thread access before touching subscription state.
HRESULT RenderSubscriptionUWP::checkThread() const
{
    return GetCurrentThreadId() == m_owner ? S_OK : RPC_E_WRONG_THREAD;
}

// Add exactly one token; an existing token makes this a harmless no-op.
HRESULT RenderSubscriptionUWP::subscribe()
{
    if (m_token.value)
        return S_OK;
    return m_hook->subscribe(m_token);
}

// Revoke the token but retain it when removal itself fails, so a later path retries.
HRESULT RenderSubscriptionUWP::unsubscribe()
{
    if (!m_token.value)
        return S_OK;
    HRESULT result = m_hook->unsubscribe(m_token);
    if (SUCCEEDED(result))
        m_token = { };
    return result;
}

// Borrow the port and enter the initial Loaded presentation state.
HRESULT RenderSubscriptionUWP::attach(RenderHookUWP* hook)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (m_attached)
        return E_ILLEGAL_METHOD_CALL;
    if (!hook)
        return E_INVALIDARG;
    m_hook = hook;
    m_attached = true;
    return subscribe();
}

// A reparented visual resumes callbacks unless suspended, failed or closed.
HRESULT RenderSubscriptionUWP::onLoaded()
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (m_closed || m_failed)
        return S_OK;
    m_loaded = true;
    if (m_suspended)
        return S_OK;
    return subscribe();
}

// A removed visual pauses callbacks while page and service state survive.
HRESULT RenderSubscriptionUWP::onUnloaded()
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (m_closed)
        return S_OK;
    m_loaded = false;
    return unsubscribe();
}

// Suspend or resume presentation clocks independently of visual attachment.
HRESULT RenderSubscriptionUWP::setSuspended(bool suspended)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (m_closed)
        return RO_E_CLOSED;
    if (m_suspended == suspended)
        return S_OK;
    m_suspended = suspended;
    if (suspended)
        return unsubscribe();
    if (!m_loaded || m_failed)
        return S_OK;
    return subscribe();
}

// A failed frame ends presentation permanently without ending the instance.
HRESULT RenderSubscriptionUWP::disableAfterFailure()
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    m_failed = true;
    return unsubscribe();
}

// End the instance: best-effort revoke, then ignore every later transition.
HRESULT RenderSubscriptionUWP::close()
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (m_closed)
        return S_OK;
    m_closed = true;
    HRESULT result = S_OK;
    if (m_token.value && m_hook)
        result = m_hook->unsubscribe(m_token);
    m_token = { };
    m_hook = nullptr;
    return result;
}

} // namespace WebKit
