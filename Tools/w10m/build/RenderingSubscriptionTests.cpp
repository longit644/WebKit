// Native presentation-subscription laws against a controlled hook double.
#include "rendering/RenderSubscriptionW10M.h"

#include <cstdio>
#include <thread>

using namespace WebKit;

namespace {

// Fail-closed totals across all independent subscription cases.
unsigned checks = 0, failures = 0;

// Preserve an earlier failure independently of later passing checks.
void check(bool value, const char* name)
{
    ++checks;
    if (!value)
        ++failures;
    std::printf("%s: %s\n", value ? "PASS" : "FAIL", name);
}

// Controlled CompositionTarget double issuing observable tokens.
struct HookDouble : RenderHookUWP {
    // Successful subscriptions and revocations observed.
    unsigned subscribes { 0 }, unsubscribes { 0 };
    // Last revoked token for exact-token assertions.
    EventRegistrationToken lastRemoved { };
    // Controlled originating failures.
    HRESULT subscribeResult { S_OK }, unsubscribeResult { S_OK };
    // Never-reused token identity for remove/recreate safety.
    int64_t nextToken { 0 };

    // Issue one token per subscription with an exact call count.
    HRESULT subscribe(EventRegistrationToken& token) override
    {
        if (FAILED(subscribeResult))
            return subscribeResult;
        token.value = ++nextToken;
        ++subscribes;
        return S_OK;
    }

