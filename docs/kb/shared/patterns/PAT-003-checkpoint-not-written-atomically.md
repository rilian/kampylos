---
id: PAT-003
title: Checkpoint or state file written in place or without checking the write, then reported anyway
status: active
severity: medium
platforms: [all]
area: checkpoint
applies_to: every write of a checkpoint, a state file, or a trimmed output file
check: 'checkpoint|_state|\.tmp|rename\(|boinc_rename|fclose|trim'
origin: phygas PHY-002
reference_fix: kampylos@3d32938 (boinc/kampylos_boinc_common.h: temporary file, checked close, boinc_rename)
added: 2026-10-27
---

## The defect

Any of these turns one unlucky kill, full disk or busy file into lost work or a wrong resume:

* (a) the file is rewritten in place: `fopen(final, "w")` truncates it, so a kill or quit before the new content is
  written leaves an empty or partial file;
* (b) the results of `fprintf`/`fwrite`/`fflush`/`fclose` are not checked, so a full disk renames a truncated
  temporary file over a good one;
* (c) `rename()` is used instead of `boinc_rename()`: on Windows it fails when the target exists, so the checkpoint
  silently never updates;
* (d) `boinc_checkpoint_completed()` is called although the write failed, so the client records a checkpoint that
  does not exist;
* (e) an error path goes on using a file that a helper already closed or freed (double `fclose`, use after free).

## Why it happens

[boinc-api.md](../boinc-api.md#files). The write is rarely interrupted on a developer machine, so these paths are never
exercised; volunteers' machines are killed, run out of disk, and run antivirus that holds files open.

## How to check a repository

```
git grep -nE 'checkpoint|_state|\.tmp|rename\(|boinc_rename|fclose|trim'
```

For every write of a checkpoint, state file or trimmed output, check (a) to (d).

* `affected`: any of (a) to (e). Say which in the Evidence column.
* `not-affected`: temporary file, every write and the close checked, `boinc_rename()` checked, report only on success.
* `n/a`: nothing is checkpointed.

## How to fix

One helper used for every such file:

```
static bool write_file_atomic(const char *path, const std::string &data) {
    std::string tmp = std::string(path) + ".tmp";
    FILE *f = boinc_fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = (fflush(f) == 0) && ok;
    ok = (fclose(f) == 0) && ok;
    return ok && boinc_rename(tmp.c_str(), path) == 0;
}
```

Report the checkpoint only when it returned true (see PAT-001 for the critical section when it did not).

## How to test

Make the write fail (a stub `fwrite`, `ln -s /dev/full` for the temporary file on Linux, or a read-only directory)
and check that the old file is unchanged and no checkpoint was reported. Kill the process in a loop while it writes
and check that the file always parses.
