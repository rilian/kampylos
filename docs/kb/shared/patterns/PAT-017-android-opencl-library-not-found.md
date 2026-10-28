---
id: PAT-017
title: Android: the OpenCL library is not found because of linker namespaces
status: active
severity: high
platforms: [android, opencl]
area: device-selection
applies_to: Android builds that load the OpenCL library with dlopen (cl_dynload.c)
check: 'dlopen|android_dlopen_ext|libOpenCL'
origin: planodia PLA-007
reference_fix: planodia@3f6c1b7 (src/cl_dynload.c)
added: 2026-10-27
---

## The defect

On Android the GPU application finds no OpenCL although the GPU supports it: `dlopen("libOpenCL.so")` fails inside
the app process, so the task errors out or falls back. The same binary may work from `adb shell`, which runs in a
different linker namespace, so testing from a shell does not show it.

## Why it happens

Since Android 7, app processes load libraries through linker namespaces that do not include the vendor partition;
vendor OpenCL libraries live in `/vendor/lib64` (or `/vendor/lib` for 32-bit builds) under names that vary by GPU
vendor.

## How to check a repository

* Is there an Android GPU build (build scripts, `__ANDROID__` in the OpenCL loader)? If not: `n/a`.
* `git grep -nE 'dlopen|android_dlopen_ext|libOpenCL'` in the loader, and compare `src/cl_dynload.c` with planodia's
  (`kb.py status ../planodia` reports the drift of the copies registered in `kb.conf`).
* `affected`: Android OpenCL build with a loader that only tries a plain `dlopen`.
* `not-affected`: the loader has the namespace fallback of planodia 3f6c1b7 (or an equivalent).

## How to fix

Copy planodia's `src/cl_dynload.c` (3f6c1b7) and keep the copies in sync through the `shared-code` entries in
`kb.conf`.

## How to test

On an Android device with an Adreno or Mali GPU, under the BOINC client (not from a shell): the task's stderr must name
the OpenCL platform and device.
