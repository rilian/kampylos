---
id: PAT-022
title: Linux binaries require a newer glibc than the hosts they are sent to
status: active
severity: high
platforms: [linux]
area: build
applies_to: Linux builds that link glibc dynamically (typically GPU builds; CPU builds that are not fully static)
check: 'ubuntu:|debian:|nvidia/cuda:|FROM '
origin: nostos NOS-005
reference_fix: phygas scripts/build_gpu_server.sh (Ubuntu 20.04 containers, glibc 2.31, checked at the end)
added: 2026-10-27
---

## The defect

A binary linked on a recent distribution needs the glibc symbol versions of that distribution (anything linked
against glibc 2.34 or newer needs `GLIBC_2.34` at least). On older hosts the application does not start
("version `GLIBC_2.34' not found") and every task errors out within a second on those hosts.

## Why it happens

The glibc a binary needs is set by the build machine, not by the code. Fully static builds avoid it; GPU builds
usually cannot be fully static.

## How to check a repository

Find the build container or host in the build scripts (`git grep -nE 'ubuntu:|debian:|nvidia/cuda:|FROM '`) and, for
built binaries, `objdump -T <binary> | grep -oE 'GLIBC_[0-9.]+' | sort -V | tail -1`. Compare with the oldest glibc the
project supports (the phygas GPU builds target 2.31: Debian 11, Ubuntu 20.04).

* `affected`: a shipped Linux binary needs a newer glibc than that target.
* `not-affected`: fully static, or built on the target (or older) and checked.
* `n/a`: no Linux build.

## How to fix

Build in the oldest supported container, link statically where possible, and make the build script fail when the
newest `GLIBC_` version in the binary is above the target.

## How to test

`objdump -T` check in the build script; run the binary in a container of the oldest supported distribution.
