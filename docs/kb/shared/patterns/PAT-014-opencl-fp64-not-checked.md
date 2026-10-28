---
id: PAT-014
title: The OpenCL kernel needs double precision but device selection does not check for it
status: active
severity: medium
platforms: [opencl]
area: device-selection
applies_to: OpenCL kernels that use double
check: 'cl_khr_fp64|CL_DEVICE_DOUBLE_FP_CONFIG|CL_DEVICE_EXTENSIONS|clCreateProgramWithSource'
origin: kampylos KAM-005
reference_fix:
added: 2026-10-27
---

## The defect

The kernel uses `double`, the device has no `cl_khr_fp64` (most Intel integrated GPUs, many mobile GPUs, Apple
Silicon), and the task fails with a kernel build log or a launch error instead of a clear message, or is accepted by a
device whose double precision is emulated and very slow.

## Why it happens

Development GPUs all support fp64; device selection checks the vendor and type only.

## How to check a repository

```
git grep -nE 'double|cl_khr_fp64' -- '*.cl' '*kernel*'
git grep -nE 'CL_DEVICE_DOUBLE_FP_CONFIG|cl_khr_fp64|CL_DEVICE_EXTENSIONS'
```

* `affected`: the kernel needs double and the host code does not check the device for it.
* `not-affected`: the device is checked and a device without fp64 gets a clear message (and the plan class or the
  documentation says which devices are supported).
* `n/a`: no OpenCL build, or the kernels are float only.

## How to fix

Query `CL_DEVICE_DOUBLE_FP_CONFIG` (non-zero) or look for `cl_khr_fp64` in `CL_DEVICE_EXTENSIONS`; refuse the device
with an explicit message. The server's plan class should not send the app to such devices at all.

## How to test

Run on Apple Silicon OpenCL (no fp64): the stderr must say why the device was refused.
