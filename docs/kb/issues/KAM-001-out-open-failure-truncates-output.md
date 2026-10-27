---
id: KAM-001
title: A failure to open "out" for reading is taken as "no output yet", and "out" is truncated
type: bug
status: confirmed
severity: medium
platforms: [all]
area: checkpoint
next: In kampylos_boinc_open_output(), start fresh only when out does not exist (boinc_file_exists); any other read failure -> boinc_temporary_exit()
pattern: PAT-005
aliases: [K-B2]
reported: 2026-10-21
source: audit 2026-10-21 (plan §1 and §6.4, K-B2)
where: src/kampylos_wu.h:295-297 @ 45a41d5
fixed_in:
---

## Symptom

A task that had finished cells starts again at cell 0 and its finished lines in `out` are gone. stderr shows
`checkpoint says N cells done but there is no usable output file -- restarting from cell 0` and
`Resuming from cell 0/T` (without a checkpoint file only the second line). No volunteer report yet; the likely
trigger is Windows, where antivirus, indexers or backup tools hold files open.

## Cause

* `src/kampylos_wu.h:295-297`: `kampylos_split_output()` opens `out` with plain `fopen(path, "rb")` (not
  `boinc_fopen`, so there is no retry on a Windows sharing violation) and returns -1 when the open fails. -1 is also
  the value for "no usable file" (missing, or no `# grid=` header, `:303`). `fread` errors are not checked (`:301`).
* `boinc/kampylos_boinc_common.h:187-205`: with `have == -1` the start cell stays 0 (`:188`, `:197-199`) and
  `boinc_fopen(out_path, "w")` at `:205` truncates the file and writes a new header.

So any failure to read an existing `out` (sharing violation, EACCES, EMFILE, an I/O error) throws away every finished
cell. Verified by reading at 45a41d5, and reproduced on 2026-10-27 with the CPU wrapper built against
`tests/boinc_stub` on macOS: a work directory with an `out` of two header lines and 2 cell lines,
`kampylos_checkpoint` = 2, and `chmod 200 out` (write-only). The run printed the two lines above, and `out` then held
only the new header.

## Fix

Proposed in the 2026-10-21 plan (K-B2), not written yet: only a missing file means a fresh start
(`!boinc_file_exists(path)`); when the file exists but cannot be read, leave it alone and call
`boinc_temporary_exit()` (with the retry limit of PAT-013, so a file that never becomes readable does not loop
forever). Read it through `boinc_fopen` so Windows retries a sharing violation for 5 s. A file that exists but has no
`# grid=` header (killed during the very first header write) may still mean cell 0. For volunteers: no change except
that such a start no longer loses finished cells.

## Test

No test covers it. Reproduction: build the CPU wrapper on the stub (build line in `tests/test_boinc_resume.sh`), put
the `in` and `photometry` of that script, an `out` with header and 2 lines and `kampylos_checkpoint` = 2 in a work
directory, `chmod 200 out`, run. Today `out` is truncated; after the fix it must be unchanged and the stub must log
`boinc_temporary_exit`.

## Log

- 2026-10-27: recorded from the 2026-10-21 audit, verified at 45a41d5 by reading and reproduced with the stub build.
