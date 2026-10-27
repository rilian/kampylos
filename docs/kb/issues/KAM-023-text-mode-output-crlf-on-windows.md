---
id: KAM-023
title: The result file "out" and the checkpoint files are written in text mode, so the Windows build writes CRLF
type: bug
status: confirmed
severity: low
platforms: [windows]
area: io
next: Open out with wb/ab in kampylos_boinc_open_output() and the temporary file with wb in kampylos_write_file_atomic()
pattern: PAT-019
aliases: []
reported: 2026-10-27
source: knowledge-base review 2026-10-27 (check of pattern PAT-019)
where: boinc/kampylos_boinc_common.h:205 @ 45a41d5
fixed_in:
---

## Symptom

Expected, not observed here: the result file of a Windows task ends every line with `\r\n`, so its bytes differ from
the same result computed on Linux or macOS.

## Cause

Verified by reading at 45a41d5: `out` is opened with `boinc_fopen(out_path, "w")` or `"a"` (`:205`), and the checkpoint
helper writes its temporary file with `"w"` (`:46`); in the Windows C runtime (MSVC and MinGW) text mode turns `\n`
into `\r\n`. The checkpoint files alone would not matter (private, read back with `fscanf`/`sscanf`/`istringstream`);
`out` does, because it is the result file the server receives.

What still works: `kampylos_split_output()` reads `out` in binary mode and splits on `\n`, so CRLF lines are counted
and kept with their `\r`, and the trim writes them back unchanged (`src/kampylos_wu.h:295-337`); the checkpoint
parsers (`fscanf`, `sscanf`, `istringstream`) and the input parsers (`isspace` tokenizer, `load_photometry`) accept
`\r`. Not known here: whether the server's parser and validator accept CRLF (the regression suite strips `\r\n` when
it assembles results "the same as kampylos_assimilate.sh", `tests/regression/run_regression.py:52-59`), and whether
results are ever compared as bytes (KAM-025).

## Fix

Not written yet: open `out` with `"wb"`/`"ab"` and the checkpoint temporary file with `"wb"`, so every platform writes
the same bytes. Tasks that resume with an old CRLF `out` would then mix line ends; the parsers accept both.

## Test

Build the CPU app with MinGW, run a work unit under Wine or on Windows, `cmp` its `out` with the Linux one;
`grep -c $'\r' out` must be 0.

## Log

- 2026-10-27: found while checking PAT-019; verified at 45a41d5 by reading.
- 2026-10-27: re-checked after PAT-019 was corrected (private text files do not count): `out` is not private,
  verdict unchanged.
