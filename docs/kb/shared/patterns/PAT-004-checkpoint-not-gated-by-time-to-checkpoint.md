---
id: PAT-004
title: Checkpoints written on the application's own cadence, outside boinc_time_to_checkpoint()
status: active
severity: low
platforms: [all]
area: checkpoint
applies_to: code that writes a checkpoint or calls boinc_checkpoint_completed()
check: 'boinc_time_to_checkpoint|boinc_checkpoint_completed|checkpoint'
origin: phygas PHY-003
reference_fix:
added: 2026-10-27
---

## The defect

The application writes its checkpoint after every batch or every N items without asking
`boinc_time_to_checkpoint()`, and often calls `boinc_checkpoint_completed()` afterwards anyway. Three consequences:

* the write is not inside a critical section, so a quit can cut it at any instruction (harmless only when the write
  is atomic, see PAT-003);
* the volunteer's "request tasks to checkpoint at most every N seconds" preference is ignored, which matters when the
  checkpoint or an output rewrite is large and frequent (SSD wear, laptops waking the disk);
* `boinc_checkpoint_completed()` without a preceding 1 resets the client's checkpoint timer and CPU-time bookkeeping
  at the application's rhythm instead of the client's (the critical-section counter itself is clamped at 0).

## Why it happens

GPU paths often finish work in large batches and checkpoint after each one because "it is cheap"; final checkpoints
just before `boinc_finish()` are written unconditionally. See [boinc-api.md](../boinc-api.md#checkpoints-and-critical-sections).

## How to check a repository

```
git grep -nE 'boinc_time_to_checkpoint|boinc_checkpoint_completed|checkpoint'
```

For every place that writes the checkpoint, note whether it is gated by `boinc_time_to_checkpoint()` and how often it
runs, and how large the write is.

* `affected`: checkpoint writes (other than one final write before `boinc_finish()`) are not gated and either the write
  is not atomic, or it is large or frequent (more than about once a minute, or it rewrites output). Write the cadence
  and size in Evidence.
* `not-affected`: gated by `boinc_time_to_checkpoint()`; or a deliberate own cadence for a tiny atomic file, written
  inside `boinc_begin_critical_section()`/`boinc_end_critical_section()`, with the reason in Evidence.
* `n/a`: nothing is checkpointed.

## How to fix

Gate the write on `boinc_time_to_checkpoint()` (kept in a variable, PAT-001), keep an unconditional final checkpoint
only where the resume logic needs one, and call `boinc_checkpoint_completed()` only for a write that the timer asked
for. One checkpoint helper shared by the CPU and GPU paths keeps them from drifting apart.

Long-running monitors whose state changes with events (a measurement bin closes, a message is sent) should not gate
the save on the timer, or data is lost on a quit: keep the own cadence, write atomically inside
`boinc_begin_critical_section()`/`boinc_end_critical_section()`, and do not call `boinc_checkpoint_completed()` for
these writes.

## How to test

Stub BOINC library that logs every `boinc_time_to_checkpoint()` result and every checkpoint write; with the timer
never firing, only the final checkpoint may appear.
