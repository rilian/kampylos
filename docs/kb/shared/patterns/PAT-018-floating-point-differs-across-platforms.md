---
id: PAT-018
title: Floating-point results differ across platforms (FMA contraction, libm, x87, GPU built-ins)
status: active
severity: medium
platforms: [aarch64, ppc64le, riscv64, i686, macos, windows, android, cuda, opencl, metal]
area: determinism
applies_to: applications whose results are compared between hosts (quorum, validator) or cut by thresholds
origin: phygas PHY-005
reference_fix:
added: 2026-10-27
---

## The defect

The same work unit gives results that differ in the last bits on different platforms. Where results pass through a
threshold (peak lists, candidate lists, "keep if above") a last-bit difference adds or drops whole lines. A validator
that compares bytes or uses too tight a tolerance then rejects valid results from some platforms, quorums between
platforms fail, and credit is lost.

## Why it happens

* **FMA contraction.** Compilers may fuse `a*b+c` into one fused multiply-add where the target has the instruction.
  GCC fuses across statements by default (`-ffp-contract=fast`) in C++ in every mode and in C in GNU modes; only C
  in an ISO mode (`-std=c99`, `-std=c11`) defaults to `off`. Upstream Clang 14 and newer default to `on` (within one
  expression); Apple clang 14 was measured on 2026-10-27 not to fuse at `-O2`, so vendor clang versions differ from
  upstream. aarch64, ppc64le and riscv64 always have FMA; x86_64 only with `-mfma` or a `-march` that includes it.
  Do not reason from version numbers: count the FMA instructions in the compiler's `-S` output (`fmadd`/`fmsub` on
  aarch64 and ppc64le, `vfmadd` on x86_64, `fmadd.d` on riscv64).
* **libm.** `pow`, `exp`, `log`, `sin`, `cos` differ between glibc, Apple's libm, the Windows CRT (msvcrt/UCRT),
  bionic, and the CUDA, OpenCL and Metal built-ins.
* **x87.** 32-bit x86 without `-msse2 -mfpmath=sse` keeps 80-bit intermediates.
* **Fast math** (`-ffast-math`, `-Ofast`, `/fp:fast`, Metal's default fast math) reorders operations.

## How to check a repository

`git grep -nE 'ffp-contract|ffast-math|Ofast|fp:fast|march|mfma|mfpmath|fastMathEnabled'` in the build scripts, list
the target architectures and GPU backends, and find the outputs that depend on thresholds. Whether it matters depends
on the validator, which is not in the public repositories: record that as a question for the maintainers.

* `affected`: platforms or backends that are mixed in a quorum produce results that differ (measured), or can
  (flags allow contraction on some targets and not on others) while the validator compares bytes.
* `not-affected`: measured identical across the shipped platforms, or the validator's tolerance is known to cover the
  differences.
* `n/a`: results are never compared across hosts.

## How to fix

Measure before changing anything: build the CLI on two architectures (for example macOS arm64 with clang, Linux
arm64 and amd64 with gcc in Docker), run the same input, diff. Add `-ffp-contract=off` (and SSE2 for i686) only if it
removes the differences; replace libm calls by own implementations only where a measured difference matters. Never
change the science result silently: record the change and its measured effect.

## How to test

A cross-platform diff script over a few small inputs, run whenever compiler flags or math code change.
