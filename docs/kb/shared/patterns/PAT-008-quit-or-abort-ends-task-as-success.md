---
id: PAT-008
title: A quit or abort request ends the task through boinc_finish(0), so it is reported as finished
status: active
severity: medium
platforms: [all]
area: boinc-lifecycle
applies_to: applications that poll boinc_get_status() or handle quit and abort themselves (long-running monitors)
check: 'quit_request|abort_request|boinc_get_status|direct_process_action|no_heartbeat'
origin: yarrow YAR-004
reference_fix:
added: 2026-10-27
---

## The defect

The application polls `quit_request` / `abort_request` and, when one is set, leaves its main loop into the normal
end path: flush, write the output, `boinc_finish(0)`. A client quit (client exit, reboot, a suspend of a task that is
not left in memory) then turns a task that should resume later into a finished one, and an abort is reported as a
success. For a long-running monitor this ends the task early and with it the measurement series.

## Why it happens

With the default `direct_process_action`, the BOINC library ends the process itself on quit and abort when no
critical section is open ([boinc-api.md](../boinc-api.md#exit-paths)), so the polling branch looks dead and is never
tested. The application's own branch wins when it set `direct_process_action = false`, or when it holds a critical
section from the check until `boinc_finish()` has written `boinc_finish_called`. Without either, on Unix the library
usually exits during the 2 s sleep of `boinc_finish()`, before the success is recorded; the defect is then latent,
but one change to the critical sections makes it real.

## How to check a repository

```
git grep -nE 'quit_request|abort_request|boinc_get_status|direct_process_action|no_heartbeat'
```

* `affected`: a quit or abort leads to `boinc_finish(0)` (or to the success path that ends in it). Say in Evidence
  whether the path is reachable today (see above) or latent.
* `not-affected`: on quit the application saves its state and exits *without* `boinc_finish` (the client restarts it
  later); on abort it exits without reporting success; or it does not poll and leaves both to the library.
* `n/a`: no polling and the default `direct_process_action`.

## How to fix

On quit: save state (atomically, PAT-003) and `exit(0)` without `boinc_finish`, so the client keeps the task. On
abort: exit without `boinc_finish(0)`. Leave the normal end path to the application's own end condition.

## How to test

Stub BOINC library whose `boinc_get_status()` reports `quit_request` after a few iterations: the run must exit without
calling `boinc_finish`, and a second run must resume from the saved state.
