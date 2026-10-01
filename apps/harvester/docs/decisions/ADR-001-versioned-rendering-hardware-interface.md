# ADR-001: Use a versioned RHI plugin boundary for native desktop rendering

## Status

Accepted

## Date

2026-09-13

## Context

Research Harvester needs a native C++ desktop interface today. Its rendering
foundation must later support an independent game-engine core. Qt and web UI
technologies are excluded. The project requires OpenGL and Vulkan to be
selectable without exposing either API to UI or application code.

## Decision

Create a project-owned Rendering Hardware Interface (RHI) with a versioned,
C-compatible plugin ABI. Renderer implementations are dynamically loaded shared
libraries. Application/UI code depends only on the RHI C++ wrapper and a
renderer-neutral UI adapter. The single unversioned exported entry point accepts
the requested ABI version, while each returned table carries its own version and
size. This lets compatible additions stay additive and makes changed mandatory
semantics an explicit new ABI request.

SDL3 is used only as the initial platform/window/input layer. Dear ImGui core is
used for the first developer-oriented desktop screens. Its SDL3 platform backend
is kept separate from the custom renderer adapter, which renders ImGui draw data
through the RHI.

The first renderer implementations are `null` for deterministic contract tests
and OpenGL for the first usable UI. Vulkan follows as a separately verified
backend-parity increment. Backend selection happens at startup; changing it
requires an orderly renderer restart rather than unsafe hot unload.

## Alternatives considered

### Use Qt

- Pros: complete widgets and mature desktop integration.
- Cons: explicitly excluded; makes the desired engine-oriented renderer boundary
  harder to own.
- Rejected: conflicts with product constraints.

### Use SDL3 GPU as the RHI

- Pros: portable graphics abstraction with a convenient device API.
- Cons: documented driver choices are Vulkan, D3D12, and Metal; it does not
  deliver the required OpenGL backend.
- Rejected: cannot meet the requested OpenGL/Vulkan substitution model.

### Use bgfx directly

- Pros: mature, API-agnostic, broad backend coverage.
- Cons: its API becomes the future engine contract; plugin/lifecycle/capability
  decisions remain external to this project.
- Rejected: valuable reference, but incompatible with the requirement to own
  the abstraction layer.

### Directly link OpenGL and add Vulkan later

- Pros: smallest initial code volume.
- Cons: native graphics types leak into application code and make an eventual
  Vulkan path expensive and disruptive.
- Rejected: violates the primary architectural constraint.

## Consequences

- The project gains a stable engine-reusable boundary with explicit capabilities,
  resource lifetime, error, and synchronization contracts.
- The first delivery has more foundation work than a direct OpenGL app, but UI
  screens will not be rewritten to adopt Vulkan or a future backend.
- Plugin ABI compatibility becomes a maintained public contract and requires
  contract tests, semantic versioning, immutable API tables, runtime quiescence
  before unload, and explicit thread/lifetime rules.
- Platform/native surface integration is isolated in WSI glue modules.
- A renderer-neutral `null` backend makes host and UI behavior testable without
  a GPU.
