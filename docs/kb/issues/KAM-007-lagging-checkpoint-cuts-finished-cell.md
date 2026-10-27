---
id: KAM-007
title: A cell checkpoint that lags "out" makes the next start cut a finished cell and redo it, and the cut rewrites "out" in place
type: bug
status: fix-proposed
severity: low
platforms: [all]
area: checkpoint
next: Rebase 7da5519 (tag archive/fix/resume-trust-output, conflict only in a comment), replace via boinc_rename instead of remove+rename, review, merge
pattern: PAT-003
aliases: [fix/resume-trust-output]
reported: 2026-10-12
source: review 2026-10-12 (commit 7da5519), kampylos-report-2026-10-16.html §4.3
where: boinc/kampylos_boinc_common.h:189-203 @ 45a41d5
fixed_in:
---

## Symptom

After a restart, a cell whose line was already in `out` is computed again: stderr shows `Resuming from cell N/T` with
N one less than the cell lines `out` had. Separately, every resume rewrites `out` even when nothing needs cutting.
Matters for work units with more than one cell (a 10-cell row took 34 hours on one host, 2026-10-16 report).

## Cause

Verified by reading at 45a41d5 and reproduced on 2026-10-27 with the CPU wrapper built against `tests/boinc_stub` on
macOS.

1. `kampylos_run_cells()` writes and flushes a cell's line (`src/kampylos_wu.h:282-283`) before `after_cell` writes
   `kampylos_checkpoint` (`boinc/kampylos_boinc_common.h:224`; the critical section starts only at `:223`). A kill,
   crash or client quit in between, or a failed checkpoint write (KAM-004), leaves the checkpoint one cell behind
   `out`. The next start resumes from `min(checkpoint, complete lines)` (`:190`), cuts the finished line (`:200`) and
   redoes the cell. Usually the redo starts from the cell's last in-cell checkpoint (they are deleted only after the
   cell checkpoint, `:226-227`), so the loss is the work since that checkpoint: about one checkpoint period plus one
   seed (24-50 min per seed on events without a baseline, 2026-10-16 report) or one GPU chunk. After a failed write
   (KAM-004) the in-cell checkpoint is gone and the whole cell is redone. On the GPU apps the redone line can differ
   from the cut one at rounding level (KAM-012). The merged bf85fb2 (checkpoints inside a long round) did not change
   `kampylos_boinc_open_output()` or `after_cell`, so this is unchanged by it.
   Reproduction: `out` with its two header lines and 2 complete cell lines, `kampylos_checkpoint` = 1 ->
   `Resuming from cell 1/2`, the second line was cut and cell 2 started again.
2. `kampylos_trim_output()` (`src/kampylos_wu.h:329-337`) runs on every start with `start_cell > 0`, also when the
   file already has exactly that many lines, and rewrites `out` in place: `fopen(path, "wb")` truncates it (`:333`),
   `fwrite` and `fclose` are not checked (`:335-336`), and it returns true. A kill during the rewrite leaves an empty
   or partial `out` (next start: cell 0, or fewer cells); a full disk leaves fewer lines than `start_cell`, and the
   run then appends after them, so cells are missing from the middle of `out`. This is case (a) and (b) of PAT-003.
   Reproduction: `out` with 2 lines and `kampylos_checkpoint` = 2 -> same inode, new modification time, same content.
3. The top comment (`boinc/kampylos_boinc_common.h:24-27`) says the work unit "never truncates an output that still
   has good lines"; point 1 shows that it does.

## Fix

Commit 7da5519 (tag `archive/fix/resume-trust-output` of rilian/kampylos; the branch was closed on 2026-10-27, not
merged upstream): resume after the complete lines of `out` whatever the checkpoint says (the checkpoint is only
compared and logged); leave a file that needs no cut untouched; replace one that does through a temporary file with
checked writes and a rename. `tests/test_boinc_resume.sh` gains "checkpoint behind the output" and "torn last line"
scenarios and a numeric comparison (`same_output`) for rounding-level differences (KAM-021).

State on 2026-10-27: `git merge-tree` in a scratch clone shows that 7da5519 conflicts with 45a41d5 only in the top
comment of `src/kampylos_batched_fit.h` (bf85fb2 added a paragraph after the lines 7da5519 edits); the code changes
merge. One weakness to fix when it is rebased: on Windows it calls `remove(path)` before `rename()`, so a kill between
the two leaves no `out` at all. `boinc_rename()` (MoveFileEx with replace) avoids the gap, but `src/kampylos_wu.h`
does not include the BOINC headers: do the replace in `boinc/kampylos_boinc_common.h` or pass a rename function in.

For volunteers: no recomputed cells after such a kill, and no window in which a restart can empty `out`.

## Test

7da5519's `tests/test_boinc_resume.sh` scenarios "checkpoint behind the output" (no cell may be recomputed) and
"torn last line", and its `test_cpu_resume` checks "trimming an intact file leaves it unchanged". Today: the two
reproductions above.

## Log

- 2026-10-12: fix proposed in 7da5519.
- 2026-10-27: recorded from the 2026-10-21 audit, verified at 45a41d5 by reading and reproduced with the stub build;
  7da5519 checked for conflicts with 45a41d5.
