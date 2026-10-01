# Desktop RHI and Native UI Design

## Status

Accepted on 2026-09-13. This document is the implementation contract.

The first usable vertical slice is implemented: an allowlisted OpenGL 3.3
plugin, SDL3-owned OpenGL presentation context, modern ImGui texture adapter,
and a native search/list/detail workflow. Searches use a bounded, on-demand
Python worker; a deterministic service remains for tests. Vulkan and the
remaining Python workflows (library, PDF, enrichment, and graph) are later
increments.

## Context

Research Harvester currently has a Python command-line core. It needs a native
C++ desktop client that is quick to use today and whose rendering foundation can
be reused by a future game-engine core. Qt and a web UI are explicitly out of
scope.

The client must not know whether OpenGL, Vulkan, or a test renderer is active.
Neither the application screens nor the UI toolkit may include, name, or pass
OpenGL/Vulkan objects. Renderer selection must be replaceable without changing
the client code.

This project uses the industry term **Rendering Hardware Interface (RHI)** for
this boundary. There is no universal C++ RHI standard; the design takes the
portable-backend and feature-level patterns used by projects such as bgfx and
Filament as reference, while retaining ownership of the contract.

## Goals

- A native Linux desktop application, structured so other platforms can be
  added without changing application or UI source.
- A C++20 implementation with a versioned, C-compatible renderer-plugin ABI.
- A composition-root renderer selection policy that can add `auto`, Vulkan,
  OpenGL, and test-only null without changing application/UI modules.
- An RHI broad enough for a future engine: device, queues, resources, pipelines,
  command encoding, synchronization, and presentation.
- A narrow, deterministic capability baseline for the current 2D UI.
- A production-quality path to OpenGL and Vulkan implementations, including
  validation, conformance tests, and software-driver CI coverage.
- Existing Python harvesting code remains the domain core. The desktop client
  integrates it through a separate application-service bridge, never through
  graphics modules.

## Non-goals

- Writing a complete 3D engine, ECS, physics engine, editor, asset pipeline, or
  render graph in the initial release.
- Pretending all graphics APIs have identical semantics. API-specific features
  are exposed through explicit capability negotiation rather than hidden
  fallback behavior.
- Live backend replacement during a frame. A backend change recreates the
  renderer and GPU resources at a controlled restart point.
- Letting UI, application, or Python-integration modules consume native GPU
  handles.
- Qt, a browser/WebView, or an HTTP UI.

## Architecture

```text
                         +--------------------+
                         | harvester-desktop  |
                         | composition root   |
                         +----------+---------+
                                    |
        +---------------------------+---------------------------+
        |                           |                           |
+-------v--------+        +---------v----------+      +---------v---------+
| platform-sdl3  |        | desktop-app         |      | rhi-loader        |
| window/input   |        | screens/view models |      | plugin discovery  |
+-------+--------+        +---------+----------+      +---------+---------+
        |                           |                           |
        |                   +-------v--------+                  |
        |                   | ui-core         |                  |
        |                   | Dear ImGui core |                  |
        |                   +-------+--------+                  |
        |                           |                           |
        +---------------------------v---------------------------+
                                    |
                         +----------v----------+
                         | ui-renderer-rhi      |
                         | draw data -> RHI     |
                         +----------+----------+
                                    |
                         +----------v----------+
                         | rhi-api              |
                         | versioned C ABI      |
                         +-----+-----------+----+
                               |           |
                    +----------v--+    +---v-----------+
                    | rhi-opengl  |    | rhi-vulkan    |
                    | plugin      |    | plugin        |
                    +-------------+    +---------------+
                               \           /
                                +---------+
                                | rhi-null |
                                | test only|
                                +----------+
```

`platform-sdl3` is a windowing/input and initial OpenGL-presentation adapter,
not the graphics abstraction. It owns the SDL window and GL context behind
opaque implementation pointers. It is kept outside the screen and service
layers; no SDL or native graphics type appears in their headers. The OpenGL RHI
plugin itself remains SDL-free and sees only the already-current context.

The use of SDL3 is limited to its mature window, input, clipboard, timing, and
native-surface facilities. SDL3's own GPU API is not used as this RHI: its
documented backends select Vulkan, D3D12, or Metal, but not OpenGL.

## Module boundaries

