## Purpose

Provide trustworthy host/device acceptance with replacement fixture oracles,
package-specific evidence and explicit owner approval for the restructured UWP port.

## ADDED Requirements

### Requirement: Replacement coverage before extraction
Every current engine-embedded fixture assertion SHALL map to a replacement public-runner, fixture-observable or internal test, or an explicit deferred gap, before old coverage is removed.

#### Scenario: A fixture phase is removed from production code
- **WHEN** implementation proposes removing that assertion
- **THEN** its replacement ledger contains matching verified evidence or an explicitly approved gap, rather than assuming test termination proves behavior

### Requirement: Isolated aggregate fixture results
The runner SHALL isolate results by run/scenario/assertion identity and SHALL fail on any required assertion failure, timeout, missing or malformed report. Cookie/credential values SHALL NOT be included in diagnostics.

#### Scenario: One assertion fails but the runner exits normally
- **WHEN** remaining assertions finish and the run terminates
- **THEN** the aggregate result is failure and cannot be reported as suite success

### Requirement: Appropriate behavioral oracles
Visual styling, fonts, security response exposure, history and cancellation SHALL use evidence that actually demonstrates the claimed behavior, not text presence or successful server response alone.

#### Scenario: The CSS fixture heading exists without style application
- **WHEN** the expected text appears but required styling is absent
- **THEN** the visual/style assertion fails independently of document-text success

### Requirement: Package-specific device acceptance
Completion SHALL include real phone evidence for XAML, lifecycle, multi-instance, input/DPI, controlled navigation/resource/security regressions and live-site observations, with exact build/package identity and owner acceptance.

#### Scenario: Specification validation succeeds without a new phone run
- **WHEN** planning artifacts validate but required device scenarios have not run
- **THEN** implementation tasks and milestone completion remain pending

#### Scenario: A live site uses unsupported functionality
- **WHEN** Wikipedia or the owner's GitHub repository is exercised
- **THEN** interactions and failed/unsupported resources are recorded precisely instead of claiming universal site compatibility
