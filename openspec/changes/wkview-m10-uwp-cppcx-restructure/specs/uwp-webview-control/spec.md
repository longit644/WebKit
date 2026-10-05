## Purpose

Provide a reusable declarative browser control with observable navigation state,
shared initialization and instance-safe visual, input and page lifecycle.

## ADDED Requirements

### Requirement: Declarative control and observable state
WebKit.WebKitWebView SHALL support URI Source assignment in UWP XAML and expose read-only bindable CanGoBack, CanGoForward, IsLoading and DocumentTitle. Internal Source changes SHALL NOT recursively navigate.

#### Scenario: Application declares a control and binds toolbar state
- **WHEN** XAML contains a namespace-qualified WebKitWebView with Source and layout properties
- **THEN** the control participates in layout and updates bindings from its engine page without app-owned surface attachment

### Requirement: Shared lazy initialization
The control SHALL initialize lazily and expose EnsureInitializedAsync with a distinct async action per call over one shared internal attempt, explicit failures/retry and only the latest queued Source. Configuration SHALL be frozen when initialization begins. Success SHALL mean controller/page readiness, not completion of the first network navigation; live joiners SHALL resolve before queued navigation is scheduled.

#### Scenario: Source changes while initialization is pending
- **WHEN** a host assigns several Source values and concurrently requests initialization
- **THEN** the joiners observe one initialization attempt and only the latest pending target begins navigation

#### Scenario: Initialization fails and is retried
- **WHEN** an initialization attempt fails and the host explicitly retries
- **THEN** all original joiners receive failure and the retry uses the same validated frozen configuration without leaking the previous page

#### Scenario: One initialization caller cancels or closes its action
- **WHEN** two callers observe the same attempt and one cancels or closes its own async action
- **THEN** that caller's observation terminates independently while the other action and Source-driven initialization continue

#### Scenario: Control closes before initialization readiness
- **WHEN** Close occurs with unresolved initialization actions
- **THEN** all live joiners and the shared attempt are canceled, and no later attempt retroactively succeeds those actions

### Requirement: Idempotent instance close
Close SHALL reject new work, terminate pending operations, detach callbacks and release instance resources once, while preserving other controls. Read-only final snapshots SHALL remain safe to read.

#### Scenario: Two instances load and one closes twice
- **WHEN** one host closes a loading control repeatedly
- **THEN** that control cleans up safely once, new operations fail as closed and the other instance continues normally

#### Scenario: Close occurs inside a public event
- **WHEN** an application handler calls Close
- **THEN** dispatch unwinds safely and stale work cannot access released control or page state

### Requirement: Unload and suspension differ from close
Temporary visual unloading or app suspension SHALL pause presentation safely without implicitly permanently closing a reusable control. Resume SHALL restore subscriptions/resources without duplicate handlers.

#### Scenario: An unclosed control is removed and reinserted
- **WHEN** the host reparents the control during loading
- **THEN** the page remains valid, networking can progress and presentation resumes with one set of subscriptions

### Requirement: Input and device-scale presentation
The control SHALL own its rendering child and translate measured size, DPI, pointer capture/cancel, keyboard and focus to the correct page. Multiple controls SHALL NOT duplicate shared engine service work.

#### Scenario: A loaded control is resized and receives input
- **WHEN** DPI/layout changes and pointer/keyboard focus events occur
- **THEN** its page renders at the correct scale and receives correctly scoped input without affecting another control

### Requirement: Dispatcher-bound public API
Public events SHALL execute on the owning UI dispatcher with coherent copied state; wrong-thread mutations SHALL report RPC_E_WRONG_THREAD and closed mutations SHALL report RO_E_CLOSED.

#### Scenario: A worker tries to navigate a closed or foreign-thread control
- **WHEN** an operation violates thread/lifetime requirements
- **THEN** an explicit corresponding WinRT error is returned without mutating another apartment or freed page

### Requirement: Dispatcher-loss cleanup ownership
A process runtime owner SHALL retain cleanup resources before dispatcher enqueue and quiesce page callbacks, workers, platform operations and process hooks before orderly dispatcher shutdown. Final-release/shutdown SHALL NOT invoke public handlers or perform foreign-thread XAML teardown. Engine code SHALL remain process-pinned; ordinary Close/recreate SHALL still release per-page resources.

#### Scenario: Cleanup enqueue fails during dispatcher shutdown
- **WHEN** final-release cleanup cannot enqueue a wake
- **THEN** its ticket remains owned, public delivery is suppressed, controlled owner-thread shutdown drains work when available and no context or module becomes a dangling callback target

#### Scenario: Owner thread has already been destroyed by the OS
- **WHEN** safe apartment cleanup is irreversibly unavailable
- **THEN** orphan native tickets remain explicitly quarantined until process exit without app calls, wrong-thread XAML teardown or module unloading; this fallback is reported rather than used for normal Close
