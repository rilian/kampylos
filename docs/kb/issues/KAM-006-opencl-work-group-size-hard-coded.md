---
id: KAM-006
title: OpenCL work-group size is hard-coded to 128
type: bug
status: fix-proposed
severity: medium
platforms: [opencl]
area: gpu-runtime
next: Review and merge a65e139 (tag archive/fix/opencl-work-group-size); it merges cleanly into 45a41d5
pattern: PAT-015
aliases: [fix/opencl-work-group-size]
reported: 2026-10-12
source: review 2026-10-12 (commit a65e139), kampylos-report-2026-10-16.html §4.3
where: src/kampylos_opencl_fit.cpp:171-186 @ 45a41d5
fixed_in:
---

## Symptom

On an OpenCL device whose work-group limit is below 128 (`CL_DEVICE_MAX_WORK_GROUP_SIZE` 64 on some embedded and
older GPUs, or a lower per-kernel limit for this register-heavy double-precision kernel) every launch fails:
`opencl_fit: clEnqueueNDRangeKernel failed with OpenCL error -54` (`CL_INVALID_WORK_GROUP_SIZE`), and the task ends
as a computation error (KAM-002). No volunteer report.

## Cause

`opencl_eval_batch()` sets `local_size = 128` (`src/kampylos_opencl_fit.cpp:171`), sizes the local memory for it
(`:172`, `:180`) and launches with it (`:184-186`). Neither device nor kernel limit is queried (`git grep
WORK_GROUP_SIZE -- src` finds nothing). Since the launch checks of 2026-10-10 the error ends the task; before, the
previous round's results were read back. Metal already fits its threadgroup to the pipeline maximum
(`src/kampylos_metal_fit.mm:603-606`). Verified by reading at 45a41d5.

## Fix

Commit a65e139 (tag `archive/fix/opencl-work-group-size` of rilian/kampylos; the branch was closed on 2026-10-27,
not merged upstream): `opencl_fit_select_device()` lowers the size to `CL_DEVICE_MAX_WORK_GROUP_SIZE`, and the launch
halves it and retries on `CL_INVALID_WORK_GROUP_SIZE`, because the loader table of `cl_dynload.h` has no
`clGetKernelWorkGroupInfo`. The kernel reads `get_local_size()`, so any power of two works; on an affected device the
per-candidate sums are added in another order (a rounding-level difference that is the same for a run and its
resume). Only syntax-checked (2026-10-16 report). On 2026-10-27 `git merge-tree` in a scratch clone showed that
a65e139 still merges into 45a41d5 without conflicts.

## Test

A device with a limit of 64, or a test build that forces a lower limit through an environment variable: the run must
complete and log the size it chose.

## Log

- 2026-10-12: fix proposed in a65e139.
- 2026-10-27: recorded from the 2026-10-21 audit, verified at 45a41d5 by reading; a65e139 checked to merge cleanly.
