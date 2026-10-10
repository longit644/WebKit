## Purpose

Provide reproducible ARM32 component/application builds and matching packages with explicit compiler, metadata, runtime, engine and asset dependency identity.

## ADDED Requirements

### Requirement: Explicit compatible build domains
Builds SHALL isolate native engine flags from component/application projection flags and SHALL record compiler, SDK, runtime and target architecture. Missing ARM32 dependencies SHALL fail explicitly before packaging.

#### Scenario: Only ARM64 libraries are available
- **WHEN** an ARM32 build discovers no matching SDK/runtime link surface
- **THEN** it fails with the missing dependency and does not substitute another architecture or dummy runtime support

### Requirement: Single metadata authority and generated output placement
The deployed component SHALL use generated metadata matching its implementation; conflicting historical metadata SHALL NOT be staged. Generated metadata/XAML/binary intermediates SHALL remain in build output directories.

#### Scenario: Packaging finds independently generated conflicting WinMD
- **WHEN** metadata does not match the selected component contract/build
- **THEN** staging rejects the candidate instead of publishing ambiguous runtime identities

### Requirement: Matching engine and dependency closure
Staging SHALL verify engine ABI/build identity, component contract revision, ARM machine/import closure and required CA/font/HTML assets. MiniBrowser SHALL reject a non-Skia engine.

#### Scenario: Candidate mixes incompatible engine or graphics backend
- **WHEN** a component is staged with a mismatched engine ABI or Cairo MiniBrowser engine
- **THEN** no deployable candidate is produced and the mismatch is reported

### Requirement: Reproducible configuration and rollback
Build tooling SHALL discover installation paths or accept explicit overrides rather than embed developer-profile paths, and SHALL preserve identity/evidence sufficient to restore a matching prior package.

#### Scenario: Another machine builds the same configuration
- **WHEN** supported tool overrides and source revision are supplied
- **THEN** the documented commands resolve dependencies without the original developer's home directory and produce an auditable build identity
