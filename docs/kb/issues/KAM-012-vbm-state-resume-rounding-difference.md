---
id: KAM-012
title: VBMicrolensing's per-thread state makes a resumed GPU cell differ at rounding level with more than one completion thread
type: design-note
status: documented
severity: low
platforms: [cuda, opencl, metal]
area: determinism
pattern:
aliases: [B1]
reported: 2026-10-12
source: commit 7da5519 (2026-10-12), kampylos-report-2026-10-16.html §2.4, Metal review 2026-10-19 §4
where: src/kampylos_cpu_patch_pool.h:167-173 @ 45a41d5
fixed_in:
---

## Symptom

`KAMPYLOS_RESUME_BACKEND=metal sh tests/test_boinc_resume.sh` with 4 completion threads (2026-10-19, 18 kills, 15
inside a cell): the resumed cell matched the reference line except rho (2.51e-5 against 2.43e-5), chi2 identical to
four decimals, so the byte comparison failed. With one completion thread: 23 kills, most inside rounds, byte-identical.
7da5519 recorded rho differences of 0.03-0.2 % in three Metal runs on 2026-10-12. The CPU app (one thread) resumes
byte-identically.

## Cause

* VBMicrolensing keeps scratch state in function-local statics (`thread_local` in the fork: 242 in
  `VBMicrolensingLibrary.cpp`). `BinaryMag0` calls `cmplx_roots_gen(zr, coefs, 5, true, true)`
  (`vendor/VBMicrolensing/VBMicrolensing/lib/VBMicrolensingLibrary.cpp:1535`): the root solver starts from the previous
  call's roots, so a magnification can differ in its last bit depending on which call came before
  (2026-10-16 report §2.4).
* A fresh process starts every thread cold. Within one evaluation call candidate i goes to pool thread i % n
  (`src/kampylos_cpu_patch_pool.h:167-173`). After a resume inside a long round (bf85fb2), or when the adaptive chunk
  size changes (`src/kampylos_batched_fit.h:199-204`), the candidates a thread sees before a given one change, the
  warm start differs, and Nelder-Mead can take a slightly different path. With one thread the effect was too small to
  show in the tests above.
* The skiplist levels of the contour integration come from `std::minstd_rand` seeded by `std::random_device`
  (`VBMicrolensingLibrary.cpp:913`, `:1737`, `:2710`, `:4044`), so they differ per process. No effect on results was
  seen: repeated uninterrupted CPU runs are byte-identical (2026-10-16 report).
* Not measured, inferred: because the chunk size depends on timing, two uninterrupted runs with more than one thread
  and chunked rounds could also differ at rounding level.

Why it is kept: the result stays in the same chi2 valley; removing the state means changing the vendored library;
today's GPU plan classes declare 0.1-0.25 CPUs, which gives one completion thread (`boinc/README.md` "CPU threads of
the GPU apps").

## Fix

None planned. `boinc/README.md:98-100` claims both apps resume bit-identically, which holds for one thread only
(KAM-020). Whether the difference is acceptable, and whether the resume test should compare numerically, is
question KAM-021.

## Test

`KAMPYLOS_RESUME_BACKEND=metal KAMPYLOS_STUB_NCPUS=4 sh tests/test_boinc_resume.sh` on a Mac shows it (about 10 min).

## Log

- 2026-10-27: recorded from the 2026-10-21 audit (plan B1) and the 2026-10-16 and 2026-10-19 reports, code paths
  verified at 45a41d5 by reading; the measurements are from those reports, not repeated.
