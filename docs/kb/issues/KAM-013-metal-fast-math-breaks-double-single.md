---
id: KAM-013
title: Metal compiles with fast math by default, which turns the double-single arithmetic back into float
type: gotcha
status: documented
severity: high
platforms: [macos, metal]
area: determinism
pattern:
aliases: []
reported: 2026-10-10
source: review 2026-10-10 (commit 53723b7)
where: src/kampylos_metal_fit.mm:576-590 @ 45a41d5
fixed_in:
---

## Symptom

With fast math on, the Metal app converges to the wrong solution: the true parameters of the synthetic planet
evaluated to chi2 = 1e18 on an M1 and the cell converged to 692 instead of 411 (comment at
`src/kampylos_metal_fit.mm:576-583`). The Metal binary shipped in early October 2026 was built that way
(kampylos-report-2026-10-16.html §3).

## Cause

Metal has no `double` (Apple GPUs have no FP64 units). The kernel computes in double-single ("ds", float-float)
arithmetic built on error-free transformations, `two_sum` and `two_prod` with `fma`
(`src/kampylos_metal_fit.mm:67-72`). Plain float fails the quintic solver by up to 460x
(`tests/test_kampylos_mag_f32.cpp`). Metal's compiler uses fast math unless told otherwise, and fast math may simplify
`lo = (a - (hi - bb)) + (b - bb)` and `lo = fma(a, b, -hi)` to 0, which silently makes ds arithmetic plain float.

## Fix

The workaround in place (53723b7, verified at 45a41d5): `metal_fit_select_device()` sets `fastMathEnabled = NO` (the
macOS 10-14 spelling) and `mathMode = MTLMathModeSafe` (macOS 15) (`:586-590`), and a one-thread self-test kernel
(`kampylos_ds_selftest`, `:469`, run at `:608-637`) refuses to run when either low part is wrong. The chi2 sums are
double-single too. Rules: never "simplify" `two_sum`/`two_prod`, never go back to float sums, keep the self-test, and
check the compile options when moving to a new macOS SDK.

## Test

`tests/test_metal_fit.mm` (self-test, Metal objective against the CPU objective point by point) and
`tests/test_ds_primitives.cpp`; the regression suite with a Metal `fit_event`.

## Log

- 2026-10-10: workaround in 53723b7.
- 2026-10-27: recorded as a gotcha, verified at 45a41d5 by reading.
