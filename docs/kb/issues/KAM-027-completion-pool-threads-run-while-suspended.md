---
id: KAM-027
title: The GPU apps' completion-pool threads keep running after the task is suspended, until their share of the current batch is done
type: bug
status: confirmed
severity: low
platforms: [cuda, opencl, metal]
area: boinc-lifecycle
next: In KampylosCpuPatchPool, block SIGALRM in worker_loop() and let run_share() wait between candidates while the task is suspended (callback)
pattern: PAT-025
aliases: []
reported: 2026-10-27
source: knowledge-base review 2026-10-27 (first-round report; origin of pattern PAT-025)
where: src/kampylos_cpu_patch_pool.h:123-191 @ 45a41d5
fixed_in:
---

## Symptom

Expected, not observed: a GPU task that the client suspends without removing it from memory keeps using CPU on
its completion-pool threads for up to one evaluation call (seconds; minutes in the cases below). CPU throttling
("use at most X % of CPU time") does not limit those threads. Only GPU apps whose CPU budget is 2 or more CPUs have
such threads.

## Cause

Verified by reading at 45a41d5 and the BOINC source at 4d1529d; not run under a real client.

* The pool has `n - 1` worker threads besides the calling thread (`src/kampylos_cpu_patch_pool.h:110-113`), where
  n is the client's CPU budget rounded down (`boinc/kampylos_boinc_common.h:282-287`). With the 0.1-0.25 CPUs that
  the GPU plan classes declared on 2026-10-19 (`boinc/README.md`, "CPU threads of the GPU apps") n is 1 and no
  thread is created, so today this needs a plan class with `avg_ncpus` of 2 or more. Outside BOINC the default is 4.
* The wrappers do not call `boinc_init_parallel()` (no `multi_thread` option in the four mains), so a suspend stops
  only the worker thread: on Windows `SuspendThread()` on the worker (`api/boinc_api.cpp:1163`), on Unix the worker
  sleeps in its SIGALRM handler at its next tick, within 0.1 s (`:1535-1540`). See `docs/kb/shared/boinc-api.md`,
  "Timer, signals and threads".
* `complete_batch()` hands out one evaluation call to all threads; the worker runs share 0 and then waits
  (`:143-152`), the pool threads run their shares (`:167-173`) and then block on a condition variable (`:181`). A
  pool thread cannot start new work without the worker, so after a suspend it runs at most until its share of the
  current call is done, on Windows and on Unix alike. One call takes up to about 15 s once a long round is split
  into chunks (`KAMPYLOS_BATCHED_CHUNK_SECONDS`), but the first call of a round, and every call after a restart (the
  chunk size is not checkpointed, `src/kampylos_batched_fit.h:82-83`, `:199-204`), covers the whole round: a few
  seconds normally, up to minutes on events whose candidates need the finite-source integration
  (kampylos-report-2026-10-19.html: rounds of 1.9-12 s on one such cell).
* When it matters: for user and scheduler preemption the client quits GPU tasks instead of suspending them
  (`client/app.cpp:179-186`), and the threads end with the process. GPU tasks are suspended in memory for CPU
  throttling (`client/app_control.cpp:1209-1213`; the throttler works in 1 s slots, `client/app.cpp:1317-1366`),
  benchmarks, non-BOINC CPU load and battery conditions (`client/app_control.cpp:1227-1246`).
* Unix only, from `api/boinc_api.cpp`: the library expects its SIGALRM handler to run only in the worker thread
  (comment at `:1507`) and blocks SIGALRM only in its own timer thread (`block_sigalrm()`, `:1499`, `:1779-1786`).
  The timer is `setitimer(ITIMER_REAL)` (`:1622`), a signal for the whole process. The pool threads are created by
  the worker and inherit its signal mask, so they do not block SIGALRM, and POSIX lets the kernel deliver such a
  signal to any thread that does not block it. When a tick lands on a pool thread, the handler runs there: during a
  suspend that pool thread sleeps in it (`:1535-1540`) and the worker goes on until a later tick reaches it. Only on
  Android does the library forward a tick that reached the wrong thread (`:1517-1521`). Not observed; which thread
  gets a tick depends on the operating system. This also decides whether the `usleep` of KAM-024 is cut short.

## Fix

Not written yet. Two small changes: block SIGALRM in the pool threads (`pthread_sigmask` at the start of
`worker_loop`, not on Windows); and let `run_share()` check `boinc_get_status()` (`suspended`) between candidates
and wait while it is set, so a suspend stops the pool within one candidate (the pool header has no BOINC dependency
today; pass a callback in). `boinc_init_parallel()` would suspend the whole process, but on Unix it forks the
application and was not checked against the GPU code.

## Test

On a real client with a plan class that gives the task 2 or more CPUs (or `KAMPYLOS_STUB_NCPUS=4` plus a stub that
simulates suspend): set "use at most 50 % of CPU time", or suspend through the benchmarks, during a long round, and
watch the per-thread CPU use (`top -H`, Process Explorer); it must drop to about zero within a second or two.

## Log

- 2026-10-27: recorded as the origin of PAT-025; verified at 45a41d5 and against BOINC 4d1529d by reading.
