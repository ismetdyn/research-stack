# Native Desktop RHI Task List

## Task 1: Create isolated CMake target topology — Complete

**Description:** Add the `desktop/` CMake project, strict compiler settings,
dependency acquisition policy, and target-level include/link boundaries without
yet introducing a graphics API.

**Acceptance criteria:**

- [x] CMake configures with C++20 and produces `rhi_api`, `rhi_cpp`,
  `rhi_loader`, `rhi_null`, and a smoke-host target.
- [x] Public RHI target exports no third-party headers.
- [x] Build artifacts and dependency caches are ignored by Git.

**Verification:**

- [x] Configure: `cmake -S desktop -B build/desktop`.
- [x] Build: `cmake --build build/desktop --parallel`.
- [x] Inspect target compile commands to confirm no GL/Vulkan/SDL headers occur
  in `rhi_api`.

**Dependencies:** None.

**Files likely touched:** `desktop/CMakeLists.txt`, `desktop/cmake/*`,
`.gitignore`.

**Estimated scope:** M.

## Task 2: Define the RHI C ABI and host-local C++ wrapper — Complete

**Description:** Define fixed-width types, result/error semantics, versioned API
tables, opaque handle rules, descriptors, capability structures, and a C++ RAII
wrapper that cannot cross plugin boundaries.

**Acceptance criteria:**

- [x] Public headers compile as C17 and C++20.
- [x] ABI validation rejects wrong ABI version, too-small tables, and missing
  required function pointers.
- [x] No public header includes C++ STL, SDL, OpenGL, or Vulkan.

**Verification:**

- [x] Focused ABI tests pass.
- [x] Compile a C translation unit that includes every public RHI header.
- [x] Compile a C++20 translation unit that uses only the wrapper.

**Dependencies:** Task 1.

**Files likely touched:** `desktop/include/harvester/rhi/*`,
`desktop/src/rhi_api/*`, `desktop/src/rhi_cpp/*`, `desktop/tests/*`.

**Estimated scope:** M.

## Task 3: Implement plugin loader and null contract backend — Complete

**Description:** Implement allowlisted shared-library loading, plugin handshake,
diagnostic reporting, typed handle/generation validation, and a deterministic
null backend that records `UI_BASELINE` command behavior.

**Acceptance criteria:**

- [x] Loader accepts the known null plugin and rejects malformed test plugins.
- [x] Null backend detects stale runtime handles and unsupported capability use
  without crashing. Command-order validation begins with the command API.
- [x] A host smoke executable loads the null plugin, creates/destroys a UI
  runtime, and exits cleanly.

**Verification:**

- [x] Loader and null-backend unit/contract tests pass.
- [x] Smoke executable succeeds with the null backend.
- [x] Valgrind reports zero memory errors/leaks. ASan/UBSan is unavailable in
  this environment because its runtime libraries are missing at link time.

**Dependencies:** Task 2.

**Files likely touched:** `desktop/src/rhi_loader/*`, `desktop/src/rhi_null/*`,
`desktop/src/smoke_host/*`, `desktop/tests/*`.

**Estimated scope:** M.

## Checkpoint: Contract foundation

- [x] Tasks 1–3 build and test cleanly from an empty build directory.
- [x] Plugin ABI contract is documented in public header comments.
- [x] No source outside `rhi_*` includes renderer implementation headers.

## Task 4: Define the UI-baseline device and command RHI contract — Complete

**Description:** Add only the buffer, texture, fixed UI pipeline, host-data
write, indexed draw, and fence operations needed by `UI_BASELINE`; implement
their validation/state machine in the null backend before an API-specific
renderer exists. Surface/present ownership remains in the later WSI increment.

**Acceptance criteria:**

- [x] UI command/resource descriptors define ownership, host-data lifetime,
  output-on-error, and cross-runtime-handle rules without native API types.
- [x] Null backend accepts a valid UI baseline trace and rejects stale handles,
  invalid lifecycle transitions, unsupported features, and invalid command
  ordering without modifying state.
- [x] The UI table extension is capability-gated; base v1 plugins without the
  optional extension remain valid and return `RHI_RESULT_UNSUPPORTED`.

**Verification:**

- [x] Focused null conformance tests pass with a valid draw trace and explicit
  invalid-order cases.
- [x] Public headers compile as C17 and C++20 without GL/Vulkan/SDL includes.
- [x] Valgrind run is clean (`rhi_ui_null_test`: zero errors and zero leaks).

**Dependencies:** Checkpoint: Contract foundation.

**Files likely touched:** `desktop/include/harvester/rhi/*`,
`desktop/src/rhi_api/*`, `desktop/src/rhi_null/*`, `desktop/tests/*`.

**Estimated scope:** M.

## Task 5: Implement OpenGL backend and SDL3 OpenGL WSI glue — Complete

**Description:** Add an OpenGL 3.3-core (or higher reported) backend plus a
separate SDL3 WSI adapter for window/context/presentation, with explicit
resource cleanup and diagnostics.

