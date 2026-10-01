# Implementation Plan: Native Desktop RHI Foundation

## Overview

Create a C++20 native desktop foundation for Research Harvester. It will use a
project-owned, versioned RHI plugin boundary so desktop UI code is independent
of OpenGL and Vulkan, while the RHI can later serve a game-engine core. The
first complete vertical slice is a deterministic `null` renderer and a small
RHI-host smoke executable; OpenGL then makes the desktop UI visible, and Vulkan
is added through the same contract rather than as a parallel application path.

The approved architecture is documented in
`docs/superpowers/specs/2026-09-13-desktop-rhi-design.md` and ADR-001.

## Architecture decisions

- Public renderer plugins expose a versioned C ABI; C++ wrappers are host-local.
- Platform/windowing, WSI glue, UI screens, RHI, and renderer implementations
  are distinct targets with one-directional dependencies.
- UI code uses Dear ImGui core only. The custom RHI adapter owns ImGui drawing
  and texture lifecycle.
- Renderer choice is startup-only. A controlled restart is the sole later
  mechanism for changing renderer.
- `null` is the test oracle for contract validation; it is not a silent runtime
  fallback.
- Every capability used by the UI belongs to `UI_BASELINE`; any non-baseline
  feature must be capability-gated and tested on every supporting backend.

## Dependency graph

```text
build rules + public ABI
        |
        +--> loader + null backend + ABI/contract tests
        |          |
        |          +--> OpenGL backend + WSI glue
        |          |          |
        |          |          +--> SDL3 host + ImGui RHI adapter
        |          |                     |
        |          |                     +--> Harvester application screens
        |          |
        |          +--> Vulkan backend + WSI glue + parity tests
        |
        +--> diagnostics, packaging, CI driver matrix
```

## Task list

The detailed checklists live in `tasks/todo.md`; this is the ordered execution
index.

### Phase 1: Contract foundation

1. [x] Establish CMake project, dependency policy, generated-file ignores, and
   target topology.
2. [x] Define the C-compatible `rhi-api` headers and ABI validation utilities.
3. [x] Implement loader and `null` backend with contract tests.
4. [x] Define UI-baseline resource/upload/draw/fence operations and prove
   their lifecycle/ordering semantics in the null backend.

### Checkpoint: Contract foundation

- [x] The host rejects incompatible/malformed plugins safely.
- [x] All foundation API/handle/lifetime tests pass without a GPU.
- [x] `rhi-null` runs the declared `UI_BASELINE` command trace.

### Phase 2: First visible vertical slice

5. [x] Implement OpenGL backend and SDL3 OpenGL presentation glue against the
   same contract.
6. [x] Build platform runtime and the RHI ImGui renderer adapter.
7. [x] Build the initial Research Harvester search/list/detail flow with a typed
   application-service seam and a bounded, read-only Python search worker; the
   fake adapter remains test-only.

### Phase 2.1: Explicit open-access full-text workflow

7a. [x] Preserve a work's DOI and declared open-access URL through the desktop
    search contract, and discover only open-access/publisher/repository PDF
    candidates from source metadata, OpenAlex, OpenAIRE, Europe PMC, and
    Zenodo without database or filesystem mutation.
7b. [x] Extend the versioned Python-worker protocol and application-service
    seam for asynchronous candidate discovery and user-selected downloads,
    while retaining the current search protocol's compatibility.
7c. [x] Add native candidate and download states to the work-detail screen;
    downloads must be explicit, bounded, URL-validated, PDF-validated, and
    atomically stored beneath the application's XDG data directory.

### Checkpoint: Safe desktop full-text workflow

- [x] The desktop UI can list provenance-labelled candidates for a selected
  work, but never downloads one until the user chooses it.
- [x] The desktop worker imports neither the database configuration nor the
  CLI full-text pipeline.
- [x] Redirects, host resolution, byte limits, PDF content, and output paths
  are validated and covered by automated tests.

### Checkpoint: Usable desktop application

- [x] OpenGL app starts, renders, resizes, falls back cleanly when vsync is not
  available, and exits cleanly.
- [x] Search/list/detail/action UI flow has a real read-only Python search
  worker plus a deterministic test service adapter.
- [x] Renderer diagnostics report active backend capabilities through plain
  application data.
- UI target has no native GL/Vulkan includes or link dependencies.

### Phase 3: Backend parity and engine hardening

8. Implement Vulkan backend and SDL3 Vulkan WSI glue.
9. Add backend parity, validation, software-driver, diagnostics, and packaging
   automation.
10. Add user-facing developer documentation, compatibility matrix, and release
   versioning for the public RHI ABI.

### Checkpoint: Engine-ready RHI v1

- OpenGL and Vulkan pass common contract tests and UI smoke tests.
- The UI runs unchanged with either backend.
- ABI, feature-profile, lifetime, error, and WSI boundaries are documented and
  enforced.

## Risks and mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| No current development packages for SDL/Vulkan/OpenGL | High | Make dependency bootstrap reproducible and prove `null` foundation before GPU work. |
| RHI grows into a premature engine | High | Enforce `UI_BASELINE`; require a second backend and contract test before extending core API. |
| Dynamic plugin ABI drift | High | C ABI only, ABI negotiation tests, pinned API version and no cross-boundary allocation. |
| UI integration leaks backend objects | Medium | Compile-time target/link checks plus `UiTexture` registry in the RHI adapter. |
| Driver-specific behavior | Medium | Validation layers, llvmpipe/Lavapipe CI, explicit capability reports, manual hardware matrix. |

## Open questions resolved by default

- **Initial OS:** Linux, matching the current development environment. The
  platform/WSI separation preserves later portability.
- **Initial visible backend:** OpenGL after the `null` contract foundation.
- **Initial UI toolkit:** Dear ImGui core plus maintained SDL3 platform backend;
  rendering is project-owned through RHI.
- **Dynamic switching:** restart renderer runtime; no in-frame hot swap.

## Plan review criteria

- Every task in `tasks/todo.md` is S or M sized, has acceptance and verification
  criteria, and leaves a buildable state.
- No task begins before its dependencies are complete.
- No GPU backend starts before the C ABI and `null` contract tests are passing.
- No Python harvesting behavior is changed until the desktop application service
  seam is under test.
