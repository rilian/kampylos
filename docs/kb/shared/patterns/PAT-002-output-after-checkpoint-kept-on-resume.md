---
id: PAT-002
title: Output written after the last checkpoint is kept on resume, so rows are duplicated or torn
status: active
severity: high
platforms: [all]
area: checkpoint
applies_to: applications that append results to an output file item by item and resume from a checkpoint
check: 'fopen\(.*"ab?"|boinc_time_to_checkpoint|trim'
origin: phygas PHY-001
reference_fix: planodia@3f6c1b7 (src/main_planodia_boinc.cpp trim_output(): the output is cut back to the checkpoint)
added: 2026-10-27
---

## The defect

The application appends one result row per finished item and writes a checkpoint with the number of finished items
now and then. On restart it reads the checkpoint and reopens the output with `"a"`. Every row written after the last
checkpoint is still in the file, so those items are computed and appended a second time; a row cut in the middle of
an `fprintf` is followed by a full copy. The result has duplicate or broken lines: validation fails, or the
assimilator ingests the duplicates.

## Why it happens

Rows reach the disk between two checkpoints in two ways: the stdio buffer fills and is written, and on every client
quit the BOINC library calls `fflush(NULL)` before it exits, from the SIGALRM handler, so at any statement
([boinc-api.md](../boinc-api.md#exit-paths)). Quits are routine (client exit, reboot, "leave in memory" off, GPU
tasks on every suspend). A test that kills the process with SIGKILL does not flush and does not show the bug.

## How to check a repository

1. Find how the output is opened on resume: `git grep -nE 'fopen\(.*"ab?"'`.
2. Find where the start position comes from: a checkpoint file, or the output itself.
3. Look at the order in the main loop: is a row written (and possibly flushed) before the checkpoint that counts it?
   Is the output flushed before the checkpoint is written? If not, a crash or power cut (no library flush) leaves a
   checkpoint that counts rows the output does not have: rows are lost, the opposite of duplication.
4. Look at end markers (a `# done` line, a footer): are they written again when the resume finds the work complete?

* `affected`: the start comes from the checkpoint and the output is reopened for append without first being cut back
  to exactly what the checkpoint describes; or the start comes from the output's lines but a torn last line (no
  final `\n`) is kept; or the checkpoint can count rows that were not flushed yet; or an end marker can be written
  twice.
* `not-affected`: the output is cut back to the checkpoint's count (through a temporary file and `boinc_rename`), or
  the resume position is derived from the complete lines of the output and a torn tail is dropped.
* `n/a`: the output is written once at the end, or the application has no resume.

GPU paths that write a batch of rows and then checkpoint have the same problem; check every backend.

## How to fix

Flush the output before every checkpoint. Then either:

* (a) the checkpoint records how many complete lines (or bytes) of output it covers, and the resume cuts the output
  back to that length before appending (write the kept part to a temporary file, then `boinc_rename`); a checkpoint
  that claims more than the output holds means restart from 0. planodia `trim_output()` does this (lines counted from
  the checkpoint's item count, one line per item). kampylos at 45a41d5 resumes at the smaller of the checkpoint and
  the number of complete lines and cuts back to it.
* (b) the output is the authority: count its complete lines, drop a torn tail, resume after them, and treat the
  checkpoint as a hint. This only works when every item writes exactly one line, or when each line carries its item
  id. Output that writes lines only for some items (threshold output, candidate lists) needs (a). The proposed
  kampylos change in KAM-007 is an example of (b).

Both need items whose result does not depend on when they were computed (deterministic seeds per item), so a
recomputed row is identical. Write end markers only after checking that they are not there yet.

## How to test

A stub BOINC library whose quit does what the real one does (`fflush(NULL)`, then `exit`) at a random point, not
SIGKILL. Run a small work unit to the end, then again with repeated stub quits and restarts, and require the final
output to be byte-identical (`cmp`) to the uninterrupted run.