**Acceptance criteria:**

- [x] OpenGL backend advertises truthful `UI_BASELINE` capabilities.
- [x] WSI drawable-size updates, present, optional vsync, and shutdown work
  without native
  objects leaving the RHI/WSI modules.
- [x] The shared ImGui-to-RHI command trace runs through null contract tests and
  the OpenGL desktop smoke path.

**Verification:**

- [x] Run the OpenGL desktop smoke with Mesa llvmpipe.
- [x] Run smoke application through create/render/resize/shutdown on the
  development display.
- [x] Enable GL debug output in debug builds and observe no high-severity error
  during the llvmpipe smoke.

**Dependencies:** Task 4.

**Files likely touched:** `desktop/src/rhi_opengl/*`,
`desktop/src/rhi_wsi_sdl3_opengl/*`, `desktop/tests/*`.

**Estimated scope:** M.

## Task 6: Add SDL3 platform runtime and ImGui-to-RHI adapter — Complete

**Description:** Add the platform event loop and the custom renderer adapter
that uploads ImGui draw data, processes dynamic textures, applies clipping and
blend state, and emits only RHI commands.

**Acceptance criteria:**

- [x] UI screens compile without native graphics headers or renderer plugin
  dependencies.
- [x] Font atlas and dynamic ImGui texture lifecycle work through logical
  `UiTexture` mapping.
- [x] A resized interactive window draws a known ImGui frame with correct
  clipping.

**Verification:**

- [x] Null-backed adapter tests exercise the modern ImGui texture lifecycle and
  RHI-validated clipped draw trace.
- [ ] Manual smoke test checks keyboard, mouse, clipboard, resize, and vsync.
- [x] Target boundaries keep `desktop_dashboard` and `ui_renderer_rhi` free of
  GL/Vulkan dependencies.

**Dependencies:** Task 5.

**Files likely touched:** `desktop/src/platform_sdl3/*`,
`desktop/src/ui_core/*`, `desktop/src/ui_renderer_rhi/*`, `desktop/tests/*`.

**Estimated scope:** M.

## Task 7: Build the first Research Harvester desktop workflow — Complete

**Description:** Define a typed application-service seam and implement focused
screens for search, works list, work detail, actions, PDF import, and renderer
diagnostics. A deterministic fake covers service tests; a separate Python
worker now serves live read-only search through tested input/output contracts.

**Acceptance criteria:**

- [x] User can complete search → select work → inspect actions in the desktop
  application with the real read-only Python search adapter; the fake remains
  deterministic test coverage.
- [x] Long work exposes pending/error state without blocking frame rendering.
- [x] Input validation prevents unbounded search text and invalid source/limit
  requests before calls reach the service adapter. The Python worker uses a
  fixed `execvp` command, bounded protocol output, timeout, and child cleanup.
  PDF-path validation remains with the future import operation.
- [x] Renderer diagnostics display the active backend, version, dynamic-texture
  capability, and reported maximum texture dimension without RHI types in UI.

**Verification:**

- [x] Service-contract tests pass.
- [x] Protocol and process-adapter tests cover valid, malformed, oversized, and
  invalid-input worker cases; a live OpenAlex worker smoke succeeds.
- [x] Existing Python unittest suite remains green after bridge integration.

**Dependencies:** Task 6.

**Files likely touched:** `desktop/src/desktop_app/*`,
`desktop/src/harvester_bridge/*`, `desktop/tests/*`, `tests/*`.

**Estimated scope:** M.

### Task 7a: Add safe, read-only open-access candidate discovery — Complete

**Description:** Preserve DOI/open-access metadata in the desktop search result
contract and add a database-free Python module that normalizes candidates from
declared open-access metadata and supported open-access APIs. Discovery must
have no filesystem side effects.

**Acceptance criteria:**

- [x] Candidate data includes provenance, repository/publisher label, licence
  and version when supplied, and a direct-PDF indication.
- [x] Discovery uses no database configuration, CLI module, browser, or
  credentials; unauthenticated open-access sources continue independently when
  one source fails.
- [x] Candidate URLs are canonical HTTPS URLs with credentials, IP literals,
  and local/private hosts rejected before a later download step.

**Verification:**

- [x] Python unit tests cover candidate normalization, malformed source data,
  duplicate elimination, the OpenAIRE/Europe PMC/Zenodo response variants,
  candidate bounds, and isolated source errors without real network use.
- [x] Existing search bridge tests remain green.

**Dependencies:** Task 7 search contract.

**Estimated scope:** S.

### Task 7b: Version the worker and full-text application-service contract — Complete

**Description:** Add asynchronous candidate discovery and selected-download
operations to the Python worker and native application service. Retain parsing
support for existing search output while introducing explicit operation and
result records.

**Acceptance criteria:**

- [x] The native service exposes typed pending/ready/error snapshots for
  candidates and downloads, independent of the graphics/RHI layer.
