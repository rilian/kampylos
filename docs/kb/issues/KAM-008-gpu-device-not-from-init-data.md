---
id: KAM-008
title: GPU apps used the device from --device instead of the one the client assigned in init_data
type: bug
status: fixed
severity: high
platforms: [cuda, opencl]
area: device-selection
pattern: PAT-011
aliases: []
reported: 2026-10-05
source: forum report 2026-10-05 (main BOINC forum)
where: boinc/main_kampylos_opencl_boinc.cpp:71-77 @ 45a41d5
fixed_in: b5f5ef2
---

## Symptom

"Only one GPU being used but BOINC says one task is on GPU0 and the other on GPU1": on a host with an AMD discrete
plus integrated GPU, every OpenCL task ran on the same device whatever GPU BOINC had assigned.

## Cause

The wrappers read only the deprecated `--device N` command-line argument. BOINC passes its choice in
`APP_INIT_DATA`: `gpu_opencl_dev_index` for OpenCL (an index into OpenCL's own enumeration, which can differ from
`gpu_device_num`) and `gpu_device_num` for CUDA.

## Fix

Fixed on 2026-10-05 in the parent repository, before the wrappers moved here. b5f5ef2 (2026-10-10), the commit that
added the BOINC wrappers to this repository, already contains the fix: `git log -S gpu_opencl_dev_index` and
`git log -S gpu_device_num` list b5f5ef2 as the first commit. Verified at 45a41d5:
`boinc/main_kampylos_gpu_boinc.cpp:67-71` uses `aid.gpu_device_num` when it is >= 0;
`boinc/main_kampylos_opencl_boinc.cpp:71-77` uses `aid.gpu_opencl_dev_index` when it is >= 0 and logs the override;
`--device` stays a fallback for standalone runs.

Metal has no device choice: the system default device, else the non-low-power device of `MTLCopyAllDevices()`
(`src/kampylos_metal_fit.mm:548-566`). Apple Silicon has one GPU, and the Metal binary seen on a volunteer host is
`kampylos_metal_v2_macos_arm64` (kampylos-report-2026-10-16.html §3); an Intel Mac with two GPUs would ignore BOINC's
assignment, should a Metal build for x86_64 ever be shipped. The `init_data` code of both wrappers is not behind any
preprocessor guard, so every build has it. Still open on the OpenCL side: the index is applied to a list that is not BOINC's
(KAM-022).

## Test

No automated test: `tests/boinc_stub/boinc_api.h:18-23` has the two fields, but no environment variable sets them.
Manual: run the OpenCL wrapper on a two-GPU host, or with a stub `init_data` index 1 and `--device 0`; stderr must
show `overriding device index 0 -> 1`.

## Log

- 2026-10-05: forum report; fixed the same day in the parent repository.
- 2026-10-10: arrived in this repository with b5f5ef2.
- 2026-10-27: recorded, verified at 45a41d5 by reading.
- 2026-10-27: re-checked after PAT-011 was corrected (guards, Metal on multi-GPU Macs): no guard around the
  `init_data` code; Metal noted; verdict unchanged.
