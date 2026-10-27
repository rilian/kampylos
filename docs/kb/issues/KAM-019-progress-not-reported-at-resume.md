---
id: KAM-019
title: Progress is not reported at resume start, only after the first seed or round finishes
type: bug
status: confirmed
severity: low
platforms: [all]
area: boinc-lifecycle
next: Call boinc_fraction_done(start_cell / total) in kampylos_boinc_open_output() and report in-cell progress right after an in-cell checkpoint is read
pattern: PAT-007
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan §6.4)
where: boinc/kampylos_boinc_common.h:180-215 @ 45a41d5
fixed_in:
---

## Symptom

After a restart nothing reports the resumed position until the first new seed (CPU) or round (GPU) finishes. Before
that the task also recomputes the single-lens anchor. A CPU seed takes 24-50 minutes on events without a baseline
(kampylos-report-2026-10-16.html §4.2), and a long GPU round minutes, so the client's progress and remaining-time
estimate are stale for that long.

## Cause

Verified by reading at 45a41d5. `boinc_fraction_done()` is called only in `kampylos_boinc_after_cell()` (`:222`), by
the seed hook after each seed (`:254`, from `src/kampylos_cpu_cell.h:87`), by the batched hook after each round
(`:271`, from `src/kampylos_batched_fit.h:249-257`; not between the chunks of a long round), and at the end (`:237`).
`kampylos_boinc_open_output()` (`:180-215`) and the in-cell resume code (`src/kampylos_cpu_cell.h:72-76`,
`src/kampylos_batched_fit.h:300-309`) report nothing.

## Fix

Not written yet: `boinc_fraction_done((double)start_cell / total)` in `kampylos_boinc_open_output()`; after an
in-cell checkpoint is read, report `(cell + own_before / own_total) / total` (CPU; `own_before` is computed in
`kampylos_fit_cell_resumable()`) or the round formula of `src/kampylos_batched_fit.h:250-256` for the resumed state
(GPU); and report between chunks of a long round.

## Test

Change the stub's `boinc_fraction_done()` (`tests/boinc_stub/boinc_stub.cpp:52-56`, which overwrites
`stub_fraction_done`) to append, resume a work unit from a checkpoint, and require the first value to be start/total.

## Log

- 2026-10-27: recorded from the 2026-10-21 audit, verified at 45a41d5 by reading.
- 2026-10-27: re-checked after PAT-007 was corrected (only items longer than about a minute count): a CPU seed
  takes 24-50 min on events without a baseline, so still affected.
