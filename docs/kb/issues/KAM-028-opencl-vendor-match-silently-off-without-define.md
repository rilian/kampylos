---
id: KAM-028
title: The OpenCL vendor match is silently off when EXPECTED_GPU_VENDOR is not defined, and no build line here defines it
type: bug
status: confirmed
severity: medium
platforms: [opencl]
area: build
next: Match the OpenCL platform to aid.gpu_type from init_data, as boinc_get_opencl_ids() does, so a build without EXPECTED_GPU_VENDOR is still right
pattern: [PAT-028, PAT-012]
aliases: []
reported: 2026-10-27
source: knowledge-base review 2026-10-27 (check of patterns PAT-028 and PAT-012)
where: src/kampylos_opencl_fit.cpp:30-36 @ 45a41d5
fixed_in:
---

## Symptom

An OpenCL binary built without `-DEXPECTED_GPU_VENDOR="..."` runs, but takes the first OpenCL platform that has
any device, whatever its vendor: the forum thread 64 symptom (KAM-009) comes back on hosts with GPUs of two vendors.
Nothing in stderr says that the vendor check is off. Whether a shipped OpenCL binary lacks the define is not known
here.

## Cause

Verified by reading at 45a41d5:

* `src/kampylos_opencl_fit.cpp:30-32` defaults `EXPECTED_GPU_VENDOR` to `""`. The matcher of d7f9ef7 returned true
  for an empty vendor, and the error text at `:80-82` still treats `""` as "any vendor"; the shared matcher that
  replaced it in 3349f90 (`cl_platform_matches_vendor()`, `cl_dynload`) is in the parent repository and was not
  read. So the fix of KAM-009 exists only in builds that pass the define.
* No build line in this repository passes it: `boinc/README.md` has build lines for the CPU and Metal apps only,
  and the OpenCL test build lines (`tests/test_opencl_fit_binary.cpp:8-11`) have no `-D`. The release builds are made
  outside this repository.
* The log line of `opencl_fit_select_device()` names the device (`:92`) but not the expected vendor.
* The release process has dropped `-D` flags before: the macOS CPU binary shipped as version 12 printed "v4" and the
  Metal binary shipped as version 2 printed "v1" (kampylos-report-2026-10-16.html §3), that is, they were built without
  `-DKAMPYLOS_VERSION` / `-DKAMPYLOS_METAL_VERSION`. That one only affects the version in stderr.

The other guards in `src/` and `boinc/` were checked and are harmless or loud: the version defaults (cosmetic),
`VBM_THREAD_SAFE` / `VBM_THREAD_LOCAL_STATICS` (the thread count is printed, `completion step: N CPU thread(s)`),
`__MAC_15_0` around `MTLMathModeSafe` (fast math is still turned off and the self-test refuses a wrong compile,
KAM-013), `KAMPYLOS_CLI_*` (CLI only), `KAMPYLOS_BATCHED_CHUNK_SECONDS` and `KAMPYLOS_PATCH_THREADS` (tuning).

## Fix

Not written yet: make the missing define loud. Either `#error` when it is not set in a BOINC build (with a
`-DEXPECTED_GPU_VENDOR=any` escape for test builds), or print the expected vendor and the chosen platform's vendor to
stderr at start. Better still, match the platform against `aid.gpu_type` from `init_data` (PAT-012's fix) so no define
is needed. Put the OpenCL and CUDA release build lines, with all their `-D` flags, into `boinc/README.md`.

## Test

Build the OpenCL wrapper without the define: after the fix the build fails, or stderr says that no vendor is
enforced. On a host with two vendors' platforms the chosen platform must be the expected one.

## Log

- 2026-10-27: found while checking PAT-028; verified at 45a41d5 by reading.
