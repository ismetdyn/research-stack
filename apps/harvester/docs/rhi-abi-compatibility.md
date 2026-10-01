# Harvester RHI v1 compatibility policy

## Scope

The public RHI surface is the C17-compatible headers under
`desktop/include/harvester/rhi`. It is deliberately small enough to serve the
desktop client now and an engine host later: a versioned plugin handshake,
fixed-width opaque handles, the `UI_BASELINE` resource/draw table, and an
opaque Vulkan WSI binding. C++ wrappers, SDL adapters, application screens,
and backend implementation details are not ABI.

`HARVESTER_RHI_ABI_VERSION` is currently **1**. A host must negotiate this
number at `harvester_rhi_get_plugin_api`; it must reject a different value
before calling any table entry. Plugin and UI tables use `struct_size` as
caller-owned capacity, so a newer producer only writes fields the consumer
allocated.

## Compatibility rules

- ABI v1 fields and their numeric values are immutable.
- New optional capabilities are appended as trailing table fields or new,
  versioned structures. Existing fields are never reordered, repurposed, or
  removed.
- A semantic behavior change to an existing v1 operation requires an ABI v2,
  even if its C layout would happen to stay the same.
- Handles are process-local, backend-owned, and generation-checked. They do
  not cross renderer runtimes, process boundaries, persistence, or plugin
  unload boundaries.
- Every native/WSI object remains private to its adapter and backend. The base
  RHI, UI renderer, dashboard, and application-service headers never expose
  SDL, OpenGL, or Vulkan types.
- Hosts serialize calls for a runtime. Plugins report
  `RHI_THREADING_EXTERNAL_SYNCHRONIZATION`; applications must destroy all
  fences and resources before unloading a plugin.

The rationale for the plugin split is in
[ADR-001](decisions/ADR-001-versioned-rendering-hardware-interface.md); the
opaque Vulkan WSI extension and its ownership order are in
[ADR-002](decisions/ADR-002-opaque-vulkan-wsi-presentation-binding.md).

## v1 backend matrix

| Backend | UI baseline | Presentation | Verification |
| --- | --- | --- | --- |
| `null` | Deterministic resource/lifetime oracle | None | CTest contract and ImGui adapter tests |
| `opengl` | Buffers, textures, fixed UI pipeline, fences | SDL3 OpenGL 3.3+ | Mesa llvmpipe desktop smoke |
| `vulkan` | Buffers, textures, fixed UI pipeline, fences | SDL3 surface, FIFO swapchain, Vulkan 1.0 | Lavapipe WSI/runtime/UI smoke and desktop smoke |

The Vulkan plugin enables `VK_LAYER_KHRONOS_validation` plus
`VK_EXT_debug_utils` automatically in Debug builds when both are installed.
Validation errors are reduced to a bounded RHI diagnostic (the validation
message ID when supplied) and cause the Vulkan UI smoke to fail. The local
development image may omit that package; the Linux CI runner installs it
explicitly.

## Release and migration policy

The desktop client follows normal semantic versioning. RHI ABI versions advance
only for incompatible binary or semantic changes and are recorded in
`desktop/CHANGELOG.md`. A future ABI migration is expand/contract: publish the
new table alongside v1, let hosts negotiate either version, provide backend
parity tests for both, then announce a v1 removal date in a major desktop
release. No renderer is switched at runtime; switching remains an orderly
application restart.
