# Synchronous native queue lifetime

RENDER-QUEUE-001 follows the original-game native containerOOM. The previous adapter created/destroyed a graphics queue per callback. Pinned Eden retains channel shared ownership in GPU/scheduler maps; its channel release is unimplemented, and channel creation initializes Vulkan cache state. Independent review inspected these actual source paths in the pinned image. This is an emulator resource limitation; successful adapter cleanup did not free that retained channel state.

The adapter now owns one graphics queue from device initialization to shutdown. Each callback still imports current guest bytes, submits, flushes, waits, checks queue error and reads back before freeing its temporary resources. Shader invalidation precedes binding freshly loaded shader code on the reused queue. Initialization failure cleans up the device; shutdown waits and destroys the queue before the device. Guest execution has one synchronous queue owner; concurrent callbacks remain outside acceptance.

A controlled128x96 real-executor fixture ran the same31GPUcases, clear and5CPUfallbacks, plus640repeated textured draws. The previous module reached6GiB and DockerOOM (runnerexit2); the candidate passed671GPUdraws, oneclear and5fallbacks with the same pixel/padding checks. Thirty memory samples peaked at4.105GiB and ended4.097GiB. Raw logs, sample data and Docker events are private in `game/build/queue-proof/`. Independent review verified exact source/runtime/shader/fixture identity and actual results. This is a queue-count stress proof, not a640x480 rendering or physical-hardware measurement.

The full Switch target builds with the candidate: NRO `7b4f73fb32cb59505b02dd9b2bc6a38c45588a309418f420399303b5c0a0df63`. Exact frozen evidence: `game/build/evidence/queue-lifetime-4eb85ee5-20261029/manifest.json`. Original-game recapture is running; no sustained FPS, gameplay, concurrent-write or hardware acceptance follows yet.

Rollback: disable MANHUNT_GPU_BACKEND, or restore the previous module and rebuild (retaining the documented runnerOOM limitation).
