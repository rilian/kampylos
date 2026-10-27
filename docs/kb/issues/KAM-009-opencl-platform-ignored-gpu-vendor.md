---
id: KAM-009
title: OpenCL platform was chosen without matching the GPU vendor, so Intel GPU tasks ran on NVIDIA
type: bug
status: fixed
severity: medium
platforms: [opencl]
area: device-selection
pattern: PAT-012
aliases: []
reported: 2026-10-06
source: forum thread 64
where: src/kampylos_opencl_fit.cpp:30-36 @ 45a41d5
fixed_in: d7f9ef7, 3349f90
---

## Symptom

Forum thread 64, "Intel GPU tasks run on Nvidia": on hosts with GPUs of two vendors, tasks sent for one vendor's GPU
ran on the other's.

## Cause

`opencl_fit_select_device()` took the first platform that reported any device, whatever its vendor.

## Fix

d7f9ef7 (2026-10-06) skips every platform whose vendor does not match the compile-time `EXPECTED_GPU_VENDOR`; each
vendor-specific `opencl_*` plan class gets its own binary built with that define. 3349f90 (2026-10-07) replaced the
local matcher by the shared `cl_platform_matches_vendor()` of `cl_dynload` (parent repository). Verified at 45a41d5:
`src/kampylos_opencl_fit.cpp:30-36` and the platform loop `:70-78`.

Limits, checked on 2026-10-27:

* The vendor is a build constant, not `aid.gpu_type`. A binary built without `-DEXPECTED_GPU_VENDOR` (default `""`)
  still takes the first platform with any device. The release build lines are not in this repository, so whether
  every OpenCL release sets the define was not verified.
* The device count and the device list use the same type, `CL_DEVICE_TYPE_ALL` (`:73`, `:86`), so they agree with
  each other, but not with BOINC, which counts `CL_DEVICE_TYPE_GPU` devices only: KAM-022.

## Test

No automated test. Manual on a host with two vendors' OpenCL platforms: stderr names the chosen device
(`opencl_fit_select_device: using OpenCL device "..."`).

## Log

- 2026-10-06: fixed in d7f9ef7; 2026-10-07: 3349f90.
- 2026-10-27: recorded, verified at 45a41d5 by reading; device type found inconsistent with BOINC (KAM-022).
