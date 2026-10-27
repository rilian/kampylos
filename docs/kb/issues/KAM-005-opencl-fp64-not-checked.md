---
id: KAM-005
title: The OpenCL kernel needs cl_khr_fp64, but device selection does not check for it
type: bug
status: confirmed
severity: medium
platforms: [opencl]
area: device-selection
next: In opencl_fit_select_device(), refuse a device whose CL_DEVICE_DOUBLE_FP_CONFIG is 0 with a clear message before building the kernel
pattern: PAT-014
aliases: [K-fp64]
reported: 2026-10-21
source: audit 2026-10-21 (plan §3.3 and §6.4, K-fp64)
where: src/kampylos_opencl_fit.cpp:50-118 @ 45a41d5
fixed_in:
---

## Symptom

On an OpenCL device without double precision the task fails at start with
`opencl_fit_select_device: kernel build failed:` and the compiler's build log, then
`opencl_fit_select_device(N) failed: kernel build failed`, and is reported as a computation error. No report
collected here.

## Cause

`src/kampylos_kernel.cl:9-16` enables `cl_khr_fp64` and computes in `double` throughout. Its comment says a device
without the extension "gets filtered out by the host-side init code"; it is not: `opencl_fit_select_device()`
(`src/kampylos_opencl_fit.cpp:50-118`) never queries `CL_DEVICE_DOUBLE_FP_CONFIG` or `CL_DEVICE_EXTENSIONS`
(`git grep` finds neither at 45a41d5). The build then fails at `:104-114` and the wrapper calls `boinc_finish(1)`
(`boinc/main_kampylos_opencl_boinc.cpp:100-104`). Devices without fp64 include most Intel integrated GPUs, Apple
Silicon's OpenCL and many mobile GPUs. Which devices the server's plan classes send the OpenCL app to is not visible
here. Verified by reading at 45a41d5.

## Fix

Not written yet (plan K-fp64): after choosing the device, query `CL_DEVICE_DOUBLE_FP_CONFIG` (non-zero) or look for
`cl_khr_fp64` in `CL_DEVICE_EXTENSIONS`, and refuse the device with a clear message. A device never gains fp64, so a
clear error is right here; the better fix is a plan class that does not send the app to such devices.

## Test

Build the OpenCL wrapper (needs `cl_dynload.c` from the parent repository) and run it on Apple Silicon's OpenCL (no
fp64): stderr must say that the device has no double precision.

## Log

- 2026-10-27: recorded from the 2026-10-21 audit, verified at 45a41d5 by reading.