| Module | May depend on | Must not depend on |
|---|---|---|
| `harvester-desktop` | composition root: platform, loader, adapter, screens | native API headers |
| `desktop-dashboard` | Dear ImGui core, application-service contracts | SDL, RHI, GL, Vulkan |
| `ui-renderer-rhi` | ImGui draw data, `rhi-api` | application screens, GL, Vulkan, SDL |
| `platform-sdl3` | SDL3 plus its OpenGL context/present API | RHI public API, application screens |
| `fake-service` / `python-service` | application-service contracts, worker protocol | ImGui, SDL, RHI, GL, Vulkan |
| `rhi-api` | C standard fixed-width types only | C++ STL, SDL, GL, Vulkan |
| `rhi-cpp` | `rhi-api` | backend headers |
| `rhi-loader` | dynamic-loader abstraction, `rhi-api` | SDL, GL, Vulkan |
| `rhi-opengl` | `rhi-api`, OpenGL/GLX procedure loader | application/UI source, SDL |
| `rhi-vulkan` | `rhi-api`, Vulkan headers, Vulkan WSI glue | application/UI source |
| `rhi-null` | `rhi-api` | SDL, GL, Vulkan |

The implemented Python adapter exposes typed `search` only. It invokes the
existing Python source clients in a separate process and cannot receive an RHI
device or an ImGui object. `listWorks`, `enrich`, `importPdf`, and
`resolveRelated` remain future typed operations rather than ad-hoc screen
commands.

## RHI public contract

### ABI rules

- `rhi-api` headers are C17-compatible and use `<stdint.h>`, `<stddef.h>`, and
  POD structures only. C++20 is an implementation detail.
- Every extensible descriptor and API table starts with `struct_size`; every
  API table declares an ABI version. Fixed value structures such as a rectangle
  and a UI draw use their exact declared layout. Unknown trailing fields are
  ignored, and a too-small known structure is rejected.
- Plugins export exactly one stable C symbol:

  ```c
  const RhiPluginApiV1* harvester_rhi_get_plugin_api(
      uint32_t requested_abi_version
  );
  ```

- The symbol name never changes. A compatible, trailing optional extension of
  v1 keeps `abi_version == 1`; a new mandatory semantic contract is requested
  through the same entry point with a new ABI version while v1 remains
  available. `struct_size` is a caller-provided capacity for output structures;
  plugins neither read unknown input fields nor write beyond that capacity.
- The current optional extension is `get_ui_api`. A host first validates the
  base table, then asks its runtime for a copied `RhiUiApiV1` table. A base v1
  plugin without that trailing pointer remains a valid base plugin and returns
  `RHI_RESULT_UNSUPPORTED` for this UI request.
- No `std::string`, `std::vector`, exceptions, RTTI objects, allocator-owned
  memory, or backend-native handle crosses the plugin boundary.
- Failures use `RhiResult` plus a copied diagnostic string from a
  fixed-size host-owned output structure. Backends must not throw across the
  ABI; each C++ plugin entry point must catch exceptions before reaching C.
- The loader verifies ABI version, table size, required entry points, backend
  identity, and capability report before using a plugin. That verification
  applies to a cooperative plugin table only: loading native code is a trust
  boundary, not a sandbox.
- A plugin table is immutable static storage for the lifetime of the loaded
  module. The module cannot unload until all runtimes are destroyed, calls have
  quiesced, and no backend callback or worker can re-enter the host.
- Initial binary support is one CPU/OS ABI per build artifact. The C contract
  eliminates C++ ABI coupling; it does not make an x86-64 Linux plugin binary
  compatible with a Windows or different-architecture host.

### Opaque handles and lifetime

The API represents runtimes, adapters, devices, queues, surfaces, swapchains,
buffers, textures, samplers, shader modules, pipelines, bind groups, command
encoders, command buffers, and fences with distinct opaque 64-bit handle
structures. C callers therefore cannot accidentally pass a buffer where a
texture is required. Zero is invalid; cross-runtime/device handles must fail
without state changes. Handle bit layout and generation are backend-private,
which prevents clients from persisting or interpreting handles.

