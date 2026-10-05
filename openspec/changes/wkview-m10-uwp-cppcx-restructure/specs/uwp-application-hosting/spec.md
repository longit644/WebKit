## Purpose

Provide UWP applications and diagnostics that consume the reusable browser
component while owning their own user experience, policy and test orchestration.

## ADDED Requirements

### Requirement: Component-consuming application
MiniBrowser SHALL embed the public control declaratively and consume its properties/events rather than compile private framework implementations or maintain its own downloader/history/completion inference.

#### Scenario: User navigates from the address bar
- **WHEN** a user requests a URI and uses toolbar history actions
- **THEN** the component performs navigation/history and the app displays its public state without curl-to-HTML loading or a URL vector

### Requirement: Application-owned warning and demo UX
Applications SHALL own warning copy, explicit consent, error templates and demo assets; the reusable framework SHALL NOT select product UI. Template substitution SHALL escape untrusted content and preserve documented override behavior.

#### Scenario: Certificate verification fails
- **WHEN** MiniBrowser receives an eligible terminal failure
- **THEN** it presents its own warning and only an explicit proceed action calls the token-bound method; other errors do not offer certificate bypass

#### Scenario: Warning HTML finishes before the user proceeds
- **WHEN** BrowserPolicy receives HostHtml completion for its explicitly token-associated warning
- **THEN** it keeps the remote certificate token separately and does not replace it with the warning document's navigation ID

### Requirement: Diagnostic isolation
Diagnostic application startup and result sequencing SHALL remain outside framework production paths and preserve native probe semantics while consuming the new UWP projection boundary.

#### Scenario: A diagnostic host is migrated
- **WHEN** its application projection is replaced
- **THEN** the same native payload and documented expected results run explicitly, without introducing diagnostic behavior into MiniBrowser's ordinary startup

### Requirement: Shared component lifetime in consumers
Consumers SHALL handle suspend/resume, binding and Close using public lifecycle semantics, without directly destroying shared process engine state.

#### Scenario: One application view closes its control
- **WHEN** another control remains alive in the same app
- **THEN** the remaining view's loading and rendering continue normally

#### Scenario: Application is shutting down its dispatcher
- **WHEN** orderly host shutdown begins
- **THEN** it rejects new browser operations and drains the process cleanup/platform protocol before dispatcher destruction, without attempting engine hot-unload
