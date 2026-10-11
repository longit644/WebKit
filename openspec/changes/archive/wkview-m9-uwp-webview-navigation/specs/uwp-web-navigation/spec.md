## Purpose

Provide real document navigation with correct addresses, responses, history, load outcomes and cancellation for embedded UWP web content.

## ADDED Requirements

### Requirement: Normal remote document navigation
Remote navigation SHALL retain the requested URI, effective committed URI, document base URI and web origin according to normal browser rules. Redirects and link navigation SHALL be handled by the control rather than by downloading HTML into a local document.

#### Scenario: Redirected page contains relative resources
- **WHEN** navigation redirects to another document URI and that response contains relative resource URLs
- **THEN** each redirect hop uses the same navigation identifier, the control exposes the effective committed URI, and resources resolve against the final document base URI

#### Scenario: User follows a same-window link
- **WHEN** the user taps an ordinary supported link in the page
- **THEN** navigation occurs in the current control with appropriate address/history updates

### Requirement: Response fidelity
Navigation SHALL honor response status, MIME type and text encoding, distinguish transport failure from HTTP error documents, and provide an explicit policy outcome for unsupported content types.

#### Scenario: An HTTP error response contains HTML
- **WHEN** a server returns a 404 HTML document
- **THEN** the document can render and the application can observe HTTP status 404 without a false DNS or TLS failure

#### Scenario: Transport cannot reach the server
- **WHEN** DNS resolution, connection or certificate verification fails
- **THEN** navigation reports a terminal categorized failure and does not report successful document loading

### Requirement: Engine-owned navigation history
Back, Forward and Reload SHALL use the actual page navigation history and preserve supported same-document/history behavior. CanGoBack and CanGoForward SHALL reflect available entries.

#### Scenario: Navigate back and forward
- **WHEN** a user visits two documents, goes back, and then goes forward
- **THEN** the correct document entries and history-availability properties are restored

#### Scenario: Reload the current entry
- **WHEN** the application requests Reload
- **THEN** the current document is reloaded without creating a spurious additional history entry

### Requirement: Ordered and cancellable navigation outcomes
Each started main-document navigation SHALL have an identifier, observable loading state and exactly one terminal success, failure or cancellation outcome. Completion SHALL correspond to the normal document load boundary, not indefinite network-idle. Superseded loads SHALL NOT overwrite the current navigation state.

#### Scenario: Stop a slow document
- **WHEN** the application calls Stop during navigation
- **THEN** loading becomes inactive and the navigation receives a cancellation outcome with no subsequent success for the same identifier

#### Scenario: Replace navigation while resources are pending
- **WHEN** the application navigates to a new document before the old load completes
- **THEN** late callbacks from the old load do not change the new document's address, title or load outcome

### Requirement: App-supplied HTML has an explicit contract
The control SHALL expose NavigateToString for host-supplied HTML using the v1 isolated opaque-origin/about:blank contract in `control-contract.md`. V1 has no base-URI parameter; a later base-URI feature SHALL NOT by itself grant a trusted remote origin. Remote URL navigation SHALL NOT use this API internally.

#### Scenario: Application supplies HTML
- **WHEN** the application calls NavigateToString while a remote document is active
- **THEN** the active main navigation is cancelled/superseded, supplied markup loads in an isolated app document with effective Source `about:blank`, and it gains no privilege from the previous remote document
