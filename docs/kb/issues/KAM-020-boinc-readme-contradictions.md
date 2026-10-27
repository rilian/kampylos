---
id: KAM-020
title: boinc/README.md contradicts itself about the VBMicrolensing submodule and the checkpoint files
type: bug
status: confirmed
severity: low
platforms: [all]
area: docs
next: In boinc/README.md, drop 'pinned to upstream v5.6' and the GPU patch step, rejoin the table row, say three files, qualify 'bit-identically'
pattern:
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan §6.4, then lines 17 and 83)
where: boinc/README.md:22-28 @ 45a41d5
fixed_in:
---

## Symptom

A reader of `boinc/README.md` is told both that the submodule is plain upstream VBMicrolensing that must be patched
for the GPU apps, and that it already is the thread-safe fork.

## Cause

Checked at 45a41d5 (the line numbers of the 2026-10-21 plan, 17 and 83, have moved):

* `:22-23` says the sources are "pinned to upstream v5.6", and `:25-28` says to run `patch_vbm_thread_local.py` for
  the GPU apps or the pool falls back to one thread. `:110-116` says, correctly, that `vendor/VBMicrolensing` is the
  fork with `thread_local` statics and `VBM_THREAD_SAFE` (`.gitmodules` branch `thread-safe-statics`, submodule commit
  a56b7bf), which the patch script recognises as already patched.
* `:6-18`: the table row for `kampylos_boinc_common.h` (`:18`) is cut off from its table by the paragraph `:13-17`
  and renders as a stray line.
* `:90` says "Two files in the slot directory" above a table of three; the top comment of
  `boinc/kampylos_boinc_common.h` (`:14-18`) also names only two (no batched checkpoint).
* `:98-100`: "Inside a cell both the CPU and the GPU apps resume bit-identically" holds for one completion thread only
  (KAM-012).
* `boinc/kampylos_boinc_common.h:24-27`: "never truncates an output that still has good lines"; it cuts `out` back to
  the checkpoint when the checkpoint lags (KAM-007).
* The "Tests" section (`:102-108`) lists three of the tests; `AGENTS.md` has the full table.

## Fix

Edit the documents (the knowledge-base step changes no existing document): drop the "pinned to upstream" and
"also run the patch" sentences or restate them for plain upstream only, move the table row up, say three files, and
qualify "bit-identically".

## Test

Read the rendered `boinc/README.md`.

## Log

- 2026-10-27: recorded from the 2026-10-21 audit, re-checked at 45a41d5.
