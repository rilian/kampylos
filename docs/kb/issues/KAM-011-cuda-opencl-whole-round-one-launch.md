---
id: KAM-011
title: CUDA and OpenCL send a whole round of candidates in one kernel launch, which can exceed the Windows TDR limit
type: bug
status: confirmed
severity: medium
platforms: [windows, cuda, opencl]
area: gpu-runtime
next: Split the CUDA and OpenCL kernel launch of a round into chunks of candidates with a measured time budget well under 1 s, as Metal does
pattern: PAT-016
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan §3.3)
where: src/gpu_fit_binary.cu:45-48 @ 45a41d5
fixed_in:
---

## Symptom

Expected, not reported: on a slow GPU with a large light curve one launch runs longer than the display driver's
watchdog (Windows TDR, 2 s by default); the launch fails (`cudaErrorLaunchTimeout`, `CL_OUT_OF_RESOURCES`), the screen
may flicker, and the task ends as a computation error (KAM-002).

## Cause

Verified by reading at 45a41d5; launch durations were not measured.

* `gpu_eval_batch()` launches one block per candidate for every candidate of the call in one kernel
  (`src/gpu_fit_binary.cu:45-48`); OpenCL does the same (`src/kampylos_opencl_fit.cpp:184-186`). Each block loops over
  every data point in double precision (a quintic root solve per point). The first round of a cell has 1440
  candidates (2026-10-19 review); light curves have hundreds to several thousand points.
* bf85fb2 splits a round only when one `eval()` call (kernel plus CPU completion) takes longer than 15 s, and aims at
  15 s per call (`src/kampylos_batched_fit.h:189-204`). That is for checkpoints, not for the watchdog.
* Metal splits each round into command buffers of 4.0e6 candidate x point evaluations, about 0.5 s on a base M1
  (1440 candidates x 4010 points took 0.69 s; `src/kampylos_metal_fit.mm:497-504`, `:692-729`, since 53723b7). Consumer
  NVIDIA and AMD GPUs run double precision at 1/16 to 1/64 of their float rate, so a whole round on a slow card can
  take longer than on the M1.

## Fix

Not written yet: launch a round in chunks of candidates with a time budget well under a second, measured per launch
and adapted, as Metal does. Each candidate is one work-group, so chunking does not change results.

## Test

Log the duration of each launch on the slowest supported GPU with the largest light curve. On Windows a host with a
short `TdrDelay` shows the failure.

## Log

- 2026-10-27: recorded from the 2026-10-21 audit, verified at 45a41d5 by reading.
