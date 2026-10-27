---
id: KAM-003
title: Write errors on "out" are never checked before boinc_finish(0)
type: bug
status: confirmed
severity: low
platforms: [all]
area: io
next: Check fflush/ferror after each cell line and fflush/ferror/fclose before boinc_finish(0); on failure call boinc_temporary_exit()
pattern: PAT-006
aliases: [K-B9]
reported: 2026-10-21
source: audit 2026-10-21 (plan §6.4, K-B9)
where: boinc/kampylos_boinc_common.h:233-239 @ 45a41d5
fixed_in:
---

## Symptom

On a full disk or after an I/O error the task still ends with `boinc_finish(0)` and uploads a result file with
missing or cut lines. No report yet.

## Cause

Verified by reading at 45a41d5: the header write (`boinc/kampylos_boinc_common.h:210-213`), each cell line
(`fprintf` in `src/kampylos_wu.h:249-253`) and the `fflush` after it (`src/kampylos_wu.h:283`) are not checked;
`kampylos_boinc_run()` calls `fclose(job.fout)` without looking at the result (`boinc/kampylos_boinc_common.h:236`)
and then `boinc_finish(0)` (`:238`). Nothing calls `ferror()`. Also, because the line is not checked before
`after_cell` writes the cell checkpoint, a checkpoint can count a cell whose line never reached the disk (the next
start then resumes from the lines that are there, see KAM-007).

What the server does with an incomplete grid is not known here (README.md says each grid is screened for complete
coverage).

## Fix

Not written yet: check `fflush(fout) == 0 && !ferror(fout)` after each cell line before the cell checkpoint, and
`fflush`, `ferror` and `fclose` before `boinc_finish(0)`. On failure: `boinc_temporary_exit()` (the resume repairs
`out` from its complete lines) or `boinc_finish(1)` with a message.

## Test

The `/dev/full` recipe of PAT-006 does not work here: kampylos reads `out` at every start
(`kampylos_split_output`), and reading `/dev/full` never ends. Use a small file system instead (Linux: a tmpfs of a
few KB as the work directory; macOS: a small disk image) with the CPU wrapper on `tests/boinc_stub`: the run must
not end with `boinc_finish(0)`.

## Log

- 2026-10-27: recorded from the 2026-10-21 audit, verified at 45a41d5 by reading.
