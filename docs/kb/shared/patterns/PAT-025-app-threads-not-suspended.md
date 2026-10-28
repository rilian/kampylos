---
id: PAT-025
title: Threads the application starts keep running while the task is suspended
status: active
severity: low
platforms: [all]
area: boinc-lifecycle
applies_to: applications that start their own threads (thread pools, OpenMP, std::thread, async I/O threads)
check: 'std::thread|pthread_create|#pragma omp|std::async|CreateThread|_beginthread|boinc_init_parallel|multi_thread'
origin: kampylos KAM-027
reference_fix:
added: 2026-10-27
---

## The defect

The volunteer (or the client, because the computer is in use or on battery) suspends the task, but some CPU use goes
on: threads the application started are not stopped, only the worker thread is. With a busy thread pool the task
keeps a core or more busy until the current batch ends, or indefinitely if the threads do not depend on the worker.

## Why it happens

Without `boinc_init_parallel()`, the library suspends only the worker thread (Windows: `SuspendThread` on that one
thread; Unix: the worker sleeps inside its signal handler). See
[boinc-api.md](../boinc-api.md#timer-signals-and-threads).

## How to check a repository

```
git grep -nE 'std::thread|pthread_create|#pragma omp|std::async|CreateThread|_beginthread|boinc_init_parallel|multi_thread'
```

* `affected`: the BOINC application starts threads that do work on their own, and does not use
  `boinc_init_parallel()`. Say in Evidence how long they can keep running after the worker stops (one batch, or
  without limit).
* `not-affected`: threads only run while the worker waits for them and each batch is short (say how short), or
  `boinc_init_parallel()` is used.
* `n/a`: no threads in the BOINC application.

## How to fix

Use `boinc_init_parallel()` (process-level suspend; check that it fits the GPU and checkpoint code), or make worker
threads check `boinc_get_status()` (`suspended`) between small pieces of work and wait while it is set.

## How to test

Suspend the task in the BOINC manager during a busy phase and watch the process's CPU use (`top -H`, Process
Explorer): it must drop to about zero within a second or two.
