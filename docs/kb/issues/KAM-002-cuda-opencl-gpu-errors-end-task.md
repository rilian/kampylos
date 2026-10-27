---
id: KAM-002
title: CUDA and OpenCL end the task as a computation error on GPU errors; only Metal exits temporarily
type: bug
status: confirmed
severity: medium
platforms: [cuda, opencl]
area: gpu-runtime
action: decide
next: Decide whether CUDA and OpenCL GPU errors may end in boinc_temporary_exit() with a retry cap of about 5, as Metal does, instead of a computation error
pattern: PAT-013
aliases: [K-B4]
reported: 2026-10-21
source: audit 2026-10-21 (plan §1 and §6.4, K-B4)
where: boinc/main_kampylos_opencl_boinc.cpp:116-127 @ 45a41d5
fixed_in:
---

## Symptom

A CUDA or OpenCL task that meets a GPU error (launch failure, out of memory, driver reset, device missing after a
driver update) ends as a computation error: stderr has `gpu_fit_binary CUDA error ...`, `GpuFitContext: ... failed`,
`opencl_fit: ... failed with OpenCL error N` or `opencl_fit_select_device(N) failed`. The work done is lost and the
host's error count goes up. No volunteer report collected here.

## Cause

Verified by reading at 45a41d5 (no CUDA or OpenCL hardware was used):

* CUDA: device selection failure ends with `boinc_finish(1)` (`boinc/main_kampylos_gpu_boinc.cpp:92-96`). Every
  runtime error in a round goes through `CUDA_CHECK_RT`, which calls `exit(1)` (`src/gpu_fit_binary.cu:23-29`, used
  at `:43`, `:48`, `:51`); allocation and upload failures go through `GpuFitContext::check`, also `exit(1)`
  (`src/gpu_fit_binary.h:49-54`). A non-zero exit without `boinc_finish` is a computation error for the client too.
* OpenCL: a failed `opencl_fit_select_device()` (no ICD, no platform, no device, kernel build) ends with
  `boinc_finish(1)` (`boinc/main_kampylos_opencl_boinc.cpp:100-104`). A runtime error throws from `opencl_fail()`
  (`src/kampylos_opencl_fit.cpp:139-145`) and is caught at `main:120-126`, which calls `boinc_finish(1)` although the
  comment there (`:121-123`) and at `src/kampylos_opencl_fit.cpp:134-138` says the client restarts the task from its
  checkpoint. It does not: `boinc_finish(1)` reports the task as failed.
* `opencl_fit_upload_light_curve()` (`main:105`) can throw (`clCreateBuffer` failure, `src/kampylos_opencl_fit.cpp:131`)
  outside the `try`: the exception is uncaught and the process ends through `std::terminate`.
* Metal does it the intended way since 5a65505: no device or a self-test that cannot run ->
  `boinc_temporary_exit(300)` (`boinc/main_kampylos_metal_boinc.cpp:79-87`); command buffers that keep failing ->
  `boinc_temporary_exit(300)` (`:103-112`).

## Fix

Proposed (plan K-B4), not written yet: classify the exits as PAT-013 describes. "No usable device right now" and
runtime errors during a round end with `boinc_temporary_exit()` and an application retry counter (a small file in
the slot directory, reset when a cell or in-cell checkpoint is written, about 5 exits without progress, then
`boinc_finish(1)`); input errors and kernel build failures stay `boinc_finish(1)`. Move the light-curve upload into
the `try`, and replace `exit(1)` in the CUDA checks by an exception the wrapper catches. Fix the comments.

Open question for the maintainers (plan §8, question 4): is moving transient CUDA and OpenCL errors from "computation
error" to `boinc_temporary_exit` with a cap of about 5 retries acceptable for all GPU apps? Metal has no retry
counter of its own and relies on the client's: the client aborts a task after more than 100 temporary exits
(`client/app_control.cpp:383-392` at BOINC 4d1529d, see `docs/kb/shared/boinc-api.md`) and resets the count when the
task checkpoints (`:1566`), as the comment at `boinc/main_kampylos_metal_boinc.cpp:15-16` says. At a 300 s delay that
is more than 8 hours of retries without progress.

## Test

Needs a failing GPU call: for OpenCL a loader stub with the same `cl_api` table whose `EnqueueNDRangeKernel` fails
on the Nth call; for CUDA a build with a test hook in `CUDA_CHECK_RT`. Run the wrapper on `tests/boinc_stub` (its
`boinc_temporary_exit` writes the file `boinc_temporary_exit` and exits 0): the run must end with a temporary exit and
the in-cell checkpoint must be intact.

## Log

- 2026-10-27: recorded from the 2026-10-21 audit, verified at 45a41d5 by reading.
- 2026-10-27: re-checked after PAT-013 and boinc-api.md were corrected; the Metal comment about the client's
  limit of 100 temporary exits is right (BOINC 4d1529d), verdict unchanged.
