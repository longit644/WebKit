#pragma once

#include "WKW10MInput.h"
#include "WKW10MNavigation.h"
#include "WKW10MPage.h"

#include <windows.h>

namespace WebKit {

// Owner-thread consumer of an already-negotiated/pinned frozen V1 page table.
// It copies function pointers, never WebCore objects, and borrows request spans
// only for each synchronous call. The runtime/page owner supplies module lifetime.
class PageControllerW10M {
public:
    // Start unbound; binding is explicit so incompatibility produces an HRESULT.
    PageControllerW10M() = default;
    // Validate identity/capabilities and attach a retained callback table before adoption.
    HRESULT bind(const WKW10MEngineAPI*, WKW10MPageHandle, const WKW10MPageCallbacks*);
    // Report a live page binding without deriving readiness from a rendering/log marker.
    bool isBound() const { return m_page != 0; }
    // Read this controller's opaque page identity.
    WKW10MPageHandle page() const { return m_page; }
    // Propose validated remote request data; the provider allocates its new main ID.
    HRESULT navigate(const WKW10MString& uri, uint64_t* navigationId);
    // Submit history-neutral opaque-origin markup with no remote base URI.
    HRESULT loadHtml(const WKW10MString& html, WKW10MCertificateToken warning, uint64_t* navigationId);
    // Traverse the engine's previous history item; the public layer decides availability.
    HRESULT goBack();
    // Traverse the engine's next history item; the public layer decides availability.
    HRESULT goForward();
    // Reload an engine remote document with a new main ID and no history insertion.
    HRESULT reload();
    // Cancel outstanding engine work without fabricating a navigation ID.
    HRESULT stop();
    // Atomically consume the provider's decision and retry its exact stored target.
    HRESULT proceed(WKW10MCertificateToken, uint64_t* navigationId);
    // Forward one validated input event; keyboard/focus delivery needs engine support.
    HRESULT sendInput(const WKW10MInputEvent&);
    // Forward keyboard focus state; the engine owns focus behavior once delivered.
    HRESULT setFocused(bool focused);
    // Close through the engine's cancellation/detach protocol; Pending retains binding.
    HRESULT close();
    // End a previously requested Pending close when the engine releases its attachment.
    void acknowledgeClose();
    // Map operation results explicitly; negative originating HRESULTs remain unchanged.
    static HRESULT resultToHRESULT(WKW10MResult);

private:
    // Reject wrong-thread/unbound work before forwarding or reading caller spans.
    HRESULT checkBound() const;
    // Immutable copied negotiated table; its module/build spans remain runtime-owned.
    WKW10MEngineAPI m_api { };
    // Opaque live engine page; zero means never bound or successfully closed.
    WKW10MPageHandle m_page { 0 };
    // Thread owning page commands/callback attachment.
    DWORD m_owner { GetCurrentThreadId() };
    // One close request owns cleanup until acknowledgement; repeated Close never calls twice.
    bool m_closeRequested { false };
};

} // namespace WebKit