    // Revoke exactly the supplied token and record it.
    HRESULT unsubscribe(EventRegistrationToken token) override
    {
        if (FAILED(unsubscribeResult))
            return unsubscribeResult;
        lastRemoved = token;
        ++unsubscribes;
        return S_OK;
    }
};

// Check attach, single-token invariance and unload/reload restoration.
void runLifecycle()
{
    {
        RenderSubscriptionUWP hooks;
        HookDouble hook;
        check(hooks.attach(nullptr) == E_INVALIDARG, "null presentation port rejects");
        check(hooks.attach(&hook) == S_OK && hooks.subscribed() && hook.subscribes == 1, "attach subscribes the initial Loaded state once");
        check(hooks.attach(&hook) == E_ILLEGAL_METHOD_CALL && hook.subscribes == 1, "second attach cannot duplicate the lease");
        check(hooks.onLoaded() == S_OK && hook.subscribes == 1, "Loaded while active never duplicates the token");
    }
    {
        RenderSubscriptionUWP hooks;
        HookDouble hook;
        hooks.attach(&hook);
        auto first = hook.nextToken;
        (void)first;
        check(hooks.onUnloaded() == S_OK && !hooks.subscribed() && hook.unsubscribes == 1, "unload pauses callbacks with one revocation");
        check(hooks.onUnloaded() == S_OK && hook.unsubscribes == 1, "repeated unload stays revoked without extra calls");
        check(hooks.onLoaded() == S_OK && hooks.subscribed() && hook.subscribes == 2, "reload restores exactly one token");
        check(hooks.onLoaded() == S_OK && hook.subscribes == 2, "repeated load never stacks tokens");
        for (int i = 0; i < 3; ++i) {
            hooks.onUnloaded();
            hooks.onLoaded();
        }
        check(hook.subscribes == 5 && hook.unsubscribes == 4 && hooks.subscribed(), "three reparent cycles balance five subscribes with four revocations");
    }
    {
        RenderSubscriptionUWP hooks;
        HookDouble hook;
        hooks.attach(&hook);
        check(hooks.setSuspended(true) == S_OK && !hooks.subscribed() && hook.unsubscribes == 1, "suspend pauses callbacks");
        check(hooks.setSuspended(true) == S_OK && hook.unsubscribes == 1, "repeated suspend is idempotent");
        check(hooks.onLoaded() == S_OK && !hooks.subscribed() && hook.subscribes == 1, "load while suspended subscribes nothing");
        check(hooks.setSuspended(false) == S_OK && hooks.subscribed() && hook.subscribes == 2, "resume restores one token");
        check(hooks.setSuspended(false) == S_OK && hook.subscribes == 2, "repeated resume is idempotent");
        hooks.onUnloaded();
        check(hooks.setSuspended(true) == S_OK && hook.unsubscribes == 2, "suspend while unloaded only records state");
        check(hooks.setSuspended(false) == S_OK && !hooks.subscribed(), "resume while unloaded restores nothing");
        check(hooks.onLoaded() == S_OK && hooks.subscribed(), "later load completes the pending resume");
    }
    {
        RenderSubscriptionUWP hooks;
        HookDouble hook;
        hooks.attach(&hook);
        check(hooks.close() == S_OK && !hooks.subscribed() && hook.unsubscribes == 1, "close revokes the token once");
        check(hooks.close() == S_OK && hook.unsubscribes == 1, "repeated close is harmless");
        check(hooks.onLoaded() == S_OK && !hooks.subscribed(), "load after close cannot revive callbacks");
        check(hooks.onUnloaded() == S_OK, "unload after close is harmless");
        check(hooks.setSuspended(false) == RO_E_CLOSED && !hooks.subscribed(), "resume after close reports closed without subscribing");
        check(hooks.setSuspended(true) == RO_E_CLOSED, "suspend after close reports closed");
        check(hooks.disableAfterFailure() == S_OK, "disable after close is harmless");
    }
    {
        RenderSubscriptionUWP hooks;
        HookDouble hook;
        hooks.attach(&hook);
        check(hooks.disableAfterFailure() == S_OK && !hooks.subscribed(), "frame failure revokes callbacks");
        check(hooks.onLoaded() == S_OK && !hooks.subscribed(), "load after failure never resubscribes");
        check(hooks.setSuspended(false) == S_OK && !hooks.subscribed(), "resume after failure never resubscribes");
        check(hooks.close() == S_OK, "close after failure still completes");
    }
    {
        RenderSubscriptionUWP hooks;
        HookDouble hook;
        hook.subscribeResult = E_ACCESSDENIED;
        check(hooks.attach(&hook) == E_ACCESSDENIED && !hooks.subscribed(), "failed initial subscribe preserves the error with no token");
        hook.subscribeResult = S_OK;
        check(hooks.onLoaded() == S_OK && hooks.subscribed() && hook.subscribes == 1, "later load retries the failed subscription once");
    }
    {
        RenderSubscriptionUWP hooks;
        HookDouble hook;
        hooks.attach(&hook);
        hook.unsubscribeResult = E_ACCESSDENIED;
        check(hooks.onUnloaded() == E_ACCESSDENIED && hooks.subscribed(), "failed revocation keeps the token for retry");
        hook.unsubscribeResult = S_OK;
        check(hooks.onUnloaded() == S_OK && !hooks.subscribed(), "retry revokes the retained token");
    }
    {
        RenderSubscriptionUWP first, second;
        HookDouble hookA, hookB;
        first.attach(&hookA);
        second.attach(&hookB);
        first.onUnloaded();
        check(!first.subscribed() && second.subscribed() && hookA.unsubscribes == 1 && !hookB.unsubscribes,
            "two instances keep independent token state");
        first.close();
        second.close();
        check(!first.subscribed() && !second.subscribed(), "both instances return to baseline");
    }
    {
        RenderSubscriptionUWP hooks;
        HookDouble hook;
        hooks.attach(&hook);
        unsigned wrongThread = 0;
        std::thread worker([&] {
            wrongThread += hooks.onLoaded() == RPC_E_WRONG_THREAD;
            wrongThread += hooks.onUnloaded() == RPC_E_WRONG_THREAD;
            wrongThread += hooks.setSuspended(true) == RPC_E_WRONG_THREAD;
            wrongThread += hooks.disableAfterFailure() == RPC_E_WRONG_THREAD;
            wrongThread += hooks.close() == RPC_E_WRONG_THREAD;
        });
        worker.join();
        check(wrongThread == 5 && hooks.subscribed() && hook.subscribes == 1, "worker transitions reject without token drift");
    }
}

} // namespace

// Run native subscription laws without any XAML or engine image.
int main()
{
    runLifecycle();
    std::printf("M10 render subscriptions: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
