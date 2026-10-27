---
id: KAM-026
title: fit_event ignores the seed-split token "part i K" and fits every seed
type: bug
status: confirmed
severity: low
platforms: [all]
area: science
next: In src/fit_event.cpp, pass kampylos_seed_part(seeds, in.part, in.nparts) to every backend's fit_cell
pattern:
aliases: []
reported: 2026-10-27
source: knowledge-base review 2026-10-27
where: src/fit_event.cpp:94-113 @ 45a41d5
fixed_in:
---

## Symptom

`fit_event` run on a seed-split work unit (`... log10 part 1 3`) writes the result of all seeds, not of part 1, so it
does not reproduce what the BOINC apps compute for that work unit.

## Cause

Verified by reading at 45a41d5. `fit_event` parses the input with `kampylos_read_input_file()`, which accepts
`part i K` and sets `in.part`/`in.nparts`, but every backend's `fit_cell` passes the whole seed set:
`gpu_fit_binary_seeds` (`src/fit_event.cpp:94`), `opencl_fit_binary_seeds` (`:100`), `metal_fit_binary_seeds`
(`:107-110`) and `fit_binary_seeds` for the CPU (`:113`). The BOINC wrappers use `kampylos_fit_cell_resumable()` (CPU)
or `kampylos_seed_part()` (GPU, since 2a375ba). The header comment (`:1-3`) says the CLI runs exactly the same code
path as the BOINC apps. The regression suite uses no `part` token, so it is not affected.

## Fix

Not written yet: apply `kampylos_seed_part(seeds, in.part, in.nparts)` in each `fit_cell`, and print the
"seed-split work unit" line as the wrappers do.

## Test

Run `fit_event` on the synthetic planet with `flux 0.2 0.2 1 -3 -3 1 log10 part 1 3` and compare with the CPU wrapper
on `tests/boinc_stub` for the same input: the lines must be equal.

## Log

- 2026-10-27: found while writing AGENTS.md; verified at 45a41d5 by reading.
