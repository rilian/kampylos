---
id: KAM-018
title: The batched checkpoint parser does not range-check indices and vector sizes
type: bug
status: confirmed
severity: low
platforms: [cuda, opencl, metal]
area: checkpoint
next: In kampylos_batched_state_from_text(), reject out-of-range phase and idx values, x and pending vectors not of length 5, and sizes other than n_seeds
pattern:
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan §6.4)
where: src/kampylos_batched_fit.h:132-173 @ 45a41d5
fixed_in:
---

## Symptom

None reported. A `kampylos_batched_checkpoint` that parses but is inconsistent (a bug in a future writer, or a
damaged file that still parses) makes the GPU app read or write out of bounds (a crash or a wrong cell) instead of
starting the cell over.

## Cause

Verified by reading at 45a41d5. `kampylos_batched_state_from_text()` checks the tag and version, upper limits of the
counts, each stepper's `n` (1..32) and its simplex and fval sizes, the final `end` tag and
`idx.size() == steppers.size()`; the caller checks cell, seed count and restart count (`:303-304`). It does not check:

* `phase` in [0, n_restarts];
* `current.size()` and `active.size()` equal to the seed count once phase > 0;
* each `idx` value in [0, n_seeds): used as `s.current[i]` and `s.active[i]` (`:331-334`);
* the length 5 of each result vector `x`: `current[best].x[0..4]` is read at `:354`;
* the length of each pending point: `kampylos_to_candidate()` reads `x[0..4]` (`:50-53`, called at `:231-233`);
* the sizes of `centroid`, `worst`, `xr`, `xe`, `xc` against `n`.

The file is written by the application itself through a temporary file and a rename, so this needs a bug or a damaged
file to matter.

## Fix

Not written yet: check those invariants in the parser and return false, so the cell starts over.

## Test

In `tests/test_batched_resume.cpp`: take a valid state text, change one `idx` value to the seed count, or shorten one
`x` vector, and require `kampylos_batched_state_from_text()` to return false.

## Log

- 2026-10-27: recorded from the 2026-10-21 audit, verified at 45a41d5 by reading.
