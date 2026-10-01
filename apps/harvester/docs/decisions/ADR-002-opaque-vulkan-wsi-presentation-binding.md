# ADR-002: Pass Vulkan presentation through an opaque, versioned WSI binding

## Status

Accepted

## Date

2026-09-14

## Context

The RHI v1 UI table deliberately has no public surface or swapchain objects.
That keeps UI and engine-facing code independent from SDL and native graphics
types, but Vulkan cannot present without a platform-created surface. The
existing OpenGL backend relies on a current SDL GL context; that implicit model
does not exist for Vulkan.

The Vulkan backend needs four platform-specific actions during runtime setup:
the loader's `vkGetInstanceProcAddr` function, the instance extension names
SDL requires, creation of an SDL-owned surface from a Vulkan instance, and
destruction of that surface before the SDL window is destroyed. The
application, dashboard, UI adapter, loader, and base RHI API must not include
SDL or Vulkan headers.

## Decision

Add a trailing, optional presentation-binding kind and opaque pointer to
`RhiRuntimeCreateInfo`. The binding pointer names a fixed-size,
C-compatible `RhiVulkanPresentationBindingV1` callback table. It has a
host-owned context plus callbacks to query the loader proc address and required
instance extensions, create a Vulkan surface, and destroy that surface. Vulkan
handles cross this
private WSI seam only as fixed-width 64-bit values; no `Vk*` or `SDL_*` type enters the base
RHI, UI, dashboard, or application-service headers.

The binding is valid from `create_runtime` until `destroy_runtime` returns.
The Vulkan plugin owns the Vulkan instance, device, queue, swapchain,
synchronization primitives, and presentation. The SDL WSI adapter owns only
the window and the callbacks that bridge SDL surface creation. A Vulkan runtime
requires the binding; OpenGL and null ignore a missing binding. The appended
creation fields remain opt-in through `struct_size`, so a pre-binding v1 host
can still create an OpenGL or null runtime with a newer plugin.

`clear_target` starts a Vulkan frame by acquiring a swapchain image, while the
existing UI submit ends it and presents. The Vulkan backend recreates its
swapchain on a changed target extent or an out-of-date/suboptimal acquire or
present result. This does not enlarge the UI draw/resource contract.

## Alternatives considered

### Pass an SDL window pointer directly through the base RHI

- Pros: few new functions.
- Cons: makes SDL an observable engine-RHI dependency and forces every future
  host to imitate an SDL object.
- Rejected: violates the platform/engine boundary from ADR-001.

### Add `VkSurfaceKHR` to the public RHI headers

- Pros: direct Vulkan implementation.
- Cons: exposes Vulkan types to all RHI consumers and makes other presentation
  backends part of the base contract.
- Rejected: violates the renderer-neutral RHI constraint.

### Let the Vulkan plugin link against SDL and find the active window globally

- Pros: no ABI extension.
- Cons: couples a dynamically loaded renderer to one windowing library and
  hides ownership/lifetime rules in global state.
- Rejected: unsafe for multiple hosts and not reusable by the future engine.

### Introduce full public surface and swapchain APIs now

- Pros: resembles a complete engine RHI.
- Cons: expands the public contract before two backend implementations prove
  the required semantics.
- Rejected: premature; the narrow WSI binding is enough for the UI baseline.

## Consequences

- The Vulkan plugin has explicit, testable WSI ownership without leaking native
  graphics types to UI/application modules.
- `RhiRuntimeCreateInfo` gains an additive trailing extension and every
  backend must respect caller-provided `struct_size` before reading it.
- The SDL platform gains a Vulkan-window mode and must destroy its surface
  before the window.
- Future Win32, Cocoa, Android, or engine-owned hosts implement the same small
  callback table rather than changing RHI clients.
