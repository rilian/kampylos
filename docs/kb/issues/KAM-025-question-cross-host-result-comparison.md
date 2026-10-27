---
id: KAM-025
title: Are kampylos results compared across hosts, platforms or backends, and with what tolerance?
type: question
status: new
severity: low
platforms: [all]
area: determinism
next: Decide (maintainers) whether any validator compares two results of a work unit across platforms or backends, and with what tolerance
pattern: PAT-018
aliases: []
reported: 2026-10-27
source: knowledge-base review 2026-10-27 (check of pattern PAT-018)
where: src/kampylos_wu.h:17-23 @ 45a41d5
fixed_in:
---

## Symptom

A question for the maintainers. Kampylos results differ between hosts at rounding level, and the answer decides
whether that matters.

## Cause

Known differences (not re-measured on 2026-10-27):

* between backends, by design: the Metal kernel uses float-float arithmetic and the GPU paths sum in another order;
  the final values are recomputed on the host, but the fit can end at a slightly different point;
* between toolchains: the shipped macOS CPU binary and a local build of the same source differ in the last printed
  digits (kampylos-report-2026-10-16.html §2.2);
* between completion thread counts and after resumes (KAM-012).

No build line sets `-ffp-contract` (`git grep` for `ffp-contract`, `ffast-math`, `march` finds nothing). Counted on
2026-10-27 on macOS arm64 in the `-O2 -std=c++17 -S` output (FMA instructions `fmadd`, `fmsub`, `fnmadd`, `fnmsub`):
GCC 13 emits 67 in `src/fit_event.cpp` (the Kampylos headers) and 3414 in `VBMicrolensingLibrary.cpp`, Apple clang 14
emits none in either. So a GCC build for aarch64 (Linux arm64, and the other FMA targets) does not compute the same
operations as the macOS build; the effect on the result lines was not measured.
The result line contains threshold-derived integers (`n_pts50`, `n_nights50`, `n_nights5pct`) that a last-bit
difference can change. The code comments say that the parts of a seed-split work unit and re-issued work units are
merged by keeping the lowest chi2 per cell (`src/kampylos_wu.h:17-23`), which suggests no byte comparison, but the
validator (`kampylos_validate.sh`) is not in this repository.

## Fix

Questions: does any validator compare two results of the same work unit (quorum above 1), and are results of
different platforms or of CPU and GPU apps mixed in such a comparison? If yes, what does it compare, bytes or numbers
with a tolerance? The answer decides whether KAM-023 (CRLF), KAM-012 and FMA contraction matter for credit.

## Test

None until answered. If a comparison exists: a cross-platform diff of `fit_event` on the synthetic planet work unit.

## Log

- 2026-10-27: recorded while checking PAT-018.
- 2026-10-27: re-checked after PAT-018 was corrected: replaced a wrong statement about compiler defaults by FMA
  counts of GCC 13 and Apple clang 14.
