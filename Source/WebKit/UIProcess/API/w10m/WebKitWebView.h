#pragma once

#include "WebKitNavigationEventArgs.h"
#include "WebKitWebViewOptions.h"
#include "native/NavigationStateW10M.h"
#include "native/PageControllerW10M.h"
#include "platform/InitializationActionW10M.h"
#include "platform/PageCallbacksW10M.h"
#include "platform/EventHandlersW10M.h"
#include "rendering/SwapChainPresenterW10M.h"

#include <deque>
#include <memory>
#include <vector>

namespace WebKit {

ref class WebKitWebView;
// Private owner hook implementation; no scheduler type is part of public WinMD.
ref class FrameSchedulerDriverW10M;
// Native per-view service lease/eligibility helper, owned until cleanup acknowledgement.
class FrameSchedulerW10M;

// A named default interface also resolves the compiler's self-referential
// TypedEventHandler GUIDs (C4462). This IID is distinct from the WRL prototype.
[uuid(b0cacd92-a344-4b7b-9e94-99c3c3bfb35c)]
[Windows::Foundation::Metadata::WebHostHidden]
public interface class IWebKitWebViewV2 {
    // Requested/effective URI DP; null follows the per-state cancellation table.
    property Windows::Foundation::Uri^ Source { Windows::Foundation::Uri^ get(); void set(Windows::Foundation::Uri^ value); }
    // Read-only availability of the engine's previous history neighbor.
    property bool CanGoBack { bool get(); }
    // Read-only availability of the engine's next history neighbor.
    property bool CanGoForward { bool get(); }
    // Read-only main-navigation loading state, not initialization/network activity.
    property bool IsLoading { bool get(); }
    // Read-only copied document title; empty when absent.
    property Platform::String^ DocumentTitle { Platform::String^ get(); }

    // Observe shared page readiness through a distinct action for this caller.
    Windows::Foundation::IAsyncAction^ EnsureInitializedAsync();
    // Traverse the engine's previous history neighbor, or perform a no-op.
    void GoBack();
    // Traverse the engine's next history neighbor, or perform a no-op.
    void GoForward();
    // Reload current content with a new main ID and unchanged history cursor.
    void Reload();
    // Cancel outstanding work without duplicating a document's terminal outcome.
    void Stop();
    // Propose opaque-origin history-neutral host markup, invalidating old decisions.
    void NavigateToString(Platform::String^ html);
    // Propose warning markup associated with the exact current decision capability.
    [Windows::Foundation::Metadata::DefaultOverload]
    void NavigateToString(Platform::String^ html, WebKitCertificateFailureToken^ warningToken);
    // Consume this control's current capability and retry its bound HTTPS target.
    void ProceedWithCertificateFailure(WebKitCertificateFailureToken^ token);
    // Reject new work immediately and finish owed notifications/cleanup once.
    void Close();

