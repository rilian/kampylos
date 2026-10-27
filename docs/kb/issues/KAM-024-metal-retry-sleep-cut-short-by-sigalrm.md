---
id: KAM-024
title: The Metal command-buffer retry waits (usleep) are cut short by the BOINC timer signal
type: bug
status: confirmed
severity: low
platforms: [macos, metal]
area: gpu-runtime
next: In metal_eval_batch(), sleep until a monotonic-clock deadline (loop on nanosleep with the time left) and drop the wait after the last attempt
pattern: PAT-010
aliases: []
reported: 2026-10-27
source: knowledge-base review 2026-10-27 (check of pattern PAT-010)
where: src/kampylos_metal_fit.mm:726 @ 45a41d5
fixed_in:
---

## Symptom

Expected, not observed: when another process keeps the GPU busy and command buffers are aborted ("Internal Error
(0000000e)"), the Metal task gives up and makes a temporary exit (300 s) sooner than the retry loop intends.

## Cause

Verified by reading at 45a41d5, not reproduced. After a failed command buffer `metal_eval_batch()` waits
`usleep(200000 * attempt)` and ignores the result (`src/kampylos_metal_fit.mm:700-729`): 0.2 s to 1.2 s, 4.2 s over
the six attempts (the last wait comes after the last attempt). In a BOINC process the API's timer sends SIGALRM about
every 0.1 s (`docs/kb/shared/boinc-api.md`, "Timer and signals"); `usleep` then returns early with EINTR, so the six
attempts can come within well under a second; the handler's `SA_RESTART` flag does not restart `usleep`
(`docs/kb/shared/boinc-api.md`). The stub and the tests send no SIGALRM, so they cannot show it. The library blocks
SIGALRM only in its own timer thread (`api/boinc_api.cpp:1499`, `:1779-1786` at BOINC 4d1529d), so when the GPU app
has completion-pool threads (a CPU budget of 2 or more) a tick can also land on an idle pool thread instead of the
main thread that sleeps here (KAM-027); with one completion thread there is no such thread.

## Fix

Not written yet: wait until a deadline measured with a monotonic clock (loop on `nanosleep` with the remaining time,
as yarrow 5e1164b does), and drop the wait after the last attempt.

## Test

PAT-010's test: a SIGALRM handler without `SA_RESTART` and a 100 ms `setitimer`, a test hook that makes command
buffers fail, and a check that the attempts are spread over the intended time.

## Log

- 2026-10-27: found while checking PAT-010; verified at 45a41d5 by reading.
- 2026-10-27: re-checked after PAT-010 was corrected (`SA_RESTART`) and against BOINC 4d1529d; verdict unchanged,
  signal delivery to pool threads noted (KAM-027).