- [x] Candidate IDs are opaque; a UI cannot supply an arbitrary URL to the
  worker's download operation.
- [x] Protocol parsing is bounded and rejects malformed or mixed operation
  results without crashing the application.

**Verification:**

- [x] C++ protocol/process tests cover v1 search compatibility, v2 discovery,
  unknown candidate IDs, timeouts, and malformed records.
- [x] Native build and CTest pass.

**Dependencies:** Task 7a.

**Estimated scope:** M.

### Task 7c: Add explicit full-text controls to the native work-detail UI — Complete

**Description:** Render candidate provenance, licences, version and download
state in the work-detail view. Only the candidate selected by the user can
start a download; the result is presented as a local application-owned file.

**Acceptance criteria:**

- [x] UI never auto-downloads during search, selection, or candidate discovery.
- [x] Downloads follow a validated redirect chain, cap body size at 64 MiB,
  require a PDF header, calculate SHA-256, and atomically store under the XDG
  application data directory with a safe generated filename.
- [x] Existing database and CLI import flows are unchanged.

**Verification:**

- [x] Tests cover redirect/private-address rejection, oversize/non-PDF failure,
  atomic-save cleanup, checksum result, and no partial output on failure.
- [x] OpenGL native smoke and both test suites pass.

**Dependencies:** Task 7b.

**Estimated scope:** M.

## Checkpoint: Usable OpenGL desktop application

- [x] Tasks 4–6 pass in a clean build and with the Python suite.
- [x] UI works via OpenGL without UI/application references to GL/Vulkan.
- [x] Renderer diagnostics identify selected backend and reported capabilities.

## Task 8: Implement Vulkan backend and SDL3 Vulkan WSI glue — Complete

**Description:** Add Vulkan device selection, queues, swapchain, resource
state/synchronization, pipelines, command translation, and WSI glue against the
existing RHI contract.

**Acceptance criteria:**

- [x] Vulkan passes the same `UI_BASELINE` command suite as OpenGL.
- [x] Debug validation reports no errors during smoke rendering and shutdown.
- [x] Backend selection runs the same desktop application with
  `--renderer=vulkan`.

**Verification:**

- [x] Vulkan validation-layer test run is clean.
- [x] Lavapipe contract and UI smoke runs pass where available.
- [x] Resize/present/out-of-date swapchain recovery is covered by tests.

**Dependencies:** Checkpoint: Usable OpenGL desktop application.

**Files likely touched:** `desktop/src/rhi_vulkan/*`,
`desktop/src/rhi_wsi_sdl3_vulkan/*`, `desktop/tests/*`.

**Estimated scope:** M.

## Task 9: Add backend parity, observability, and reproducible CI

**Description:** Automate ABI/contract/UI checks, sanitizers, software-renderer
coverage, diagnostic capture, and target-boundary enforcement.

**Acceptance criteria:**

- [ ] CI validates null, OpenGL, and Vulkan paths where software drivers are
  available.
- [x] Runtime logs structured backend/capability/validation diagnostics locally.
- [x] Build dependency versions and shader compilation are reproducible.

**Verification:**

- [x] CI-equivalent local script passes from a fresh build directory.
- [x] Intentional ABI/capability failure tests produce actionable diagnostics.
  Loader failures (ABI, struct size, entry point, plugin info, allowlist) assert
  message content in `rhi_loader_test`; `harvester_desktop` prints them.
- [ ] Unsupported profile/features from `create_runtime` still return a bare
  `RHI_RESULT_UNSUPPORTED`; a message needs a plugin contract change.
- [x] No diagnostic includes secrets or database connection settings.

**Dependencies:** Task 8.

**Files likely touched:** `desktop/tests/*`, `desktop/cmake/*`,
`.github/workflows/*`, `docs/*`.

**Estimated scope:** M.

## Task 10: Publish RHI developer documentation and compatibility contract

**Description:** Document public ABI versioning, feature profiles, backend
matrix, extension policy, module boundaries, and migration rules for future
engine consumers.

**Acceptance criteria:**

- [x] RHI headers document ownership, thread, error, and lifetime rules.
- [x] README includes build/run, renderer selection, diagnostics, and supported
  driver matrix.
- [x] Public ABI version and changelog policy are explicit.

**Verification:**

- [x] A clean developer can build the null smoke target from README commands.
- [x] Documentation links to ADR-001 and the accepted design specification.
- [x] Documentation review has no stale implementation claims.

**Dependencies:** Task 9.

**Files likely touched:** `desktop/README.md`, `desktop/include/*`, `docs/*`,
`CHANGELOG.md`.

**Estimated scope:** M.

## Checkpoint: Engine-ready RHI v1

- [ ] All task acceptance criteria and verification commands pass.
- [x] OpenGL and Vulkan run the unchanged UI through the plugin loader.
- [x] Public contract has semantic-versioning, tests, diagnostics, and docs.