    // Synchronous owner-thread proposal event with a dispatch-limited Cancel verdict.
    event Windows::Foundation::TypedEventHandler<WebKitWebView^, WebKitNavigationStartingEventArgs^>^ NavigationStarting;
    // Actual engine commit/same-document change; requested/restored values use INPC only.
    event Windows::Foundation::TypedEventHandler<WebKitWebView^, WebKitSourceChangedEventArgs^>^ SourceChanged;
    // Copied engine history flags, raised after their public snapshot is installed.
    event Windows::Foundation::TypedEventHandler<WebKitWebView^, WebKitHistoryChangedEventArgs^>^ HistoryChanged;
    // Copied current document title, raised after property notifications.
    event Windows::Foundation::TypedEventHandler<WebKitWebView^, WebKitDocumentTitleChangedEventArgs^>^ DocumentTitleChanged;
    // Single recorded terminal delivery, owed across reentrant mutations/Close.
    event Windows::Foundation::TypedEventHandler<WebKitWebView^, WebKitNavigationCompletedEventArgs^>^ NavigationCompleted;
};

// Authored public control. Native implementation helpers retain their W10M suffix;
// public runtime classes and their source basenames do not.
[Windows::Foundation::Metadata::WebHostHidden]
[Windows::UI::Xaml::Data::Bindable]
public ref class WebKitWebView sealed : Windows::UI::Xaml::Controls::UserControl,
    [Windows::Foundation::Metadata::Default] IWebKitWebViewV2,
    Windows::UI::Xaml::Data::INotifyPropertyChanged,
    Windows::UI::Xaml::Data::ICustomPropertyProvider {
public:
    // Construct a XAML-compatible control with containing-package default options.
    WebKitWebView();
    // Construct a control with a private immutable copy of the supplied options.
    WebKitWebView(WebKitWebViewOptions^ options);

    // Lazily registered URI dependency property used by declarative assignments/binding.
    static property Windows::UI::Xaml::DependencyProperty^ SourceProperty {
        // Return the lazily registered shared URI property identity.
        Windows::UI::Xaml::DependencyProperty^ get();
    }

    // Read the published URI or enqueue a validated requested/null transition.
    virtual property Windows::Foundation::Uri^ Source { Windows::Foundation::Uri^ get(); void set(Windows::Foundation::Uri^ value); }
    // Read copied engine previous-neighbor availability on the owner thread.
    virtual property bool CanGoBack { bool get(); }
    // Read copied engine next-neighbor availability on the owner thread.
    virtual property bool CanGoForward { bool get(); }
    // Read the transaction's main-loading snapshot, including after Close.
    virtual property bool IsLoading { bool get(); }
    // Read the immutable published title, including the final closed snapshot.
    virtual property Platform::String^ DocumentTitle { Platform::String^ get(); }

    // Obtain an independent observation of one shared initialization attempt (task 5.4).
    virtual Windows::Foundation::IAsyncAction^ EnsureInitializedAsync();
    // Invoke engine Back traversal once controller command wiring is delivered.
    virtual void GoBack();
    // Invoke engine Forward traversal once controller command wiring is delivered.
    virtual void GoForward();
    // Reload retained current content through the controller with a fresh main ID.
    virtual void Reload();
    // Stop instance work through the controller without a synthetic document clear.
    virtual void Stop();
    // Load unassociated host HTML through substitute data (task 5.6).
    virtual void NavigateToString(Platform::String^ html);
    // Load warning HTML while preserving the exact valid decision object (task 5.6).
    [Windows::Foundation::Metadata::DefaultOverload]
    virtual void NavigateToString(Platform::String^ html, WebKitCertificateFailureToken^ warningToken);
    // Validate and consume a hidden capability before a new-ID HTTPS retry.
    virtual void ProceedWithCertificateFailure(WebKitCertificateFailureToken^ token);
    // Mark closing immediately; defer native cleanup until public dispatch unwinds.
    virtual void Close();

    // Notify changed observable properties after atomically publishing the snapshot.
    virtual event Windows::UI::Xaml::Data::PropertyChangedEventHandler^ PropertyChanged {
        // Register a property observer only while the owner control accepts new work.
        Windows::Foundation::EventRegistrationToken add(Windows::UI::Xaml::Data::PropertyChangedEventHandler^ handler);
        // Remove a property observer on its owner thread, including after Close.
        void remove(Windows::Foundation::EventRegistrationToken token);
    }

// Synchronously expose the copied proposal before its load is accepted.
    virtual event Windows::Foundation::TypedEventHandler<WebKitWebView^, WebKitNavigationStartingEventArgs^>^ NavigationStarting {
        // Register one owner-thread synchronous policy observer.
        Windows::Foundation::EventRegistrationToken add(Windows::Foundation::TypedEventHandler<WebKitWebView^, WebKitNavigationStartingEventArgs^>^ handler);
        // Remove the supplied policy subscription without invalidating active invocation.
        void remove(Windows::Foundation::EventRegistrationToken token);
    }

    // Expose a copied actual commit/same-document signal after property notifications.
    virtual event Windows::Foundation::TypedEventHandler<WebKitWebView^, WebKitSourceChangedEventArgs^>^ SourceChanged {
        // Register one actual commit/same-document observer.
        Windows::Foundation::EventRegistrationToken add(Windows::Foundation::TypedEventHandler<WebKitWebView^, WebKitSourceChangedEventArgs^>^ handler);
        // Remove the supplied URI-change subscription on its owner thread.
        void remove(Windows::Foundation::EventRegistrationToken token);
    }

    // Expose copied history flags after observable state installation.
    virtual event Windows::Foundation::TypedEventHandler<WebKitWebView^, WebKitHistoryChangedEventArgs^>^ HistoryChanged {
        // Register one copied history-availability observer.
        Windows::Foundation::EventRegistrationToken add(Windows::Foundation::TypedEventHandler<WebKitWebView^, WebKitHistoryChangedEventArgs^>^ handler);
        // Remove the supplied history subscription on its owner thread.
        void remove(Windows::Foundation::EventRegistrationToken token);
    }

    // Expose a copied document title after observable state installation.
    virtual event Windows::Foundation::TypedEventHandler<WebKitWebView^, WebKitDocumentTitleChangedEventArgs^>^ DocumentTitleChanged {
        // Register one copied document-title observer.
        Windows::Foundation::EventRegistrationToken add(Windows::Foundation::TypedEventHandler<WebKitWebView^, WebKitDocumentTitleChangedEventArgs^>^ handler);
        // Remove the supplied title subscription on its owner thread.
        void remove(Windows::Foundation::EventRegistrationToken token);
    }

    // Finish a marked terminal obligation before processing deferred commands.
    virtual event Windows::Foundation::TypedEventHandler<WebKitWebView^, WebKitNavigationCompletedEventArgs^>^ NavigationCompleted {
        // Register one terminal observer; active owed deliveries use a retained snapshot.
        Windows::Foundation::EventRegistrationToken add(Windows::Foundation::TypedEventHandler<WebKitWebView^, WebKitNavigationCompletedEventArgs^>^ handler);
        // Remove the supplied terminal subscription on its owner thread.
        void remove(Windows::Foundation::EventRegistrationToken token);
    }


    // Supply owner-thread runtime Binding accessors for the five observable properties.
    virtual Windows::UI::Xaml::Data::ICustomProperty^ GetCustomProperty(Platform::String^ name);
    // Report no indexed properties; the public state is scalar.
    virtual Windows::UI::Xaml::Data::ICustomProperty^ GetIndexedProperty(Platform::String^ name, Windows::UI::Xaml::Interop::TypeName type);
    // Supply a stable class representation for runtime property discovery.
    virtual Platform::String^ GetStringRepresentation();
    // Report the actual public runtime type to the custom-property provider.
    virtual property Windows::UI::Xaml::Interop::TypeName Type { Windows::UI::Xaml::Interop::TypeName get(); }

internal:
    // Controller entry points consume structured values, never diagnostic text.
    void SetCommandHandler(SourceCommandHandlerW10M, void* context);
    // Attach an already-negotiated/pinned page table before controller readiness succeeds.
    HRESULT BindPageController(const WKW10MEngineAPI*, unsigned long long pageId);
    // Build the actual frozen callback attachment with retained weak-target ownership.
    HRESULT BuildPageCallbacks(WKW10MPageCallbacks*);
    // Read the last contained native callback result for controller diagnostics.
    HRESULT LastPageCallbackResult();
    // Consume owner-thread engine callback-release acknowledgement during pending Close.
    void PageCallbacksReleased();
    // Validate hidden object/control/session/revision binding without granting consent.
    bool IsCurrentCertificateToken(WebKitCertificateFailureToken^);
    // Install readiness/failure without letting exceptions cross the native caller.
    HRESULT CompleteInitialization(unsigned long long pageId, HRESULT result);
    // Resolve only the matching attempt; delayed signals cannot complete retry actions.
    HRESULT CompleteInitialization(unsigned long long attemptId, unsigned long long pageId, HRESULT result);
    // Read the active attempt cookie for a controller's retained readiness callback.
    unsigned long long InitializationAttemptId() { return m_initializationAttempt; }
    // Remove one canceled/closed observation without canceling shared initialization.
    void DetachInitializationAction(unsigned int observationId);
    // Borrow the validated immutable holder for controller setup/diagnostic identity checks.
    const ValidatedWebViewOptionsW10M* ValidatedInitializationOptions() { return m_validatedOptions.get(); }
    // Copy/evaluate synchronous policy and return a nonthrowing V1 verdict/HRESULT.
    WKW10MResult HandleStarting(const WKW10MPageEvent&, uint32_t* cancel);
    // Copy and transact a V1 notification, returning diagnostics instead of throwing.
    HRESULT HandleNotification(const WKW10MPageEvent&);
    // Read internal lifetime phase for controller setup and controlled diagnostics.
    ControlPhaseW10M ControlPhase() { return m_state.controlPhase(); }
    // Read suppressed public-handler failures for diagnostic verification.
    unsigned int HandlerFailures() { return m_handlerFailures; }
    // Read rejected deferred commands independently of already-recorded terminal outcomes.
    unsigned int DeferredCommandFailures() { return m_deferredCommandFailures; }
    // Read the exact most recent deferred revalidation/controller failure HRESULT.
    HRESULT LastDeferredCommandError() { return m_lastDeferredCommandError; }

private:
    // Join process service for a bound page; visual presentation hooks are configured separately.
    HRESULT AttachFrameService(const WKW10MEngineAPI*);
    // Create the SwapChainPanel visual and its host-owned presenter once.
    void EnsurePanel();
    // Resize the engine page and attach the engine-owned compositor when laid out.
    HRESULT EnsurePresentation();
    // Render one dirty engine frame; S_FALSE when nothing was eligible or dirty.
    HRESULT RenderPresentation(int64_t epoch);
    // Detach the engine surface and release the visual before close/teardown.
    HRESULT TeardownPresentation();
    // Retry presentation after layout settles; layout itself never navigates.
    void OnControlSizeChanged(Platform::Object^, Windows::UI::Xaml::SizeChangedEventArgs^);
    // Forward declaration of the owned native/CX-safe deferred command record.
    struct DeferredSource;
    // Enforce owning UI-dispatcher access before state reads or mutations.
    void CheckThread();
    // Reject wrong-thread or closing/closed mutations with WinRT HRESULT semantics.
    void CheckOpen();
    // Validate/freeze copied package configuration before page use.
    void PrepareConfiguration();
    // Enter one generation-bound shared attempt, independently of caller observations.
    void BeginInitializationAttempt();
    // Queue shared work once after current property/completion dispatch unwinds.
    void RequestInitialization();
    // Validate frozen options and issue the current controller attempt once.
    void IssueInitialization(unsigned long long attemptId);
    // Create one independent action with dispatcher-safe weak-owner detach bookkeeping.
    InitializationActionW10M^ CreateInitializationAction();
    // Mark all live caller observations canceled before invoking any completion handler.
    void CancelInitializationActions();
    // Release instance-owned handlers/context/configuration only after owed dispatch unwinds.
    void FinishClose();
    // Revoke public delegate roots after the last owed property/typed-event transaction.
    void RevokePublicEvents();
    // Validate and enqueue requested/null Source without changing an active snapshot.
    void SubmitSource(Windows::Foundation::Uri^);
    // Drain copied events and FIFO Source commands only after safe dispatch/call unwind.
    void DrainCommands();
    // Revalidate a queued Source and publish its state before controller work.
    void ExecuteSource(const std::wstring&);
    // Revalidate readiness intent and restore committed Source after pre-admission rejection.
    void ExecuteInitialSource(unsigned long long intent);
    // Validate and execute one deferred public command against current page/state/token data.
    void ExecutePageCommand(struct DeferredSource&);
    // Queue a mutation under the same snapshot/FIFO/policy-interruption rules as Source.
    void QueuePageCommand(struct DeferredSource&&);
    // Continue a copied document command after shared readiness without rooting the control.
    void QueueAfterInitialization(struct DeferredSource&&);
    // Advance latest unstarted document intent so obsolete readiness continuations drop safely.
    void AdvanceDocumentIntent();
    // Invoke the instance command port under a native-call drain guard.
    void ExecuteNative(SourceActionW10M, const std::wstring&);
    // Atomically install a candidate and finish property/typed-event notifications.
    void PublishState(NavigationStateW10M&&, unsigned int eventKind, Platform::Object^ payload,
        std::vector<Platform::Agile<InitializationActionW10M^>>* resolvingActions = nullptr, HRESULT initializationResult = S_OK);
    // Synchronize the URI DP under the nonrecursive internal-update guard.
    void WriteSourceProperty();
    // Raise one INPC notification while containing subscriber failures.
    void NotifyProperty(Platform::String^);
    // Accept null or absolute HTTP(S) targets and preserve their canonical URI spelling.
    static std::wstring ValidateSource(Windows::Foundation::Uri^);
    // Convert/apply a structured nonpolicy signal and create its owed event payload.
    HRESULT ProcessNotification(const WKW10MPageEvent&);
    // Invalidate the current decision without changing already-copied event payloads.
    void InvalidateCertificateToken();
    // Roll back generic DP assignments before validation/enqueue against the snapshot.
    static void SourcePropertyChanged(Windows::UI::Xaml::DependencyObject^, Windows::UI::Xaml::DependencyPropertyChangedEventArgs^);

    // Shared registered property identity; no page/controller state is shared here.
    static Windows::UI::Xaml::DependencyProperty^ s_sourceProperty;
    // Process-unique control capability identity, independent of coinciding page IDs.
    unsigned long long m_controlIdentity;
    // Setup ownership for the weak retained frozen-ABI attachment.
    std::unique_ptr<PageCallbacksW10M, PageCallbacksOwnerDeleterW10M> m_pageCallbacks;
    // Exact currently eligible object; object identity is required in addition to its pair.
    WebKitCertificateFailureToken^ m_certificateToken;
    // Bound engine issuer for this page/session; never exposed to the host.
    uint64_t m_certificateIssuer { 0 };
    // Highest offered failure revision; delayed packets cannot reissue an older capability.
    uint64_t m_certificateRevision { 0 };
    // Mutation epoch revalidated by a locally consumed deferred retry capability.
    uint64_t m_certificateEpoch { 0 };
    // Private immutable constructor copy of configuration.
    std::unique_ptr<WebViewConfigurationW10M> m_configuration;
    // Retained validated CA bytes/paths/limits used by initialization retries.
    std::unique_ptr<const ValidatedWebViewOptionsW10M> m_validatedOptions;
    // Pins the packaged engine and tracks this control's page until close acknowledgement.
    std::unique_ptr<EngineModuleW10M> m_engineModule;
    // Native frozen-table page consumer; module pin/lifetime is supplied by the runtime.
    std::unique_ptr<PageControllerW10M> m_pageController;
    // Visible engine surface; created once on the owner thread, never a page identity.
    Windows::UI::Xaml::Controls::SwapChainPanel^ m_panel;
    // Host-owned presenter bridging the panel to the engine-owned compositor.
    std::unique_ptr<SwapChainPresenterW10M> m_presenter;
    // Last geometry accepted by the engine; reattach is required on scale change.
    unsigned m_presentationWidth { 0 }, m_presentationHeight { 0 };
    // Last device scale accepted by the engine attachment.
    double m_presentationScaleX { 0 }, m_presentationScaleY { 0 };
    // Native exposure requires restoring a painted frame even at unchanged geometry.
    bool m_presentationNeedsRevalidation { false };
    // Window visibility pauses presentation alone, not process service.
    bool m_windowVisible { true };
    // Weak external window hooks are revoked at ordinary Close.
    Windows::UI::Xaml::Window^ m_presentationWindow;
    // Captured external hook identities contain no strong control delegate.
    Windows::Foundation::EventRegistrationToken m_windowVisibilityToken, m_windowActivationToken;
    // Whether the external hooks still require revocation.
    bool m_windowHooksInstalled { false };
    // Engine surface is currently attached to the visible panel.
    bool m_presentationAttached { false };
    // Shared weak-target SDK timer/rendering driver for a service-capable page table.
    FrameSchedulerDriverW10M^ m_frameDriver;
    // Native instance service lease remains live while ordinary Close is Pending.
    std::shared_ptr<FrameSchedulerW10M> m_frameScheduler;
    // Monotonic driver subscription revoked only after page cleanup finishes.
    uint64_t m_frameClient { 0 };
    // Published value state plus private page/generation/lifecycle bookkeeping.
    NavigationStateW10M m_state;
    // Published immutable URI handle, matching the guarded DP value.
    Windows::Foundation::Uri^ m_source;
    // Published immutable title handle, never a borrowed engine string.
    Platform::String^ m_title { L"" };
    // Explicit revocable property-handler roots; active invocation copies stay independent.
    EventHandlersW10M<Windows::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChangedHandlers;
    // Explicit revocable synchronous policy-handler roots.
    EventHandlersW10M<Windows::Foundation::TypedEventHandler<WebKitWebView^, WebKitNavigationStartingEventArgs^>> m_startingHandlers;
    // Explicit revocable commit/same-document handler roots.
    EventHandlersW10M<Windows::Foundation::TypedEventHandler<WebKitWebView^, WebKitSourceChangedEventArgs^>> m_sourceChangedHandlers;
    // Explicit revocable history observer roots.
    EventHandlersW10M<Windows::Foundation::TypedEventHandler<WebKitWebView^, WebKitHistoryChangedEventArgs^>> m_historyHandlers;
    // Explicit revocable copied-title observer roots.
    EventHandlersW10M<Windows::Foundation::TypedEventHandler<WebKitWebView^, WebKitDocumentTitleChangedEventArgs^>> m_titleHandlers;
    // Explicit revocable terminal observer roots, cleared only after owed dispatch.
    EventHandlersW10M<Windows::Foundation::TypedEventHandler<WebKitWebView^, WebKitNavigationCompletedEventArgs^>> m_completedHandlers;
    // Borrowed controller entry point, valid through native/public call unwind.
    SourceCommandHandlerW10M m_commandHandler { nullptr };
    // Instance controller context; never a process-global routed target.
    void* m_commandContext { nullptr };
    // Internal command kinds kept separate from URI intent and engine navigation IDs.
    enum class DeferredOperationW10M {
        // Revalidate and execute a requested/null Source transition.
        Source,
        // Begin the captured initialization generation's controller work once.
        Initialize,
        // Consume the latest initial URI only after all live joiners resolve.
        InitialSource,
        // Traverse the actual previous engine history neighbor when available.
        Back,
        // Traverse the actual next engine history neighbor when available.
        Forward,
        // Reload current remote content or retained history-neutral host markup.
        Reload,
        // Cancel outstanding page work without inventing a main-navigation ID.
        Stop,
        // Propose generic or explicitly associated warning host markup.
        Html,
        // Submit a locally consumed decision to the provider's atomic retry slot.
        Retry
    };

    // Copied reentrant Source intent or initialization/readiness scheduling marker.
    struct DeferredSource {
        // Validated requested URI; empty represents null Source.
        std::wstring uri;
        // Operation to execute without overloading null Source semantics.
        DeferredOperationW10M operation { DeferredOperationW10M::Source };
        // Initialization cookie, initial-document intent, or consumed retry mutation epoch.
        unsigned long long attempt { 0 };
        // Owned UTF8 host text, preserving explicit lengths and embedded NULs.
        std::string html;
        // Exact warning association revalidated at execution; never numeric URL consent.
        Platform::Agile<WebKitCertificateFailureToken^> warning;
        // Hidden consumed native pair retained only for the queued provider retry call.
        WKW10MCertificateToken decision { 0, 0 };
        // The caller queued inside dispatch/native work; execution failures are diagnostic.
        bool deferred { false };
    };

    // Owned callback copy retained when another notification transaction is active.
    struct DeferredEvent {
        // Copied scalar/header payload; its string views are rebuilt before delivery.
        WKW10MPageEvent event;
        // Owned UTF8 URI bytes replacing the provider's borrowed span.
        std::string uri;
        // Owned UTF8 title bytes replacing the provider's borrowed span.
        std::string title;
    };

    // Reentrant command FIFO, revalidated when each entry executes.
    std::deque<DeferredSource> m_sourceCommands;
    // Copied native signals deferred across an immutable public snapshot.
    std::deque<DeferredEvent> m_deferredEvents;
    // Producer-owned live observations, each with its own handler/cancel/close state.
    std::vector<Platform::Agile<InitializationActionW10M^>> m_initializationActions;
    // Monotonic cookie binding readiness/failure to one attempt, never a main ID.
    unsigned long long m_initializationAttempt { 0 };
    // The shared attempt survives independent cancellation/closure of every observer.
    bool m_initializationActive { false };
    // One initialization marker is queued for the current generation.
    bool m_initializationQueued { false };
    // The controller has been invoked once for this attempt.
    bool m_initializationIssued { false };
    // Last committed content policy, used to distinguish transient versus remote Reload.
    uint32_t m_committedContentKind { WKW10MContentRemote };
    // Current transient host text; adopted only on its real commit, never an app history list.
    std::string m_hostHtml;
    // Candidate host text for the synchronously executing loadHtml proposal.
    std::string m_proposedHtml;
    // Engine main ID to which the candidate host text was actually attributed.
    uint64_t m_proposedHtmlId { 0 };
    // True only while the controller is synchronously proposing copied host text.
    bool m_sendingHtml { false };
    // Monotonic latest unstarted document intent, independent of main/initialization IDs.
    unsigned long long m_documentIntent { 0 };
    // Failed deferred commands do not erase owed outcomes or strand subsequent FIFO work.
    unsigned int m_deferredCommandFailures { 0 };
    // Original HRESULT of the last rejected deferred command.
    HRESULT m_lastDeferredCommandError { S_OK };
    // Public notification nesting, preventing in-transaction command execution.
    unsigned int m_notificationDepth { 0 };
    // Native controller call nesting, preserving engine callback/terminal ordering.
    unsigned int m_nativeCallDepth { 0 };
    // Diagnostic count of contained nonpolicy subscriber failures.
    unsigned int m_handlerFailures { 0 };
    // Prevent recursive drains when commands deliver synchronous native signals.
    bool m_draining { false };
    // Immediate public mutation rejection marker.
    bool m_closing { false };
    // Ensure the native close command is issued at most once.
    bool m_closeIssued { false };
    // Completed instance cleanup marker; late notifications cannot revive it.
    bool m_closed { false };
    // Reentrant replacement/null/Close that invalidates a synchronous proposal.
    bool m_policyInterrupted { false };
    // Hold deferred commands until a refused/failed policy proposal finishes terminally.
    bool m_awaitingPolicyTerminal { false };
    // Suppress recursive navigation during DP publication/rollback.
    bool m_updatingSource { false };
};

} // namespace WebKit

