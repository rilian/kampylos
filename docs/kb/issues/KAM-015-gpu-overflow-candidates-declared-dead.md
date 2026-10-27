---
id: KAM-015
title: GPU apps declared a candidate dead when it needed more than 512 finite-source points
type: bug
status: fixed
severity: high
platforms: [cuda, opencl, metal]
area: science
pattern:
aliases: [fix/gpu-overflow-candidates-on-cpu]
reported: 2026-10-19
source: Metal review 2026-10-19 §3.2 (event ev25055)
where: src/kampylos_gpu_complete.h:68-85 @ 45a41d5
fixed_in: 34168e1
---

## Symptom

On events whose single-lens anchor has no baseline (`n_peak` close to `npts`; ev25055, 1191 points) the GPU apps
discarded most candidates: 1100 of a cell's 1440 starting vertices and 31416 of 42084 evaluated candidates (75 %).
Their seeds collapsed on a flat 1e18 landscape within about 30 iterations, and the cell's result was the best of the
few surviving seeds, reported with a normal-looking chi2.

## Cause

The kernels list at most 512 finite-source points per candidate (`KAMPYLOS_MAX_BAD_POINTS`,
`src/kampylos_gpu_types.h:34`; Metal's own constant is also 512) and set `overflow` beyond that.
`kampylos_complete_candidate()` returned chi2 = 1e18 for an overflowed candidate.

## Fix

34168e1, merged in 45a41d5 (2026-10-19): an overflowed candidate's objective is computed on the CPU in full, every
point through `BinaryMag2`, as the CPU app does (`src/kampylos_gpu_complete.h:68-85`); the point count is passed
through `complete_batch()` in all three backends. Such candidates cost what they cost the CPU app (3.6x the CPU time
of the discarding version over the first 14 rounds of the ev25055 cell); the seed-split work units keep tasks short.
Planet work unit: output byte-identical to before (no overflow there). Verified by reading at 45a41d5.

Leftovers, comments only: the comment of `CompletedFit::dead` (`:16-18`) still lists overflow as a reason; and
`src/kampylos_cl_embedded.h` was not regenerated after the comment change in `kampylos_gpu_types.h`, so the embedded
OpenCL source differs from the file in that comment only (checked on 2026-10-27 by regenerating it into a scratch
directory).

## Test

No automated overflow test. The ev25055 cell with a GPU app shows it (stderr per round on an instrumented build);
`tests/test_kampylos_gpu_kernel.cu` and `tests/test_kampylos_opencl.cpp` were adapted to the new signature.

## Log

- 2026-10-19: fixed in 34168e1, merged in 45a41d5.
- 2026-10-27: recorded, verified at 45a41d5 by reading.
