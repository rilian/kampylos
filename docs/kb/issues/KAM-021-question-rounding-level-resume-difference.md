---
id: KAM-021
title: Is a rounding-level difference after a GPU resume acceptable, and should the resume test compare numerically?
type: question
status: new
severity: low
platforms: [cuda, opencl, metal]
area: determinism
next: Decide whether a rounding-level difference after a GPU resume with several completion threads is acceptable, and the resume test may compare numerically
pattern:
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan §8, question 6)
where: tests/test_boinc_resume.sh:80 @ 45a41d5
fixed_in:
---

## Symptom

A question for the maintainers, not a defect. A GPU cell resumed with more than one completion thread can differ from
an uninterrupted run at rounding level (KAM-012: rho 2.51e-5 against 2.43e-5, chi2 equal to four decimals).
`tests/test_boinc_resume.sh` requires a byte-identical `out` (`:80`, `:88`), so its Metal run fails in that
configuration.

## Cause

See KAM-012.

## Fix

Questions:

1. Is a rounding-level difference between a resumed and an uninterrupted GPU cell (more than one completion thread)
   acceptable for the science and for the server's checks?
2. Should `tests/test_boinc_resume.sh` then compare numerically in that case, as 7da5519 proposed (`same_output`:
   chi2 and the dchi2 columns within 1e-3, other fields within 1 %, integers equal), and keep the byte comparison for
   the CPU app and for one completion thread?

The same plan question asked about the merge order of seven branches. Four were merged into 45a41d5 (66a1ab3,
bf85fb2, 2a375ba, 34168e1); the other three were closed on 2026-10-27 without merging and are tracked here:
`docs/agents-md` (now `AGENTS.md`), `fix/resume-trust-output` (KAM-007), `fix/opencl-work-group-size` (KAM-006).

## Test

None until answered.

## Log

- 2026-10-27: recorded from the 2026-10-21 plan, §8 question 6.
