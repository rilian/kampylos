---
id: PAT-009
title: A trickle-up message is lost: overwritten by the next send, or the process exits before the client takes it
status: active
severity: medium
platforms: [all]
area: trickle
applies_to: applications that call boinc_send_trickle_up()
origin: yarrow YAR-006
reference_fix:
added: 2026-10-27
---

## The defect

Measurements or reports sent as trickle messages never reach the server: no credit for that period, gaps in the
data. Two ways to lose one: a second `boinc_send_trickle_up()` before the client picked up the first, or an exit
right after the send.

## Why it happens

`boinc_send_trickle_up()` writes the fixed file `trickle_up.xml` (mode `"wb"`) and sets a flag. The library's timer
announces it to the client only on a one-second tick, and the client renames the file into the project directory when
it processes that message on its next poll. A second send in between overwrites the file. Once `boinc_finish()` has
started, the timer no longer announces anything, and `boinc_temporary_exit()` exits at once, so a send right before
either of them, or before `exit()`, is lost. See [boinc-api.md](../boinc-api.md#trickle-messages) for the source lines.

## How to check a repository

`git grep -n boinc_send_trickle_up` and list every path that leads to a send.

* `affected`: two sends can happen within a few seconds of each other (a start-up flush followed by an identity or
  calibration change, a final flush right after a periodic one); or the last send is followed by `exit()`/return
  (or `boinc_finish()`, `boinc_temporary_exit()`) without a wait of a few seconds; or the return value is ignored,
  so a failed send drops the data.
* `not-affected`: one sender with a minimum interval that also waits while `trickle_up.xml` still exists in the slot
  directory, data kept pending until a send succeeded, and some seconds of life after the last send.
* `n/a`: no trickle messages.

## How to fix

Route every send through one function that keeps the data pending (and does not send) while
`boinc_file_exists("trickle_up.xml")` or less than a few seconds after the previous send, and that checks the return
value. A `trickle_up.xml` that is still there long after the send (for example left by the previous run of the task)
must not block sending for good: after a timeout, send anyway. Wait at least 3 seconds after the last send before
`boinc_finish()`, `boinc_temporary_exit()` or `exit()`.

## How to test

Stub `boinc_send_trickle_up()` that records each message and fails when the previous one was not "picked up" yet (the
test picks them up on a timer): no message may be lost across start-up, identity change and exit.
