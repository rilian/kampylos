---
id: KAM-017
title: Checkpoint and output files are never fsynced
type: design-note
status: documented
severity: low
platforms: [all]
area: checkpoint
pattern:
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan §6.4)
where: boinc/kampylos_boinc_common.h:42-52 @ 45a41d5
fixed_in:
---

## Symptom

None seen. After a power loss (not a process kill) a checkpoint renamed into place can be empty or old, and the last
lines of `out` can be missing, depending on the file system.

## Cause

`kampylos_write_file_atomic()` (`boinc/kampylos_boinc_common.h:42-52`) writes, closes and renames without `fsync` of
the file or the directory; `out` gets only `fflush` after each line (`src/kampylos_wu.h:283`).

## Fix

Kept as it is, because the resume does not depend on the files being durable (verified by reading at 45a41d5):
an unreadable cell checkpoint means "resume from the complete lines of `out`"
(`boinc/kampylos_boinc_common.h:189-196`), a checkpoint ahead of `out` is lowered to it, a seed or batched checkpoint
that does not parse makes the cell start over (`src/kampylos_cpu_cell.h:72`, `src/kampylos_batched_fit.h:300-309`),
and a torn last line of `out` is dropped (`src/kampylos_wu.h:308`). A power loss costs at most the work since the data
last reached the disk. An `fsync` after every seed or round would cost volunteers' disks more than it saves. Two
related open issues: the in-place cut of `out` (KAM-007) and the unchecked parser (KAM-018).

## Test

Not testable without cutting power; the kill/resume tests cover the parsing side.

## Log

- 2026-10-27: recorded as a design note from the 2026-10-21 audit, verified at 45a41d5 by reading.
