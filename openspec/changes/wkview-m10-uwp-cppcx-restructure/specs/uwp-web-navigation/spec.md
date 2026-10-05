## Purpose

Provide normal document navigation with stable identities, engine-owned history,
accurate source/title changes, explicit failures and isolated host-supplied HTML.

## ADDED Requirements

### Requirement: Requested and committed Source
Source SHALL contain the pending requested target until a commit updates it to the engine's effective URI; pre-commit refusal/failure SHALL restore the previous committed URI or null. SourceChanged SHALL report actual engine source changes, including engine-classified same-document mutations.

#### Scenario: Redirect commits another URI
- **WHEN** an accepted request redirects and commits
- **THEN** Source becomes the final effective URI and its internal update causes no additional navigation

#### Scenario: Policy refuses the requested target
- **WHEN** a new target is rejected before commit
- **THEN** Source returns to the prior committed URI and does not claim that the refused target committed

#### Scenario: Null Source while initialization is pending
- **WHEN** Source is set to null before any proposal has started
- **THEN** queued target intent is removed without canceling shared initialization or creating navigation events

#### Scenario: Null Source during provisional navigation
- **WHEN** a started target has not committed and the host sets Source to null
- **THEN** that navigation terminates once as Cancelled and Source restores the previous committed URI or null without erasing history

#### Scenario: Null Source after commit
- **WHEN** a committed document still has loading work and Source is set to null
- **THEN** outstanding work stops while the committed URI/document remains; no duplicate terminal event occurs if main navigation already ended

#### Scenario: Invalid dependency-property assignment
- **WHEN** Source is assigned an unsupported URI or scheme
- **THEN** the previous published value is restored before E_INVALIDARG and no navigation or certificate-token side effect occurs

### Requirement: Exactly one terminal outcome per started navigation
Each NavigationStarting SHALL carry a stable nonzero page-scoped ID and record exactly one terminal outcome of Completed, Failed or Cancelled, delivered once through NavigationCompleted while a live consumer remains. Explicit Close SHALL finish owed delivery before revocation; final-release/process-shutdown cleanup without a live consumer SHALL record cancellation without invoking app handlers. Redirects SHALL retain the ID; superseded/stopped work SHALL NOT later succeed under that ID.

#### Scenario: Slow navigation is replaced
- **WHEN** a replacement target supersedes an active slow navigation
- **THEN** the old ID terminates once as Cancelled before the replacement begins and delayed old completion cannot update the new document

#### Scenario: Policy vetoes or reentrantly replaces a navigation
- **WHEN** a Starting handler cancels, replaces or closes the control
- **THEN** invalidated work does not start loading and its ID receives no contradictory success event

### Requirement: Engine history and same-document classification
Back/Forward/Reload and history availability SHALL follow engine history; fragment, pushState, replaceState and same-document traversal SHALL be classified by engine signals, not URL string heuristics. Reload SHALL create a new navigation ID without adding a history entry.

#### Scenario: Reload uses the same URL
- **WHEN** the current URL is reloaded
- **THEN** it is a new-document navigation and not classified as a fragment or same-document mutation merely because the URL matches

#### Scenario: Script replaces a same-document history state
- **WHEN** replaceState updates the URL after pushState
- **THEN** Source/history notifications reflect engine mutation and Back/Forward traverses actual entries without a shell history vector

### Requirement: Error domain and response fidelity
Terminal events SHALL distinguish transport, adapter, engine and platform errors and SHALL explicitly identify HTTP-status presence. MIME/encoding/origin/relative URL semantics SHALL come from the normal engine response path.

#### Scenario: Server returns a valid HTML 404 document
- **WHEN** an HTTP 404 HTML response renders normally
- **THEN** navigation completes successfully with status 404, rather than being reported as a transport failure

#### Scenario: A committed document fails
- **WHEN** the main navigation fails after commit but before terminal completion
- **THEN** the same navigation ID receives one Failed outcome with the correct domain/code, not an unrelated provisional-only report

### Requirement: Isolated host HTML
Both NavigateToString overloads SHALL supersede active work and load an opaque-origin about:blank substitute document without remote privileges or implicit remote base URL. Host HTML SHALL have a distinct page-local navigation ID, HostHtml classification, cancellable Starting and exactly one terminal outcome with absent HTTP status. It SHALL leave existing engine history entries and cursor unchanged; Back/Forward use existing neighbors and Reload reloads the current host text without history insertion.

#### Scenario: Host displays HTML after a remote page
- **WHEN** an application supplies HTML through NavigateToString
- **THEN** Source becomes about:blank and the new document cannot inherit the remote page's origin or be used as the remote downloading path

#### Scenario: Host HTML is vetoed or reloaded
- **WHEN** the application vetoes a HostHtml proposal or later reloads accepted host HTML
- **THEN** veto retains the previous document with one Cancelled outcome; reload uses a new HostHtml ID, and neither inserts/replaces an engine history item

### Requirement: Transactional reentrant notification ordering
The control SHALL mark terminal obligations before publishing state, complete each notification transaction before executing deferred commands, and never drop an owed terminal event solely because a later command invalidates an engine generation. Close SHALL reject new work immediately but finish live-consumer owed terminal delivery before revocation; destruction without a live consumer SHALL NOT invoke app handlers.

#### Scenario: PropertyChanged requests replacement navigation
- **WHEN** A publishes IsLoading=false and its PropertyChanged handler requests B
- **THEN** Completed(A) precedes Starting(B), and A's snapshot remains coherent throughout its transaction

#### Scenario: PropertyChanged closes the control before terminal delivery
- **WHEN** a terminal state notification handler calls Close before Completed(A)
- **THEN** A's already-recorded terminal event is raised once before subscriptions detach, with no second terminal A and no replacement command execution

#### Scenario: Starting handler mutates navigation or throws
- **WHEN** a Starting handler requests replacement/Stop/null/Close or throws
- **THEN** invalidated proposals do not load; cancellation takes precedence, otherwise handler failure yields Failed/Platform without an exception crossing the native boundary

### Requirement: Title and loading state accuracy
Title, loading and history notifications SHALL be engine-driven and coherent before event dispatch. A failed subresource SHALL NOT automatically produce a main-navigation failure or a second terminal event after main completion.

#### Scenario: One image fails on an otherwise completed page
- **WHEN** an image request fails without failing the main document
- **THEN** the control retains the completed navigation outcome and reports resource failure separately from main-navigation identity
