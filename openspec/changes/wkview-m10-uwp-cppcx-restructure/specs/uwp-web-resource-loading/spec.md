## Purpose

Provide asynchronous engine-managed resource loading with isolated sessions,
bounded policies, security enforcement and explicit application certificate consent.

## ADDED Requirements

### Requirement: Normal asynchronous resource loading
External stylesheets, scripts, images, fonts and supported fetch/XHR SHALL use the engine resource path with response metadata and incremental delivery; progression SHALL NOT depend on active rendering or an application downloader.

#### Scenario: An idle or unloaded control has delayed resources
- **WHEN** a page has delayed requests while presentation is paused
- **THEN** requests and owner-thread completion continue within policy without rendering-based completion polling

### Requirement: Independent ephemeral sessions
Each control SHALL own independent ephemeral cookie/session/certificate state; closing one SHALL NOT change another. Sharing/persistence SHALL NOT be implied by a process-wide loader.

#### Scenario: Two controls use the same fixture origin
- **WHEN** one page sets cookies and the other requests cookie state
- **THEN** the second page does not see the first page's session data unless a separately specified sharing feature exists

### Requirement: Engine security enforcement
CORS, CSP, mixed-content and origin restrictions SHALL be enforced by the engine's actual loading semantics, without app-side scraping, resource injection or security bypass.

#### Scenario: A fixture requests forbidden cross-origin or mixed content
- **WHEN** browser policy disallows response exposure or loading
- **THEN** the forbidden operation remains blocked and test results distinguish policy failure from transport failure

### Requirement: Explicit token-bound certificate consent
TLS verification SHALL fail closed by default. ProceedWithCertificateFailure SHALL accept only an opaque, nonconstructible certificate-failure token bound to its issuing control/page-session/failure revision, consume it once and retry only the validated target with session/HTTPS host/effective-port scoped enforcement. Numeric page-local navigation IDs SHALL NOT establish token identity. No global flag or reserved document URL SHALL grant consent.

#### Scenario: A certificate-failed target is explicitly approved
- **WHEN** the host supplies the current eligible token after user consent
- **THEN** a new navigation ID retries that target and unrelated origin/session requests retain normal verification

#### Scenario: Token is stale or belongs to another control
- **WHEN** a stale, used, cross-control or non-certificate token is submitted
- **THEN** the request fails explicitly without weakening any request's verification

#### Scenario: Two controls fail at the same local navigation ordinal
- **WHEN** A and B both have failed navigation ID 1 and A's token is submitted to B
- **THEN** B rejects it even though the numeric navigation IDs coincide

### Requirement: Explicit warning-document decision preservation
Only NavigateToString(html, exactCurrentWarningToken) SHALL preserve an eligible decision through warning-document proposals, vetoes and commits. Plain HTML and subsequent remote/history/reload/Stop/null commands SHALL invalidate it on execution; rejected arguments SHALL have no side effect. Warning HTML IDs SHALL remain separate from the failed remote ID.

#### Scenario: Host displays warning HTML before consent
- **WHEN** an eligible remote failure is followed by explicitly token-associated warning HTML and then Proceed
- **THEN** warning HTML has its own HostHtml events, the original token remains eligible and retry consumes it under a new Remote navigation ID

#### Scenario: Warning is replaced by unrelated content
- **WHEN** the host loads plain HTML or executes unrelated navigation before attempting Proceed
- **THEN** the old token is invalid and cannot arm a certificate exception

### Requirement: Immutable trust and explicit global fonts
Each session SHALL snapshot CA-file-only trust and each request SHALL use that policy without mutating process SSL state or sharing TLS/connection reuse across sessions. Native OS trust SHALL NOT silently augment the bundle. Fonts SHALL use one process-global canonical directory/content fingerprint; conflicting later configuration SHALL fail explicitly without modifying existing pages.

#### Scenario: Two controls use different trust bundles
- **WHEN** the same server certificate is trusted by A's bundle but not B's
- **THEN** A and B receive different correct verification outcomes even with concurrent requests or TLS reuse attempts

#### Scenario: Later control requests conflicting fonts
- **WHEN** a successful process font setup exists and another control requests a different canonical directory/content
- **THEN** its initialization reports configuration conflict before page creation and existing controls' font behavior stays unchanged

### Requirement: Bounded and distinguishable loading policies
The session SHALL apply the bounded option ranges in [the public contract](../../artifact/public-api.md), defaulting to 8 transfers, 32 MiB decoded response-body bytes per resource, 16 MiB transient undelivered buffering, 128 queued requests, 10 redirects and a 30-second deadline including queue/redirect time. Explicit app suspension SHALL pause deadlines; visual unload SHALL NOT. Queue/buffer/resource/redirect/transport exhaustion SHALL remain distinguishable and limits SHALL be checked before copying/allocating.

#### Scenario: A document exceeds its resource budget
- **WHEN** the fixture's HTML main-document response exceeds the configured limit
- **THEN** the load terminates with a budget error distinguishable from unsupported MIME, DNS, TLS and timeout errors without unbounded buffering

#### Scenario: Compressed content or deferred delivery exceeds a bound
- **WHEN** decoded body bytes or worker-to-owner undelivered backlog exceed their respective limits
- **THEN** the appropriate resource/buffer error terminates the offending request before unchecked memory growth, without claiming a total DOM/cache memory cap

### Requirement: Lifetime-safe resource cancellation
Stop, replacement and teardown SHALL cancel outstanding resource work and prevent stale delivery from modifying a replacement document or released session.

#### Scenario: Page closes before a delayed response
- **WHEN** delayed network work completes after Close
- **THEN** no bytes, terminal callback or session mutation reaches the released page or another control
