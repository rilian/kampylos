---
id: PAT-007
title: Progress is not reported after a resume until the next item finishes
status: active
severity: low
platforms: [all]
area: boinc-lifecycle
applies_to: applications that resume from a checkpoint and call boinc_fraction_done()
origin: planodia PLA-005
reference_fix:
added: 2026-10-27
---

## The defect

After a restart the client shows 0 % and a full remaining time until the first new item finishes. With items that
take minutes or hours, volunteers conclude the task restarted from scratch and abort it, and the client's runtime
estimate is wrong meanwhile.

## Why it happens

`boinc_fraction_done()` is only called after an item finishes; nothing reports the resumed position.

## How to check a repository

`git grep -n boinc_fraction_done`, then look at the code between reading the checkpoint and the first item.

* `affected`: no `boinc_fraction_done(start / total)` before the first new item, and an item (or the time from the
  start to the first report) can take more than about a minute.
* `not-affected`: it is reported at resume, or the first report after a resume comes within about a minute anyway
  (give the measured or estimated item time in Evidence).
* `n/a`: no resume, or no progress reporting at all (then record that as an issue of its own if items are long).

## How to fix

Call `boinc_fraction_done((double)start / total)` right after the resume position is known.

## How to test

Stub BOINC library logging `boinc_fraction_done()`: after a resume, the first value must equal start / total.
