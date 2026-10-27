---
id: PAT-012
title: OpenCL platform or device list differs from the one BOINC's device index refers to
status: active
severity: medium
platforms: [opencl]
area: device-selection
applies_to: OpenCL applications that enumerate platforms and devices themselves
origin: kampylos KAM-009
reference_fix: kampylos@d7f9ef7 (platform chosen by the vendor BOINC assigned)
added: 2026-10-27
---

## The defect

The index the client assigned (`gpu_opencl_dev_index`) is applied to a different list than the one it counts, so the
task runs on the wrong device, on another vendor's GPU, or on a CPU OpenCL device (POCL, Intel's CPU runtime, Apple's
CPU device): very slow, or wrong-precision failures. Typical forms:

* (a) the platform is the first one that has a GPU, not the one whose vendor matches `gpu_type` (hosts with NVIDIA
  plus Intel, or AMD plus NVIDIA, drivers);
* (b) devices are counted or fetched with another type than `CL_DEVICE_TYPE_GPU` (`CL_DEVICE_TYPE_ALL` or `DEFAULT`
  for the count, the list, or both), so the index names another device and index 0 can be a CPU device;
* (c) a fallback to `CL_DEVICE_TYPE_ALL` when no GPU is found, which quietly runs a GPU task on the CPU.

## Why it happens

BOINC counts the `CL_DEVICE_TYPE_GPU` devices of the platform whose vendor matches `gpu_type`
([boinc-api.md](../boinc-api.md#gpu-assignment-init_dataxml)); a hand-written enumeration that differs in any step
gives a different list. Development machines usually have a single platform with a single GPU.

## How to check a repository

```
git grep -nE 'clGetPlatformIDs|clGetDeviceIDs|CL_DEVICE_TYPE_|CL_PLATFORM_VENDOR|gpu_type|boinc_get_opencl_ids'
```

* `affected`: any of (a) to (c).
* `not-affected`: `boinc_get_opencl_ids()`, or an enumeration that picks the platform by `gpu_type` and uses
  `CL_DEVICE_TYPE_GPU` for both the count and the list.
* `n/a`: no OpenCL build.

As for PAT-011, check that the code path that does this is compiled into the shipped builds (preprocessor guards
against the `-D` flags of the build scripts).

## How to fix

Use `boinc_get_opencl_ids()` where the build links BOINC's OpenCL helper, otherwise mirror it: match the platform
vendor against `aid.gpu_type` (kampylos `platform_vendor_matches()`, d7f9ef7), `CL_DEVICE_TYPE_GPU` everywhere, no CPU
fallback for a GPU task (exit with a clear message, or a temporary exit, PAT-013).

## How to test

Needs a host or a loader stub with two platforms, or a GPU platform that also lists a CPU device (POCL). Manual on
real hardware otherwise: log the platform vendor, device type and name that were chosen.