The implemented `RhiUiApiV1` exposes only the UI-baseline resource path:
RGBA8 buffer/texture creation, alpha-blended pipeline creation, host-data
writes, indexed/scissored draws, and fences. Write input memory is borrowed for
the duration of the call only; a successful backend must not read it after the
call returns. Every creation output is reset to the invalid handle before
validation. A submit validates the entire draw array before creating its fence,
so a rejected trace has no partial submission. The null backend completes
submits immediately; a hardware backend must defer destruction until any prior
draw that references a resource is complete.

Surfaces, swapchains, presentation, general command encoders, and engine
resource types are deliberately not part of this first UI table. They stay in
the WSI and later engine increments rather than receiving prematurely broad
semantics. UI code sees only logical `UiTexture` IDs resolved by
`ui-renderer-rhi`, never a `GLuint` or `VkImage`.

### Core capability model

The host requests a named profile and validates the returned capabilities before
creating UI resources:

```text
UI_BASELINE
  - indexed triangles, dynamic vertex/index buffers, RGBA8 textures
  - alpha blending, scissor rectangles, sRGB swapchain
  - sampled texture arrays or equivalent per-draw texture binding
  - one graphics queue, fences, present, resize handling, vsync control

ENGINE_BASELINE (future)
  - UI_BASELINE plus depth/stencil, render targets, compute dispatch,
    timestamp query support when reported, and explicit resource barriers
```

Capabilities are a bit set plus numeric limits (`max_texture_dimension`,
`max_bindings_per_group`, `uniform_alignment`, sample counts). Callers branch on
capabilities, not on renderer names. Optional capability paths are separately
tested; no backend may claim support and silently emulate undefined behavior.

### RHI operation rollout

The implemented v1 foundation freezes plugin discovery, runtime, capabilities,
diagnostics, result, handle, ABI validation, and the optional UI-baseline table.
`RhiUiApiV1` has deterministic null-backend conformance coverage for resource
creation, host-data upload validation, cross-runtime rejection, stale handles,
prevalidation of draw arrays, and fences. The OpenGL plugin implements the same
baseline, including buffers, RGBA8 textures, fixed alpha pipeline, clipping,
and GL sync fences. Its optional trailing `clear_target` table entry clears the
current presentation target without changing the original UI-table contract.

The planned engine-oriented operation surface is:

1. Enumerate adapters, query capabilities and limits, create/destroy device and
   queue.
2. Create/destroy surfaces and swapchains through WSI glue; acquire, present,
   resize, and select present mode.
3. Create/destroy buffers, textures, views, samplers, shader modules, bind
   group layouts/groups, pipeline layouts, render/compute pipelines, and
   queries.
4. Map or upload CPU data through explicit staging/update commands; no hidden
   synchronous upload in a draw call.
5. Encode command buffers: render pass, viewport/scissor, pipeline and
   bindings, draw/draw-indexed, copy, clear, compute dispatch, debug labels,
   barriers, submit, and fences.
6. Poll or wait for work completion, retrieve structured backend diagnostics,
   and install a debug-message callback.

This remains intentionally resource-and-command based. A future render graph
sits above the RHI and translates its scheduling decisions to these explicit
operations; it is not smuggled into backend plugins.

## Platform and WSI boundary

The platform API owns window creation, events, and the initial OpenGL context
and presentation path. It exposes only opaque pointers internally to the
composition root. The RHI's device/resource layer remains headless. Vulkan will
use a dedicated SDL3 WSI adapter because surface/swapchain creation is explicit
there; the current SDL3 platform implementation uses SDL's GL context API and
does not include OpenGL headers.

This prevents an SDL window pointer, a Vulkan surface, or an OpenGL context from
becoming part of the engine contract. Other hosts can later provide a Win32,
Cocoa, Android, or game-engine-owned WSI adapter without modifying RHI clients.

## UI integration

Dear ImGui core is selected for the first desktop tool because it is designed
around a platform backend and a separate renderer backend. We use the maintained
SDL3 platform backend for input, clipboard, cursor, timing, and IME behavior,
but do not use a stock OpenGL or Vulkan renderer backend. `ui-renderer-rhi`
implements the rendering side once against the copied `RhiUiApiV1` table.

The renderer adapter owns font-atlas upload, texture registration, vertex/index
upload, clipping, blend state, pipeline selection, and per-frame command
submission. Screens produce ImGui commands only. With modern ImGui, dynamic
texture updates are consumed from `ImDrawData` by this renderer adapter; they do
not leak native texture IDs to screens.

