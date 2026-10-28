---
id: PAT-010
title: Sleeps, waits and device reads cut short by the BOINC library's SIGALRM (EINTR)
status: active
severity: high
platforms: [unix]
area: boinc-lifecycle
applies_to: code running in a BOINC worker that sleeps, waits, or reads from devices, pipes or sockets
check: '(^|[^A-Za-z0-9_])(sleep|usleep|nanosleep|clock_nanosleep|poll|select|pselect|read|recv|waitpid) *\('
origin: yarrow YAR-003
reference_fix: yarrow@5e1164b
added: 2026-10-27
---

## The defect

On Linux, macOS and Android the BOINC library sends the worker thread a SIGALRM about ten times a second. Every
blocking call that is not restarted returns early: `sleep()` returns the seconds left, `nanosleep()`/`usleep()`/
`poll()`/`select()` fail with EINTR, a `read()` on a serial port or pipe returns short. Code that counts sleeps samples
several times too fast; code that treats a short read or EINTR as failure declares a device lost (yarrow: every
Radioactive@Home counter was refused on Linux; a 300 ms wait became 50 ms). The standalone build and unit tests have no
such signal and pass. The library installs its handler with `SA_RESTART`, so a `read()` that has not received anything
yet is restarted; sleeps, `poll()` and `select()` are not.

## Why it happens

[boinc-api.md](../boinc-api.md#timer-signals-and-threads). Reads from regular files are not interrupted in practice; slow
devices, pipes, sockets and all sleeps are.

## How to check a repository

```
git grep -nE '(^|[^A-Za-z0-9_])(sleep|usleep|nanosleep|clock_nanosleep|poll|select|pselect|read|recv|waitpid) *\('
```

Only code reached from the BOINC entry point counts (not the standalone CLI).

* `affected`: a call whose EINTR or short result is treated as an error or as "time has passed", or a timeout that
  counts loop iterations instead of reading a clock.
* `not-affected`: every such call retries with the remaining time (measured with `CLOCK_MONOTONIC` or the wall clock),
  and reads loop until the wanted count or a real timeout.
* `n/a`: the BOINC worker only computes and reads regular files.

## How to fix

Retry loops with the remaining time, and clocks instead of counted sleeps; yarrow commit 5e1164b is the reference.

## How to test

Install a SIGALRM handler without `SA_RESTART` and a 100 ms `setitimer(ITIMER_REAL, ...)`, then run the sleep or read
(on a pty or a pipe fed slowly): the call must still deliver the full time or byte count.
