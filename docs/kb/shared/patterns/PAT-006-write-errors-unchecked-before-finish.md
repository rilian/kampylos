---
id: PAT-006
title: Write errors on the result file are not checked before boinc_finish(0)
status: active
severity: low
platforms: [all]
area: io
applies_to: applications that write a result file
origin: kampylos KAM-003
reference_fix:
added: 2026-10-27
---

## The defect

A full disk, a quota or an I/O error makes `fprintf`/`fwrite` fail silently. The application never looks at
`ferror()` or the result of `fclose()`, calls `boinc_finish(0)`, and a truncated result is uploaded as a success: a
validation error at best, a partial result accepted at worst.

## Why it happens

stdio buffers writes; the error shows up only in `ferror()`, `fflush()` or `fclose()`, which the success path rarely
checks.

## How to check a repository

`git grep -n 'boinc_finish(0)'` and read upwards to the last write of each output file.

* `affected`: no `fflush`/`ferror`/`fclose` result is checked before `boinc_finish(0)`.
* `not-affected`: they are checked and a failure does not end in `boinc_finish(0)`.
* `n/a`: no result file. A placeholder result that carries no data (the work is reported in another way, for example
  trickle messages) may be recorded as `not-affected` with that reason.

## How to fix

```
if (fflush(out) != 0 || ferror(out) || fclose(out) != 0) {
    fprintf(stderr, "write error on the output file: %s\n", strerror(errno));
    boinc_finish(1);       /* or boinc_temporary_exit() when the resume can repair the output (PAT-002) */
}
```

## How to test

On Linux: run a standalone task with its slot directory on a small tmpfs (`mount -t tmpfs -o size=64k`) or a small
loop-mounted disk image, so the disk fills during the run, and check that the run does not end with status 0. (A
symlink to `/dev/full` is simpler but hangs an application that reads its output back at start.)
