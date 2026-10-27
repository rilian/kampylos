---
id: PAT-016
title: One GPU launch runs long enough to hit the display driver's watchdog
status: active
severity: medium
platforms: [windows, cuda, opencl, metal]
area: gpu-runtime
applies_to: GPU applications whose single kernel launch or command buffer can run longer than about a second on a slow GPU
origin: kampylos KAM-011
reference_fix: kampylos@53723b7 (Metal command buffers split and retried)
added: 2026-10-27
---

## The defect

Windows resets the display driver when one GPU job runs longer than about 2 s (TDR, `TdrDelay`); macOS and Linux
with a display attached have similar watchdogs. The launch then fails (`cudaErrorLaunchTimeout`,
`CL_OUT_OF_RESOURCES`, a Metal command-buffer error), the screen may flicker, and the task errors out (or, once
PAT-013 is fixed, retries on that host). Fast development GPUs and small test work units never get near the limit;
slow volunteer GPUs with the largest work units do.

## Why it happens

A whole round of work is sent in one launch to minimise overhead. The duration scales with the work unit and the
slowest supported GPU, not with the developer's GPU.

## How to check a repository

`git grep -nE 'clEnqueueNDRangeKernel|<<<|cudaLaunch|commandBuffer|dispatchThreadgroups'` and, for each launch,
estimate the worst case: largest work unit times the slowest GPU the plan class allows. Measure if possible.

* `affected`: one launch can exceed about 1 s on a supported GPU.
* `not-affected`: launches are split into chunks with a time budget (well under a second), or measured worst cases
  are far below the limit (say which GPU and work unit in Evidence). Chunks made for checkpointing (every few
  seconds) are not chunks for the watchdog: what counts is the duration of one launch.
* `n/a`: no GPU build.

## How to fix

Split the work into chunks and launch them one after the other; adapt the chunk size to the measured time per
launch. Checkpoint between chunks when rounds are long (kampylos in-cell checkpoints).

## How to test

Log the duration of each launch on the slowest available GPU with the largest work unit.
