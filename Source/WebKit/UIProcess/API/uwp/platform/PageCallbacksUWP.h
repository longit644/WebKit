#pragma once

#include "WKUWPNavigation.h"

#include <atomic>
#include <windows.h>

namespace WebKit {

ref class WebKitWebView;

// Retained frozen-ABI context with a weak CX target. The engine retains one
// table reference; each dispatch adds a temporary reference through reentrancy.
class PageCallbacksUWP {
public:
    // Capture only weak control identity and its owner thread; setup owns one reference.
    explicit PageCallbacksUWP(WebKitWebView^);
    // Build a frozen V1 callback table borrowing this retained context.
    void buildTable(WKUWPPageCallbacks*) const;
    // Suppress future delivery without touching XAML or releasing engine ownership.
    void deactivate();
    // Retain native context ownership, including active-dispatch protection.
    void retain();
    // Release one ownership reference; final release destroys native/weak state only.
    void release();
    // Read the last nonthrowing callback result for controller diagnostics.
    HRESULT lastResult() const { return m_lastResult.load(); }
    // Read remaining engine attachment ownership separately from setup/dispatch references.
    unsigned int engineReferences() const { return m_engineReferences.load(); }

private:
    // Destroy only after engine/setup/active-dispatch references have unwound.
    ~PageCallbacksUWP();
    // Retain one engine callback-table context reference.
    static void __stdcall retainThunk(void*);
    // Acknowledge one engine context release independently of the control lifetime.
    static void __stdcall releaseThunk(void*);
    // Evaluate policy through the copied real-control event boundary without throwing.
    static WKUWPResult __stdcall startingThunk(void*, const WKUWPPageEvent*, uint32_t* cancel);
    // Deliver copied structured state through the real control without throwing.
    static void __stdcall notifyThunk(void*, const WKUWPPageEvent*);
    // Wake owner cleanup using only a weak target and retained native context.
    void notifyReleased();

    // Native ownership count; no strong control/page reference is retained here.
    std::atomic<unsigned int> m_references { 1 };
    // References explicitly held by provider callback-table attachment ownership.
    std::atomic<unsigned int> m_engineReferences { 0 };
    // Weak target used only after the callback's owner-thread check.
    Platform::WeakReference m_target;
    // Thread on which synchronous policy and public notifications may execute.
    DWORD m_ownerThread;
    // Agile owner dispatcher for release acknowledgement, never a strong XAML target.
    Windows::UI::Core::CoreDispatcher^ m_dispatcher;
    // Atomic suppression marker usable by owner shutdown/native cleanup.
    std::atomic<bool> m_active { true };
    // Last originating callback HRESULT, never parsed to create a public event.
    std::atomic<HRESULT> m_lastResult { S_OK };
};

// Setup-owner deleter; engine/active-dispatch references remain independently valid.
struct PageCallbacksOwnerDeleterUWP {
    // Deactivate and release setup ownership without invoking application handlers.
    void operator()(PageCallbacksUWP*) const;
};

} // namespace WebKit
