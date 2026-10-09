#pragma once

#include <agile.h>
#include <limits>
#include <map>
#include <vector>

namespace WebKit {

// Owner-thread delegate roots with explicit revocation. Each invocation snapshots
// subscriptions before app calls, so remove/Close cannot invalidate owed delivery.
template<typename Delegate> class EventHandlersUWP {
public:
    // Begin with no subscriptions and a nonreused per-event token sequence.
    EventHandlersUWP() = default;
    // Retain one independently revocable handler; null/exhausted tokens fail explicitly.
    Windows::Foundation::EventRegistrationToken add(Delegate^ handler)
    {
        if (!handler) throw ref new Platform::InvalidArgumentException();
        if (m_next == std::numeric_limits<long long>::max())
            throw Platform::Exception::CreateException(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW));
        auto value = ++m_next;
        m_handlers.emplace(value, Platform::Agile<Delegate^>(handler));
        Windows::Foundation::EventRegistrationToken token;
        token.Value = value;
        return token;
    }

    // Remove only the supplied subscription; already-revoked tokens are harmless.
    void remove(Windows::Foundation::EventRegistrationToken token) { m_handlers.erase(token.Value); }

    // Release all subscriber/captured-object roots after active transactions finish.
    void clear() { m_handlers.clear(); }

    // Copy invocation machinery once; throwing subscribers stop this invocation normally.
    template<typename... Arguments> void invoke(Arguments... arguments)
    {
        std::vector<Platform::Agile<Delegate^>> handlers;
        handlers.reserve(m_handlers.size());
        for (auto& item : m_handlers) handlers.emplace_back(item.second.Get());
        for (auto& handler : handlers) handler.Get()(arguments...);
    }

private:
    // Monotonic per-event token counter; IDs are never used as navigation/certificate consent.
    long long m_next { 0 };
    // Explicit handler roots, held only until token removal or final Close revocation.
    std::map<long long, Platform::Agile<Delegate^>> m_handlers;
};

} // namespace WebKit
