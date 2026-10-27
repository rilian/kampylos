---
id: KAM-010
title: MinGW-w64 build crashed with heap corruption when completion-pool threads exited
type: bug
status: fixed
severity: high
platforms: [windows, mingw]
area: build
pattern: PAT-020
aliases: []
reported: 2026-10-08
source: developer reproduction with a MinGW build of the OpenCL path
where: src/kampylos_cpu_patch_pool.h:52-62 @ 45a41d5
fixed_in: 5a5ffff
---

## Symptom

The process was killed with exit code 0xC0000374 (`STATUS_HEAP_CORRUPTION`) within the first cell. MSVC and
Linux/glibc builds of the same code did not show it.

## Cause

The completion pool spawned and joined fresh `std::thread`s for every batch. Each thread exit ran the destructors of
VBMicrolensing's `thread_local` scratch objects (the fork makes the library's function-local statics
`thread_local`), and under MinGW-w64 (winpthreads, emulated TLS) one of them, an `_augmented_priority_queue`, freed
memory it did not own (`src/kampylos_cpu_patch_pool.h:52-60`).

## Fix

5a5ffff (2026-10-08; `git log -S "PERSISTENT WORKERS"`): the workers are created once (`:110-113`), loop forever
(`worker_loop`, `:175-191`) and never exit, so no `thread_local` destructor runs; every backend creates the pool with
`new` and never deletes it (`src/gpu_fit_binary.cu:69`, `src/kampylos_opencl_fit.cpp:199`,
`src/kampylos_metal_fit.mm:755`), and the destructor detaches rather than joins (`:117`). Patching the vendored
library was rejected. 66a1ab3 (thread count from the client's CPU budget) kept the design. Verified by reading at
45a41d5. New code must not add threads that exit after touching VBMicrolensing.

## Test

Run the threaded completion step of a MinGW build for a whole work unit on Windows or under Wine. No automated test;
`tests/test_vbm_threads.cpp` joins its threads at the end, so it is not a regression test for this.

## Log

- 2026-10-08: fixed in 5a5ffff.
- 2026-10-27: recorded, verified at 45a41d5 by reading.
