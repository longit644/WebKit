## Purpose

The v1 profile/session boundary and other deferred features are governed by
`../../control-contract.md`.

Load web resources asynchronously with response fidelity, browser security, session isolation and bounded resource usage on ARM32 UWP devices.

## ADDED Requirements

### Requirement: External page resources and script requests
The control SHALL load external CSS, JavaScript, images and supported web fonts and support script-originated fetch/XHR through normal browser resource-loading semantics. Resource completion SHALL preserve script ordering and document load behavior without application-owned resource injection.

#### Scenario: Page references external style script and image
- **WHEN** a controlled page references a stylesheet, an external script and an image through relative URLs
- **THEN** the style visibly applies, the script executes in the correct document, and the image renders

#### Scenario: Script requests data
- **WHEN** a page issues a permitted fetch or XMLHttpRequest
- **THEN** response data or a categorized failure reaches the script through the expected web API

### Requirement: Asynchronous loading and cancellation
Resource loading SHALL avoid blocking UI input with network I/O, deliver engine callbacks on the owning thread, and support cancellation on Stop, superseding navigation and control disposal. Request progress SHALL NOT depend on rendering-event polling.

#### Scenario: Slow resource while the page is visible
- **WHEN** a stylesheet or image response is delayed
- **THEN** input remains responsive and completion is delivered through resource lifecycle callbacks

#### Scenario: Resource completes after cancellation
- **WHEN** a backend response arrives after its page load was cancelled
- **THEN** it cannot mutate a replacement document or released control

### Requirement: Browser transport and origin security
HTTPS certificate and hostname verification SHALL remain enabled for document and resource requests. Browser origin, CORS, CSP and mixed-content rules SHALL be enforced consistently for markup resources and script requests, without permissive bypasses to make a live site render.

#### Scenario: Invalid HTTPS certificate
- **WHEN** a document or subresource presents an invalid certificate
- **THEN** the load fails explicitly rather than retrying with verification disabled

#### Scenario: Cross-origin script request lacks authorization
- **WHEN** script requests a cross-origin response without required CORS authorization
- **THEN** the response is unavailable to script under browser security rules

#### Scenario: Document policy blocks a resource
- **WHEN** a resource violates the document's applicable CSP or mixed-content policy
- **THEN** the engine blocks it and reports the reason without weakening the policy

### Requirement: V1 ephemeral session isolation
Each v1 control SHALL use an independent ephemeral session for cookies and supported origin-scoped storage. Controls SHALL NOT implicitly share state. Persistent profiles, explicit session sharing and profile clearing are deferred to a later version; when added, engine and transport state SHALL remain consistent.

#### Scenario: Response sets a cookie
- **WHEN** a permitted response sets a cookie and a later request in the same session matches its scope
- **THEN** subsequent document/resource requests use the same cookie state under browser cookie rules

#### Scenario: Independent sessions navigate to the same site
- **WHEN** two controls use distinct sessions
- **THEN** their cookies and origin storage are not implicitly shared

#### Scenario: A control closes
- **WHEN** a control closes and a new control is created
- **THEN** the new control starts with an independent empty ephemeral session

### Requirement: Bounded requests and meaningful diagnostics
The control SHALL bound concurrency, buffering, redirect chains and request duration, stream resources where supported, and report resource-limit failures explicitly. Diagnostics SHALL distinguish DNS, connection, TLS, HTTP, cancellation and policy failures and correlate requests with navigation identifiers without logging credentials or cookie contents.

#### Scenario: Response exceeds the configured budget
- **WHEN** a response exhausts a documented resource budget
- **THEN** the affected load fails with a resource-limit outcome rather than silently truncating its data or misreporting TLS failure

#### Scenario: One image fails in a loaded page
- **WHEN** the main document succeeds but an image request fails
- **THEN** the page remains usable and diagnostics identify the failed resource separately from main-document transport success

### Requirement: Device acceptance with controlled and live pages
Acceptance SHALL include deterministic tests for resource loading/security/cancellation and on-device Wikipedia/GitHub checks. Evidence SHALL record exact tested URLs, package/backend versions, resource failures and owner visual/interaction observations; successful main-HTML fetching alone SHALL NOT establish full-page support.

#### Scenario: Candidate is evaluated on the phone
- **WHEN** controlled fixtures and live-site navigation are tested
- **THEN** external styling, scripts, images, links and navigation outcomes are checked and remaining unsupported features are recorded explicitly
