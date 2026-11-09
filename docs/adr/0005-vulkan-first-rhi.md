# 0005. Vulkan first, Metal native second, D3D12 later

- **Status:** Accepted
- **Date:** 2026-11-09

## Context
The engine needs one modern explicit API that runs on Windows and Linux, and must later reach macOS/iOS.

## Decision
The render hardware interface (RHI) is backend-neutral. Vulkan 1.3 is the first backend. A native Metal backend follows for Apple platforms (not MoltenVK as the long-term answer). The RHI keeps D3D12 possible but it is not scheduled.

## Consequences
- RHI concepts must map cleanly to Vulkan, Metal and D3D12 (explicit barriers handled by the render graph).
- Shaders are authored once and cross-compiled to SPIR-V and MSL.

## Alternatives considered
- **D3D12 first:** Windows-only.
- **WebGPU-native as the only API:** lacks hardware ray tracing and some advanced features today.