The implemented first screen set is deliberately small: query/source selection,
non-blocking service state, results list, selected-work detail, and queued
enrichment/full-text actions. `PythonResearchService` starts the read-only
`research_harvester.desktop_bridge` worker only when a search starts. It does
not import the CLI, database, or database configuration; OpenAlex, Crossref,
and arXiv results are returned through a versioned, base64-encoded stdio
protocol. A partial source outage is carried as a source-name-only notice;
transport or provider diagnostics are not displayed. `FakeResearchService`
remains deterministic for screen/service tests.
PDF import, related graph, persistent works, action execution, and progress
streaming follow behind the same service contract.

## Renderer selection and recovery

The current executable selects OpenGL in its composition root; it accepts only
an optional `--frames <positive-count>` test limit. Startup renderer selection
is deferred until Vulkan exists rather than advertising unsupported modes.

- `null` remains test-only and is never a silent interactive fallback.
- When Vulkan lands, `auto` will attempt Vulkan then OpenGL, and an explicit
  mode will fail closed with a copied backend diagnostic.
- A future renderer setting will finish the frame, retire UI fences, destroy
  owned GPU resources, recreate the runtime, and restore screen state. There is
  no hot unload while handles or queued work exist.

## Threading and synchronization

The initial runtime has one render-owning thread. All v1 calls for a given
runtime are externally synchronized and thread-affine to its creation thread.
Application service work,
database I/O, PDF processing, and external harvest calls run asynchronously and
publish immutable results to the UI thread. GPU object mutation and presentation
are serialized through the render thread.

The API permits command encoders to be created from worker threads in a later
phase, but queue submission remains centralized until backend conformance tests
cover concurrent recording. Vulkan synchronization and resource transitions are
implemented by the Vulkan backend; callers declare usage and ordering through
the RHI command model. The OpenGL backend preserves equivalent ordering through
its command executor and fences.

## Shader strategy

The initial fixed ImGui pipeline uses a small private GLSL 3.30 program embedded
in the OpenGL backend. It is not visible to screens or public RHI headers and
has no user-defined shader inputs. General engine shaders will be source assets,
compiled from a canonical representation with reflection; SPIRV-Cross is the
planned OpenGL generation/reflection tool.

## Build, dependencies, and layout

- CMake minimum version: 3.28; implementation language: C++20.
- Targets are namespaced and privately linked: `harvester::rhi_api`,
  `harvester::rhi_cpp`, `harvester::rhi_loader`, `harvester::platform_sdl3`,
  `harvester::ui_core`, `harvester::ui_renderer_rhi`, and one shared library per
  renderer backend.
- Third-party libraries are version-pinned and commit recorded. The current
  `BUNDLED` provider fetches SDL 3.4.10 at
  `8e37db5e797b6167f3a00d697d816a684bd259c7` and Dear ImGui 1.92.9 at
  `01380c579715e62fb9a8d6ec0502c4ea83bfde6e`, then remains disconnected on
  later configurations. They are not copied into application modules and
  expose no public headers through `desktop-app`.
- Current required libraries: SDL3 (platform), Dear ImGui (core plus SDL3
  platform backend), and the system OpenGL/GLX development package. The OpenGL
  plugin owns a narrow GLX procedure loader. Vulkan headers/loader, shader
  compiler, and SPIRV-Cross are deferred with the Vulkan/general-shader work.
- The native executable also requires the existing project `uv` environment at
  search time. The current process adapter is POSIX/Linux-specific: it uses
  `fork`/`execvp`, pipes, and `waitpid`; platform-specific worker launchers can
  later implement the same application-service contract.
- Proposed tree:

  ```text
  desktop/
    CMakeLists.txt
    cmake/
    include/harvester/rhi/       # C ABI only
    src/rhi_api/
    src/rhi_cpp/
    src/rhi_loader/
    src/rhi_null/
    src/rhi_opengl/
    src/rhi_vulkan/
    src/platform_sdl3/
    src/ui_core/
    src/ui_renderer_rhi/
    src/desktop_app/
    tests/
  ```

## Verification strategy

1. **ABI tests:** load every plugin; reject wrong version/table size/missing
   entry point; exercise error copying and opaque-handle generation checks.
