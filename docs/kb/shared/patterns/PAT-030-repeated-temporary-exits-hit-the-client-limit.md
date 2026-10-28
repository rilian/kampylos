---
id: PAT-030
title: Repeated temporary exits without a checkpoint the client sees make the client abort the task after 100
status: active
severity: medium
platforms: [all]
area: boinc-lifecycle
applies_to: applications that call boinc_temporary_exit() (or exit without boinc_finish) as a normal way of waiting
check: 'boinc_temporary_exit|exit\(0\)'
origin: yarrow YAR-030
reference_fix:
added: 2026-10-27
---

## The defect

The application waits for something by exiting and asking to be restarted later: no detector plugged in yet, no
preferences, another instance running, a GPU that is busy. Each restart finds the same situation and exits again.
After more than 100 such exits the client aborts the task as an error ("too many boinc_temporary_exit()s"). For a
long-running monitor without its device that happens after about 2.5 days at a 30-minute delay; for a GPU retry at
5 minutes after about 8 hours. The volunteer sees an error and the host's error count goes up, for a situation that
is normal.

## Why it happens

The client counts premature exits per task and resets the count only when it sees a new checkpoint from the running
task, or when the client restarts ([boinc-api.md](../boinc-api.md#exit-paths)). An application that saves state and
calls `boinc_temporary_exit()` straight away never lets the client see that checkpoint: the status message that
carries it is sent on a one-second tick, and `boinc_temporary_exit()` exits at once.

## How to check a repository

`git grep -nE 'boinc_temporary_exit|exit\(0\)'` and, for each call, ask: can the task come back to the same call on
every restart in a normal situation (not a fault), and how long until 100 exits at the delay used? Does the client see
a new checkpoint before each exit (`boinc_checkpoint_completed()` after real progress, then more than a second of
life)?

* `affected`: a normal waiting state is implemented with repeated temporary exits and the client sees no checkpoint
  in between; give the time to 100 exits in Evidence.
* `not-affected`: temporary exits only for faults, where giving up after 100 (state the time) is acceptable or
  intended; or each exit follows real progress that the client has seen as a checkpoint.
* `n/a`: no `boinc_temporary_exit()` and no deliberate exit without `boinc_finish()`.

## How to fix

For a normal waiting state: report a checkpoint (`boinc_checkpoint_completed()`) and stay alive for at least 2 to 3
seconds before `boinc_temporary_exit()`, so the client sees it and resets the count; or wait inside the task (sleeping
in small steps with the clock, PAT-010) instead of exiting. For faults: keep the client's limit or an own lower limit
(PAT-013), and say in stderr why the task gives up.

## How to test

With a stub BOINC library, count the temporary exits in a no-device loop and check that a checkpoint is reported and
the process lives on for a few seconds before each one. With a real client: a test build with a short delay (say
10 s) left without its device must still be running after more than 100 waits (about 20 minutes).
