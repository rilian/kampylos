---
id: PAT-013
title: Transient GPU errors end the task as a computation error instead of a temporary exit
status: active
severity: medium
platforms: [cuda, opencl, metal]
area: gpu-runtime
applies_to: GPU applications
origin: kampylos KAM-002
reference_fix: kampylos@5a65505 (Metal: device unavailable -> boinc_temporary_exit)
added: 2026-10-27
---

## The defect

A launch failure, out-of-memory, a driver reset (Windows TDR), a device that is busy or missing after a driver update
or a wake from sleep: the application calls `boinc_finish(1)` or `exit(1)`. The task is reported as a computation
error: hours of GPU work and the credit are lost, the host's error count goes up (the server then limits its work),
and the work unit is resent.

## Why it happens

Error handling written for development treats every GPU error as fatal. `boinc_temporary_exit()` keeps the task and
its checkpoints and lets the client retry later ([boinc-api.md](../boinc-api.md#exit-paths)). The client gives up
only after more than 100 temporary exits, which at a delay of minutes means many hours of retries on a device that is
broken for good; a lower limit of the application's own ends such a task sooner.

## How to check a repository

```
git grep -nE 'boinc_finish\(1\)|exit\(1\)|exit\(EXIT_FAILURE\)|abort\(\)|fail\(' -- src boinc
```

Classify every GPU error exit after start-up: (1) no usable device or initialisation failed, (2) runtime error during
the computation, (3) bad input or a bug (deterministic).

* `affected`: (1) or (2) ends with `boinc_finish(non-zero)` or `exit(non-zero)`.
* `not-affected`: (1) and (2) end with `boinc_temporary_exit()` and a retry limit; only (3) is a computation error.
* `n/a`: no GPU build.

Whether a temporary exit is wanted at all is the maintainers' decision; record it as a question in the affected
repository until it is answered.

## How to fix

`boinc_temporary_exit(delay, reason, false)` for (1) and (2), with a counter in a small file in the slot directory that
is reset whenever the task makes progress; after about five temporary exits without progress, `boinc_finish(1)` with
a clear message. Make sure a checkpoint exists, or the retry starts from 0. kampylos Metal (5a65505) is the reference
for the exit itself.

## How to test

Stub that makes the Nth kernel launch fail: the run must end with a temporary exit, the checkpoint intact; repeated
failures without progress must end in `boinc_finish(1)` after the limit.
