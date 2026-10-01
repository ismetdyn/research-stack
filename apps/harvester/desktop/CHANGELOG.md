# Changelog

## 0.1.0 — 2026-09-14

- Introduced the stable `HARVESTER_RHI_ABI_VERSION` 1 plugin contract.
- Added deterministic null, OpenGL, and Vulkan UI-baseline backends.
- Added startup-only `--renderer=opengl|vulkan` selection for the native
  desktop client.
- Added an opaque, versioned Vulkan WSI binding so engine/UI consumers remain
  independent from SDL and Vulkan headers.
- Fixed Vulkan present-semaphore reuse by associating each signal semaphore
  with a swapchain image rather than an in-flight frame slot.

Future incompatible RHI changes receive a new ABI version and a migration
entry; v1 field layouts and operation semantics remain stable.
