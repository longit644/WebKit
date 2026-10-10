#pragma once

#include <eventtoken.h>
#include <windows.h>

namespace WebKit {

// Narrow presentation port behind subscription state; implemented by the XAML owner.
class RenderHookUWP {
public:
    // Subscribe the owner's Rendering callback, writing exactly one token.
    virtual HRESULT subscribe(EventRegistrationToken&) = 0;
    // Revoke one previously issued token.
    virtual HRESULT unsubscribe(EventRegistrationToken) = 0;
protected:
    // Non-deleting base; ports are borrowed, never owned.
    ~RenderHookUWP() = default;
};

// Per-control Rendering subscription across unload/load, suspension and Close.
// Exactly one token is ever outstanding: every path that can add first checks,
// and every removal clears. Temporary unload only pauses callbacks while the
// page, presenter and service lease stay alive; only Close ends the instance.
// A frame-failure disable is terminal like Close but keeps the instance alive.
class RenderSubscriptionUWP {
public:
    // Construct detached on the thread that will own all mutations.
    RenderSubscriptionUWP() = default;
    // Subscription state has exactly one owner.
    RenderSubscriptionUWP(const RenderSubscriptionUWP&) = delete;
    // Prevent duplicated token bookkeeping.
    RenderSubscriptionUWP& operator=(const RenderSubscriptionUWP&) = delete;

    // Borrow the presentation port and subscribe the initial Loaded state.
    HRESULT attach(RenderHookUWP*);
    // The visual re-entered the tree: subscribe unless suspended/failed/closed.
    HRESULT onLoaded();
    // The visual left the tree: pause callbacks, preserve page and service state.
    HRESULT onUnloaded();
    // Pause or resume presentation clocks without touching visual attachment.
    HRESULT setSuspended(bool suspended);
    // Terminal frame failure: revoke and never resubscribe (inherited behavior).
    HRESULT disableAfterFailure();
    // Terminal instance cleanup: revoke and ignore every later transition.
    HRESULT close();

    // Check owner affinity before any subscription mutation.
    HRESULT checkThread() const;
    // Whether a Rendering token is currently outstanding.
    bool subscribed() const { return m_token.value != 0; }

private:
    // Subscribe only when no token is outstanding; preserve hook failures.
    HRESULT subscribe();
    // Revoke the outstanding token, keeping it when the hook reports failure.
    HRESULT unsubscribe();

    // Owner thread for all subscription mutation.
    DWORD m_owner { GetCurrentThreadId() };
    // Borrowed presentation port; null until attach, cleared by close.
    RenderHookUWP* m_hook { nullptr };
    // Outstanding Rendering token; zero value means no subscription.
    EventRegistrationToken m_token { };
    // Visual attachment, suspension, failure and cleanup markers.
    bool m_attached { false }, m_loaded { true }, m_suspended { false }, m_failed { false }, m_closed { false };
};
} // namespace WebKit
