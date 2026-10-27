---
id: KAM-022
title: The OpenCL device list includes non-GPU devices, and an out-of-range index silently becomes device 0
type: bug
status: confirmed
severity: medium
platforms: [opencl]
area: device-selection
next: In opencl_fit_select_device(), use CL_DEVICE_TYPE_GPU for the count and the list, and stop with a clear message on an out-of-range index
pattern: PAT-012
aliases: []
reported: 2026-10-27
source: knowledge-base review 2026-10-27 (check of pattern PAT-012)
where: src/kampylos_opencl_fit.cpp:70-88 @ 45a41d5
fixed_in:
---

## Symptom

Expected, not reported: on a host whose OpenCL platform lists a CPU or accelerator device next to its GPUs, an OpenCL
task can run on a different device than BOINC assigned, or on the CPU device (very slow, and outside the client's CPU
accounting). An index that does not exist in the list is quietly replaced by 0.

## Cause

Verified by reading at 45a41d5:

* the platform scan counts devices with `CL_DEVICE_TYPE_ALL` (`src/kampylos_opencl_fit.cpp:73`) and the list is
  fetched with `CL_DEVICE_TYPE_ALL` (`:86`). BOINC's `gpu_opencl_dev_index` counts only the `CL_DEVICE_TYPE_GPU`
  devices of the vendor's platform (`docs/kb/shared/boinc-api.md`, "GPU assignment"), so the same index can name
  another device. A platform with only a CPU device also passes the "has a device" test.
* `if (device_id < 0 || (cl_uint)device_id >= n_devices) device_id = 0;` (`:87`) hides a wrong index.
* The tests enumerate with `CL_DEVICE_TYPE_GPU` (`tests/test_kampylos_opencl.cpp:61-63`), so they cannot show it.

Which drivers list CPU and GPU devices on one platform on volunteers' hosts was not checked here.

## Fix

Not written yet: `CL_DEVICE_TYPE_GPU` for both the count and the list, no fallback to other device types; an index
out of range ends with a clear message (or a temporary exit, PAT-013) instead of device 0; log the vendor, type and
name of the chosen device. Alternatively `boinc_get_opencl_ids()`, if the build links BOINC's OpenCL helper.

## Test

A host or loader stub with a platform that lists a CPU device before its GPU (POCL, Intel's CPU runtime); otherwise
manual: stderr must name a GPU device and the index BOINC assigned.

## Log

- 2026-10-27: found while checking PAT-012 (KAM-009 fixed the platform, not the device type); verified at 45a41d5 by
  reading.
