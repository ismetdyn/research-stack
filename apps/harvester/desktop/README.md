# Research Harvester native desktop client

This is a C++20 native Linux client: it is neither a web UI nor a Qt
application. SDL3 supplies the window/input layer; Dear ImGui produces screen
draw data; a project-owned, versioned RHI loads OpenGL or Vulkan as an
allowlisted shared module. The screen and application-service code do not
include OpenGL or Vulkan headers.

## Build and run

From the repository root:

```sh
cmake -S desktop -B build/desktop \
  -DCMAKE_BUILD_TYPE=Debug \
  -DHARVESTER_DEPENDENCY_PROVIDER=BUNDLED
cmake --build build/desktop --parallel
./build/desktop/harvester_desktop --renderer=opengl
```

The first configure fetches pinned SDL 3.4.10 and Dear ImGui 1.92.9. Later
configures remain disconnected from upstream. The app loads only the selected
allowlisted module (`librhi_opengl.so` or `librhi_vulkan.so`) from its own
build directory. Running a search also requires the project's `uv` environment
because the app starts its existing Python worker.

For a finite render smoke test:

```sh
./build/desktop/harvester_desktop --renderer=opengl --frames 3
./build/desktop/harvester_desktop --renderer=vulkan --frames 3
```

Mesa software rendering is also supported when a GLX-capable display is
available:

```sh
LIBGL_ALWAYS_SOFTWARE=1 ./build/desktop/harvester_desktop --frames 3
```

For a software Vulkan run under an X11 virtual display:

```sh
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json \
  xvfb-run -a ./build/desktop/harvester_desktop --renderer=vulkan --frames 3
```

Renderer selection is startup-only. The Vulkan path owns device selection,
FIFO swapchain recreation, presentation and synchronization internally; SDL3
supplies only its opaque WSI callbacks. Neither UI code nor the application
needs a Vulkan object.

## Current workflow

The native screen implements source selection, query/limit validation,
non-blocking search state, result list, selected-work detail, explicit
open-access candidate discovery, and selected-PDF download. Searches run
through `PythonResearchService`: an on-demand Python worker queries selected
OpenAlex, Crossref, and arXiv sources without importing the CLI or writing the
local database. Search results retain their DOI and source-declared OA URL.

For a selected work, **Find full text** performs a read-only discovery from
the declared OA location, OpenAlex, OpenAIRE Graph, Europe PMC, and Zenodo.
It lists candidate provenance, location, version, and licence but never
downloads a file automatically. The user must select a candidate and click
**Download selected PDF**. The service keeps the remote URL private behind an
opaque candidate ID; the UI never submits a URL or output path. Public provider
responses are normalized and capped at 200 candidates before they enter the
worker protocol.

The worker is launched with fixed arguments plus individual validated `argv`
values—never a shell—and its output is bounded and schema-validated before it
reaches the UI. Downloads disable environment proxies, manually validate each
HTTPS redirect and pin the TCP connection to its validated public address,
limit data to 64 MiB, require a PDF header, calculate SHA-256, and atomically
store the result under
`$XDG_DATA_HOME/research-harvester/papers` (or
`~/.local/share/research-harvester/papers`). No database document record is
created. UI and graphics modules do not invoke Python directly.

`Queue enrichment` remains intentionally unavailable; PDF import and the
persistent library remain separate operations.

## Verification

```sh
ctest --test-dir build/desktop --output-on-failure
uv run python -m unittest discover -s tests -v
uv run python -m research_harvester.desktop_bridge \
  --query 'OpenGL rendering' --limit 1 --source openalex
valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=99 \
  ./build/desktop/imgui_rhi_renderer_null_test
./desktop/scripts/verify.sh
```

`null` is the deterministic RHI contract backend. OpenGL and Vulkan run the
same ImGui-to-RHI adapter and application screen. The windowed Vulkan tests are
separate from CTest because they require an X11 display; `verify.sh` runs them
through Xvfb and uses Lavapipe when installed. Debug builds automatically use
the Khronos validation layer when it is installed; validation errors fail the
Vulkan UI smoke through the backend diagnostic. At startup the native client
writes a bounded, structured renderer record (backend, version, texture limit,
dynamic-texture capability, and RHI result) to standard error; it never logs
worker requests, URLs, paths, credentials, or database settings.

The architecture and compatibility policy are in
[`ADR-001`](../docs/decisions/ADR-001-versioned-rendering-hardware-interface.md)
and the [accepted design](../docs/superpowers/specs/2026-09-13-desktop-rhi-design.md).
The ABI compatibility, backend matrix, and migration policy are in
[RHI v1 compatibility](../docs/rhi-abi-compatibility.md); release notes begin
in [CHANGELOG](CHANGELOG.md).
