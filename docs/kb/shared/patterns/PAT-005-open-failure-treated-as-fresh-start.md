---
id: PAT-005
title: Failure to open an existing output or checkpoint is treated as "no output yet", and finished work is truncated
status: active
severity: medium
platforms: [all]
area: checkpoint
applies_to: resume code that opens an existing output or checkpoint file
origin: kampylos KAM-001
reference_fix:
added: 2026-10-27
---

## The defect

On start the application opens its output (or checkpoint) for reading; if that fails it assumes a fresh start, sets
the start position to 0 and opens the output with `"w"`, which truncates it. When the open failed for another reason
than "the file does not exist" (on Windows a sharing violation from antivirus, an indexer or a backup tool; EACCES;
EMFILE; an I/O error) hours of finished work are thrown away and recomputed, or worse, the truncated file is later
appended to.

## Why it happens

`boinc_fopen(path, "r")` returns NULL both when the file is missing and when it cannot be opened (on Windows after
retrying for 5 s), see [boinc-api.md](../boinc-api.md#files). Code that only tests for NULL cannot tell them apart.

## How to check a repository

Find the resume code: `git grep -nE 'boinc_fopen\([^)]*"r|fopen\([^)]*"r'` near the checkpoint and output handling.
For each `if (!f)` on an output or checkpoint file:

* `affected`: any failure leads to a fresh start that opens the output with `"w"`.
* `not-affected`: only a missing file (`!boinc_file_exists(path)`) means a fresh start; any other failure ends with a
  temporary exit (or an error) and leaves the file alone.
* `n/a`: no resume.

A checkpoint whose content does not parse may reasonably mean "start from 0", as long as the output is then made
consistent with that (PAT-002).

## How to fix

```
if (!boinc_file_exists(path)) { start = 0; }                       /* really a fresh start */
else if (!(f = boinc_fopen(path, "rb"))) {
    boinc_temporary_exit(300, "cannot open the output file, will retry", false);
}
```

## How to test

On Unix: run until a checkpoint, `chmod 000` the output, restart: the output must not be truncated and the task must
ask to be retried. On Windows: hold the file open without sharing from another process.
