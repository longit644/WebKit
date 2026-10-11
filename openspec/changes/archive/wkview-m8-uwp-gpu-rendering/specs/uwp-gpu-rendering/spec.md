## ADDED Requirements

### Requirement: Verified GPU painting backend
The UWP control SHALL distinguish GPU-backed page painting from GPU composition of CPU-painted content. GPU painting SHALL be enabled only with a backend proven to build and execute in ARM32-UWP.

#### Scenario: Cairo has no GPU backend
- **WHEN** the dependency version lacks a usable GPU surface backend
- **THEN** feasibility evidence identifies that blocker and CPU Cairo painting is not reported as GPU painting

#### Scenario: GPU painting backend initializes
- **WHEN** the selected backend creates its render target on the Lumia
- **THEN** diagnostics identify the painting backend and a probe verifies shapes, text and image output

### Requirement: Direct GPU texture composition
GPU-painted page content SHALL be passed to TextureMapper through GPU-backed resources without routine CPU readback or CPU image extraction and re-upload.

#### Scenario: A painted tile is composed
- **WHEN** GPU painting completes for a tile
- **THEN** TextureMapper samples the compatible texture with valid synchronization and lifetime

### Requirement: Correct cached scrolling
The control SHALL reuse unchanged cached content during ordinary scrolling and SHALL preserve correct document, fixed and sticky positioning and coherent edge behavior.

#### Scenario: Repeated scrolling over cached static content
- **WHEN** the viewport moves over unchanged cached document tiles
- **THEN** scroll position is reflected by compositing transforms without rerasterizing unrelated content

#### Scenario: End-of-page pull
- **WHEN** the document reaches its scroll limit and elastic feedback is active
- **THEN** the document and its layers present coherently without composited labels moving over a stationary host-painted page

### Requirement: Bounded graphics resource usage
The GPU path SHALL use bounded tile coverage and resource retention and SHALL release resources safely when a page or context is destroyed.

#### Scenario: A large document is scrolled
- **WHEN** the document exceeds the tile coverage budget
- **THEN** coverage/eviction respects the budget and newly exposed content renders correctly

### Requirement: Graphics lifecycle and fallback
The control SHALL handle viewport/DPI changes, suspension and device/context failures, and SHALL report and select a working fallback when the GPU backend cannot continue.

#### Scenario: GPU initialization fails
- **WHEN** a GPU context or painting surface cannot be created
- **THEN** the error and active Cairo/D3D11 fallback are observable and fallback is not reported as GPU painting

#### Scenario: Viewport or device state changes
- **WHEN** the host resizes, changes DPI, resumes or loses its graphics context
- **THEN** resources are updated or recreated without stale/corrupt page output

### Requirement: Measured rendering acceptance
M8 acceptance SHALL include visual correctness, complete active-frame timing, presentation cadence and owner device confirmation. The local static demo SHALL target sustained 60 Hz warmed scrolling with p95 complete active-frame CPU work below 16.7 ms; a successful swap or CPU submission average alone SHALL NOT establish 60 FPS.

#### Scenario: Scrolling is profiled
- **WHEN** dragging, inertia and edge motion are tested on the Lumia
- **THEN** evidence records tile raster/upload work, complete callback timing, frame intervals and missed refreshes, with warm-up costs separated

#### Scenario: Rendering correctness is reviewed
- **WHEN** text, SVG, images, transparency, clipping and DOM changes are tested
- **THEN** comparisons and owner observations verify correct output without ghosting or misplaced layers
