---
id: PAT-001
title: boinc_time_to_checkpoint() called more than once per checkpoint leaves a critical section open
status: active
severity: high
platforms: [all]
area: boinc-lifecycle
applies_to: code that calls boinc_time_to_checkpoint() or boinc_begin_critical_section()
check: 'boinc_time_to_checkpoint|boinc_checkpoint_completed|boinc_(begin|end)_critical_section'
origin: planodia PLA-001
reference_fix:
added: 2026-10-27
---

## The defect

Every call of `boinc_time_to_checkpoint()` that returns 1 enters a critical section, and
`boinc_checkpoint_completed()` leaves exactly one. Code that asks twice in one checkpoint opportunity (once in a
condition, once more to decide whether to report the checkpoint), or that returns 1 on a path that never reports,
leaves the counter above zero for the rest of the task. From then on suspend is deferred (the task keeps computing
while the BOINC manager shows it suspended), quit and abort are ignored, and the task does not end when the client
dies. Volunteers see a "suspended" task that still uses the CPU or GPU, and a client that cannot stop it.

## Why it happens

[boinc-api.md](../boinc-api.md#checkpoints-and-critical-sections): the "time to checkpoint" flag stays set until
`boinc_checkpoint_completed()`, so the second call returns 1 again and enters a second time; the counter only goes back
to zero when every entry is matched. Short-circuit conditions such as `if (n % 20 == 0 || boinc_time_to_checkpoint())`
make the number of calls depend on the path, so the bug shows only on some iterations.

## How to check a repository

```
git grep -nE 'boinc_time_to_checkpoint|boinc_checkpoint_completed|boinc_(begin|end)_critical_section'
```

For every call site, count the calls on each path through one checkpoint opportunity.

* `affected`: `boinc_time_to_checkpoint()` can be evaluated twice before the matching `boinc_checkpoint_completed()`
  (condition plus body, caller plus helper, two calls in one loop iteration); or a path after it returned 1 continues
  the computation without `boinc_checkpoint_completed()` / `boinc_end_critical_section()` (`continue`, `break`, an
  error branch that does not exit, an exception that is caught); or `boinc_begin_critical_section()` has a path
  without the matching end.
* `not-affected`: one call per opportunity, the result kept in a variable, every 1 paired on every path.
* `n/a`: the repository has no BOINC application code that checkpoints.

## How to fix

```
bool due = boinc_time_to_checkpoint();          // once per opportunity
if (due || own_reason_to_checkpoint) {
    bool ok = write_checkpoint(...);
    if (due) {
        if (ok) boinc_checkpoint_completed();
        else    boinc_end_critical_section();   // leave it, but do not report a checkpoint that was not written
    }
}
```

## How to test

A stub BOINC library (a few dozen lines, as in kampylos `tests/boinc_stub/`) whose `boinc_time_to_checkpoint()`
returns 1 at chosen calls and counts begin and end; it aborts when the counter is not 0 after
`boinc_checkpoint_completed()` or at `boinc_finish()`. Run the main loop with the timer firing on an iteration where
the application's own checkpoint cadence does not.
