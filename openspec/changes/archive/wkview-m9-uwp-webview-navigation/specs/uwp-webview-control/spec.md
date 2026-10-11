## Purpose

The v1 surface and deferred-feature boundary are governed by
`../../control-contract.md`.

Provide an embeddable UWP XAML web control that owns web-engine behavior while applications implement user experience and explicit policy decisions.

## ADDED Requirements

### Requirement: Declarative XAML construction
Applications SHALL be able to construct the public WebKitWebView control from XAML with a namespace-qualified element, set its Source, and place it in normal application layout without manually attaching an engine surface.

#### Scenario: XAML page contains a web control
- **WHEN** an application loads XAML containing `<web:WebKitWebView Source="https://example.com" />` with the declared control namespace
- **THEN** the control activates, participates in layout, and navigates after initialization without application-owned HTML fetching

#### Scenario: Application uses WebView2-style layout markup
- **WHEN** the application declares `<controls:WebKitWebView x:Name="MyWebView" Grid.Row="1" Grid.ColumnSpan="2" Source="https://www.microsoft.com" HorizontalAlignment="Stretch" VerticalAlignment="Stretch" />` with `xmlns:controls="using:WebKit"`
- **THEN** the control occupies the requested grid layout and begins navigation without additional engine-surface attachment code

### Requirement: Reusable navigation API and observable state
The v1 control SHALL expose Source, EnsureInitializedAsync, GoBack, GoForward, Reload, Stop, CanGoBack, CanGoForward, IsLoading and DocumentTitle, together with NavigationStarting, SourceChanged, HistoryChanged, NavigationCompleted and DocumentTitleChanged. `Navigate(uri)` is not a separate v1 method; setting Source requests navigation. ContentLoading is deferred. Public state changes SHALL be observable by application bindings or events. Source assignment SHALL trigger navigation and invalid URIs SHALL produce an explicit argument error.

#### Scenario: Application builds a toolbar
- **WHEN** the application binds an address field and history buttons to control properties and calls navigation methods
- **THEN** the properties and notifications describe the current engine page and the application does not maintain a separate document history or downloader

### Requirement: Initialization
The control SHALL initialize lazily for an assigned Source and expose EnsureInitializedAsync for explicit initialization. Concurrent initialization requests SHALL share an attempt and report initialization failure.

#### Scenario: Source is set before initialization
- **WHEN** XAML or application code assigns Source before engine initialization completes
- **THEN** the control initializes and applies the current requested Source when ready, without navigating superseded values

### Requirement: Explicit HTML and close convenience methods
The control SHALL expose NavigateToString for app-supplied HTML under its documented origin/base rules and an idempotent Close method for cancellation/disposal. Closing SHALL release page work and reject further navigation/script operations on that closed instance.

#### Scenario: Host closes the control twice
- **WHEN** application code calls Close repeatedly while resource requests are pending
- **THEN** cleanup occurs safely once and later operations fail explicitly rather than using released engine state

### Requirement: Application and engine responsibility boundary
The control SHALL own document loading, resource loading, web-content execution, rendering and page input mechanisms. Applications SHALL provide toolbar/tab/bookmark UI and explicit TLS certificate-consent decisions through the v1 public contract without accessing engine internals.

#### Scenario: An eligible certificate failure is explicitly approved
- **WHEN** the application elects to proceed for the current eligible certificate-failure token
- **THEN** the control retries only that HTTPS target once and does not weaken verification globally

### Requirement: Control ownership and teardown
The control SHALL deliver public callbacks on its owning UI dispatcher and safely cancel or detach page work when closed. Multiple control instances SHALL have independent page state and independent ephemeral sessions in v1. Explicit sharing and persistence are deferred.

#### Scenario: A control is removed during loading
- **WHEN** the application disposes a control while network requests or event deferrals remain pending
- **THEN** pending work cannot access released control/application state or update another control

#### Scenario: Two controls use different pages
- **WHEN** two controls navigate independently
- **THEN** source, title, history and rendering events are associated with the correct instance

### Requirement: Skia rendering baseline
MiniBrowser deployments SHALL use the selected Skia backend and retain observable rendering-backend diagnostics. Normal navigation SHALL preserve correct device-scale rendering, sticky/fixed positioning and the accepted input/scroll behavior.

#### Scenario: Packaging selects a non-Skia browser engine
- **WHEN** MiniBrowser staging selects an engine configured without Skia
- **THEN** staging fails explicitly before producing a deployable candidate

#### Scenario: A remote page scrolls
- **WHEN** a styled remote page is displayed and scrolled on the phone
- **THEN** rendering uses the selected backend and text, images and pinned layers remain visually correct

## Deferred beyond v1

These are recorded follow-ups, not v1 requirements. Each needs its own
requirement, scenarios and device evidence before adoption.

- **Host script execution** (`ExecuteScriptAsync`): returns a JSON-encoded result
  or an explicit error; must not replace normal resource loading; must define
  cancellation when navigation or `Close` supersedes the script, and prevent
  results being attributed to a replacement document.
- **New-window, download and permission requests**: typed app decisions with
  documented fail-closed behavior when unhandled; must never silently launch
  applications or write files.
- **Persistent and shared sessions**: application-scoped profiles, explicit
  sharing, and user-controlled clearing, with consistent engine/transport
  cookie state.
- **Content loading / DOMContentLoaded events** and any additional navigation
  stages the engine can source.
