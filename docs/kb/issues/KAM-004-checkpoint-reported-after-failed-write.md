---
id: KAM-004
title: boinc_checkpoint_completed() is called after a failed checkpoint write
type: bug
status: confirmed
severity: low
platforms: [all]
area: checkpoint
next: Let write_checkpoint return bool; after a failed write call boinc_end_critical_section(), not boinc_checkpoint_completed(), and keep in-cell checkpoints
pattern: PAT-003
aliases: [K-B6]
reported: 2026-10-21
source: audit 2026-10-21 (plan §6.4, K-B6)
where: boinc/kampylos_boinc_common.h:221-229 @ 45a41d5
fixed_in:
---

## Symptom

stderr shows `warning: could not write the cell checkpoint after cell N` (or `... the seed checkpoint ...`,
`... the in-cell checkpoint`), and the client still records a checkpoint. No report yet.

## Cause

Verified by reading at 45a41d5. The helper itself is correct: `kampylos_write_file_atomic()`
(`boinc/kampylos_boinc_common.h:42-52`) writes a temporary file, checks `fwrite` and `fclose`, renames with
`boinc_rename` and returns false on any failure (it leaves the `.tmp` file behind). Its result is only used for a
warning:

* `kampylos_boinc_after_cell()` (`:221-229`) prints the warning, then deletes the seed and batched checkpoints
  (`:226-227`) and calls `boinc_checkpoint_completed()` (`:228`).
* The CPU seed hook (`:248-253`) and the GPU batched hook (`:265-270`) print the warning; their `write_checkpoint`
  returns `void`, so the drivers call `checkpoint_completed` whatever happened (`src/kampylos_cpu_cell.h:88-91`,
  `src/kampylos_batched_fit.h:207-210` and `:258-261`).

Consequences: the client records a checkpoint that does not exist. At a cell boundary it is worse: the in-cell
checkpoints are deleted although the cell checkpoint was not written, so the next start finds the cell checkpoint
one cell behind `out`, cuts the finished line (KAM-007) and redoes that cell from its first seed, because nothing is
left to resume from.

## Fix

Not written yet: let `write_checkpoint` return `bool`; on false call `boinc_end_critical_section()` instead of
`boinc_checkpoint_completed()` (the shape of PAT-001's fix) and do not delete the in-cell checkpoints. With the
resume rule of 7da5519 (trust the complete lines of `out`, KAM-007) a missing cell checkpoint costs nothing.

## Test

With the CPU wrapper on `tests/boinc_stub`: create a directory named `kampylos_checkpoint.tmp` in the work directory
so the temporary file cannot be opened, run a 2-cell work unit with `KAMPYLOS_STUB_CHECKPOINT_SECONDS=0`. The stub
appends a line to `stub_checkpoints` for every `boinc_checkpoint_completed()`; after the fix no line may be added for a
failed write, and the seed checkpoint must survive the end of the cell.

## Log

- 2026-10-27: recorded from the 2026-10-21 audit, verified at 45a41d5 by reading.
