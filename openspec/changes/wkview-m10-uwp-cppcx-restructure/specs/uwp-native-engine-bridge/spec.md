## Purpose

Provide a versioned engine boundary with typed per-page notifications and
explicit data lifetime, threading and cancellation for independently built consumers.

## ADDED Requirements

### Requirement: Versioned capability negotiation
The bridge SHALL negotiate version, structure sizes and required capabilities before page creation; incompatible contracts SHALL fail explicitly without reading incompatible layouts.

#### Scenario: Consumer requires an unavailable ABI capability
- **WHEN** the engine lacks a required capability or compatible version
- **THEN** initialization reports incompatibility before creating a page or invoking an unsupported function

#### Scenario: Two ARM compilers disagree on the proposed ABI layout
- **WHEN** producer/consumer conformance checks do not match the frozen V1 member offsets, sizes or signatures
- **THEN** integration is blocked and the contract is revised before implementation, rather than silently selecting one compiler's layout

### Requirement: Typed independent page callbacks
The bridge SHALL provide context-bearing source/history/title/navigation notifications with stable page/navigation/document identity and explicit error/status fields. Diagnostic logs SHALL NOT be a semantic event source.

#### Scenario: Two pages complete interleaved loads
- **WHEN** callbacks from independent pages arrive in interleaved order
- **THEN** each consumer receives the correct page identity and copied payload without global instance routing or log parsing

### Requirement: Payload ownership
The bridge SHALL specify length, encoding and lifetime of every payload and SHALL avoid cross-module ownership of implementation objects or allocations.

#### Scenario: A notification is queued for application dispatch
- **WHEN** an engine callback returns and its borrowed storage changes
- **THEN** the queued public notification uses owned copied values and remains valid

#### Scenario: Snapshot storage capacity is insufficient
- **WHEN** a caller requests state with an undersized string buffer
- **THEN** only the required byte count is returned, state/storage remain unchanged and the caller can retry with its own allocation

### Requirement: Safe callback detachment
Detach and Close SHALL prevent later callbacks from touching a released context; active reentrant dispatch SHALL preserve lifetime until unwound and invalidate superseded work.

#### Scenario: Close is called during navigation policy
- **WHEN** a policy handler closes its page before returning
- **THEN** the page is not accessed after release and the invalidated navigation cannot subsequently commit

#### Scenario: Delayed transport completion arrives after detachment
- **WHEN** old work completes after callback detachment
- **THEN** it cannot notify or mutate a closed or replacement page

#### Scenario: Detach is requested within active dispatch
- **WHEN** the host detaches while a callback still owns its context
- **THEN** the attachment becomes inactive immediately and its retained-context release acknowledges final unwind exactly once before host storage is freed

### Requirement: Owner-thread operations
Engine page operations SHALL be thread-affine and SHALL report an explicit wrong-thread error instead of accessing page state from another thread.

#### Scenario: Page operation is called from a foreign thread
- **WHEN** a consumer invokes a page mutation outside the owning thread
- **THEN** the operation fails explicitly and leaves page state unchanged

### Requirement: Platform completion and graphics ownership
Accepted asynchronous platform requests SHALL complete exactly once even on cancellation and retain completion context until acknowledgment. CPU presentation and GPU compositor modes SHALL have exclusive surface ownership with explicit native interface retention/release.

#### Scenario: Clipboard completion races page close
- **WHEN** Close cancels an accepted clipboard request
- **THEN** the request is acknowledged once before its context is released, and invalidated page delivery is dropped without duplicate completion

#### Scenario: Presentation switches from GPU to CPU
- **WHEN** the control switches presentation modes
- **THEN** engine-owned GPU attachment/interfaces are detached before the host CPU presenter attaches, with no two swap chains bound simultaneously