2. **RHI contract tests:** run identical declarative resource/command cases on
   `null`, OpenGL, and Vulkan. `null` is the deterministic oracle for validation
   semantics, not a rendering-equivalence oracle.
3. **Backend tests:** use Vulkan validation layers in debug builds; test Vulkan
   with Lavapipe and OpenGL with llvmpipe in Linux CI where available.
4. **UI adapter tests:** verify ImGui texture lifecycle, clipping, resize,
   missing capability handling, and that no application target includes native
   graphics headers.
5. **Runtime smoke tests:** start each backend, create a window, render a known
   UI frame, resize, switch vsync, and shut down without validation errors.
6. **Performance telemetry:** record frame CPU time, draw count, upload bytes,
   GPU timing when supported, backend name, adapter identity, and validation
   events. These are local diagnostics by default; no telemetry is sent.

## Security and operational boundaries

- Plugins are discovered only from the application-owned install directory or
  an explicit command-line path; arbitrary current-directory loading is
  prohibited.
- The loader canonicalizes paths and validates ownership/permissions where the
  platform supports it. Plugin names are an allowlist, not a raw user-provided
  filename.
- The Linux loader resolves only canonical direct children of the supplied
  trusted directory and opens them with `RTLD_NOW | RTLD_LOCAL`. It validates
  the API table plus reported ABI version, pointer width, threading model, and
  allowlisted backend identity. This detects incompatible cooperative plugins;
  it cannot sandbox a malicious native module, whose load-time code already
  executes. The trusted directory must therefore be application-controlled and
  not writable by an untrusted principal.
- A loaded module remains alive while callers hold its API table or any runtime
  it created. Current host smoke paths destroy their runtime before the module
  leaves scope; later host wrappers will make this lifecycle mechanically
  enforced rather than a caller convention.
- Desktop application inputs continue to be validated by the Python service
  boundary: especially DOI, search terms, and local PDF paths. Graphics plugins
  never process harvested content.
- The current desktop search adapter validates query bytes, selected enum
  sources, and result limit before launch. It opens the worker with a fixed
  executable/argument prefix and individual `argv` values (never a shell),
  connects stdin to `/dev/null`, caps stdout at 1 MiB and stderr at 16 KiB,
  applies a 45-second timeout, and reaps children during shutdown. Worker
  output is a bounded versioned protocol; malformed fields, embedded NULs, and
  raw diagnostics become a generic UI error.
- Debug logging redacts environment variables, credentials, and absolute
  database connection data.

## Risks and mitigations

| Risk | Mitigation |
|---|---|
| Over-abstracting before two backends exist | Keep `UI_BASELINE` small; add operations only with OpenGL and Vulkan contract evidence. |
| Vulkan/GL behavior divergence | Shared RHI contract tests, capability gates, debug validation, and software-driver CI. |
| C++ plugin ABI breakage | C-compatible versioned API table, opaque handles, no STL/exceptions across boundary. |
| WSI contaminates engine core | Keep SDL/native headers in WSI glue only. |
| ImGui renderer drift | Implement modern dynamic texture handling in the adapter and pin the upstream revision. |
| UI work delays harvester value | Deliver a vertical OpenGL slice first; Vulkan is a separate parity phase. |

## Sources

- Dear ImGui separates platform and renderer backends and recommends SDL3 among
  portable platform choices: https://github.com/ocornut/imgui/blob/master/docs/BACKENDS.md
- SDL3 documents its GPU abstraction's backend set (Vulkan, D3D12, Metal), which
  is why it is not the RHI for this OpenGL/Vulkan requirement:
  https://wiki.libsdl.org/SDL3/SDL_CreateGPUDevice
- Vulkan makes application-side synchronization explicit:
  https://docs.vulkan.org/spec/latest/chapters/synchronization.html
- Filament exposes backend selection and feature levels:
  https://github.com/google/filament/blob/main/filament/backend/include/backend/DriverEnums.h
- bgfx is an established API-agnostic, multi-backend renderer reference:
  https://github.com/bkaradzic/bgfx
- SPIRV-Cross converts/reflection-inspects SPIR-V for backend shader targets:
  https://github.com/KhronosGroup/SPIRV-Cross/blob/main/README.md
