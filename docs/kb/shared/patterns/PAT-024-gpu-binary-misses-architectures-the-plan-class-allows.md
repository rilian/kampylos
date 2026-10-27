---
id: PAT-024
title: GPU binaries are compiled for fewer GPU architectures than the plan class sends them to
status: active
severity: medium
platforms: [cuda, windows, linux]
area: build
applies_to: CUDA (and other ahead-of-time compiled GPU) builds
origin: nostos NOS-021
reference_fix:
added: 2026-10-27
---

## The defect

The CUDA toolkit decides which GPU architectures a binary contains (`-gencode` / `-arch` lists). Newer toolkits drop
old architectures, and different build machines use different toolkits (for example CUDA 13 on Windows and CUDA 12 on
Linux). If the server's plan class still sends the app to GPUs the binary was not compiled for, every task on those
hosts fails at the first launch ("no kernel image is available for execution on the device"), on one OS only.

## Why it happens

The toolkit version and the gencode list live in build scripts; the plan class lives on the server; nobody compares
them when a toolkit is upgraded.

## How to check a repository

`git grep -nE 'gencode|-arch[ =]|sm_[0-9]+|compute_[0-9]+|nvcc|cuda:[0-9]|CUDA_PATH|v1[0-9]\.[0-9]'` in the build
scripts. List per OS: toolkit version, architectures compiled (and whether PTX is embedded for JIT on newer GPUs).

* `affected`: the architecture lists differ between the shipped builds or exclude GPUs the project still serves, and
  nothing records the matching plan-class limit. Usually also a question for the maintainers (the plan class is
  server-side).
* `not-affected`: the same architectures on every OS, including PTX for forward compatibility, and the minimum
  compute capability is written down where the plan class is configured.
* `n/a`: no CUDA build.

## How to fix

Write the minimum compute capability of each build into the build script (and the app's stderr), keep the lists
the same across OSes where the toolkits allow, embed PTX for the newest architecture, and set the plan class to
match.

## How to test

`cuobjdump --list-elf --list-ptx <binary>` on each shipped binary, compared with the plan class.
