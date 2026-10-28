---
id: PAT-015
title: OpenCL work-group size hard-coded instead of fitted to the device and the kernel
status: active
severity: medium
platforms: [opencl]
area: gpu-runtime
applies_to: OpenCL kernel launches with an explicit local work size
check: 'clEnqueueNDRangeKernel|reqd_work_group_size|__local'
origin: kampylos KAM-006
reference_fix: kampylos@a65e139 (proposed, not merged; see kampylos KAM-006)
added: 2026-10-27
---

## The defect

The launch passes a fixed local size (64, 128, 256). Every desktop GPU accepts 128, but
`CL_DEVICE_MAX_WORK_GROUP_SIZE` is 64 on some embedded and older GPUs, and a register-heavy kernel can be limited
below the device maximum by the driver (`CL_KERNEL_WORK_GROUP_SIZE`). Every launch then fails with
`CL_INVALID_WORK_GROUP_SIZE`: the task errors out, or, if launch errors are not checked, silently reuses old results.

## Why it happens

The limit depends on the device and on the compiled kernel; it is never hit on the developer's GPU.

## How to check a repository

`git grep -n clEnqueueNDRangeKernel` and look at the local work size argument and where it comes from; also
`reqd_work_group_size` attributes and `__local` arrays sized by a constant.

* `affected`: a constant local size with no clamp to both limits.
* `not-affected`: `NULL` local size, or the size is clamped to `CL_DEVICE_MAX_WORK_GROUP_SIZE` and
  `CL_KERNEL_WORK_GROUP_SIZE` (and local memory sized to match), or the kernel needs exactly that size and the device
  and kernel limits are checked before use, with a clear message when a device cannot run it.
* `n/a`: no OpenCL build.

## How to fix

Query both limits after building the kernel, use the largest size the kernel's algorithm allows (a power of two for
tree reductions) that fits, and on `CL_INVALID_WORK_GROUP_SIZE` at the first launch halve and retry. A changed
size changes the summation order: a rounding-level difference that is the same for a run and its resume on one
device.

## How to test

Force a smaller limit through an environment variable in a test build, or run on a device with a maximum of 64; the
run must complete and log the size it chose.
