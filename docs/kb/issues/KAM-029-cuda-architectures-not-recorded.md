---
id: KAM-029
title: No CUDA architecture list or minimum compute capability is recorded; the only gencode in the repository is sm_120
type: bug
status: new
severity: medium
platforms: [cuda, windows, linux]
area: build
next: Run cuobjdump --list-elf --list-ptx on each shipped CUDA binary and compare with the plan class's minimum compute capability
pattern: PAT-024
aliases: []
reported: 2026-10-27
source: knowledge-base review 2026-10-27 (check of pattern PAT-024)
where: tests/test_gpu_fit_binary.cu:9-13 @ 45a41d5
fixed_in:
---

## Symptom

Possible, not reported: CUDA tasks fail at the first launch on some hosts with "no kernel image is available for
execution on the device", if the shipped binary lacks the architecture of a GPU the plan class sends it to. Still to
confirm: the architecture lists of the shipped CUDA binaries (`cuobjdump --list-elf --list-ptx`) against the plan
class; neither is available here.

## Cause

Verified by reading at 45a41d5:

* This repository has no build line for the CUDA BOINC app. The only `-gencode` lines are in test build comments,
  `-gencode arch=compute_120,code=sm_120` (`tests/test_gpu_fit_binary.cu:10`, `tests/test_kampylos_gpu_kernel.cu:16`):
  machine code for compute capability 12.0 only and no PTX, so a binary built that way runs on no older GPU.
* A test comment points at a CUDA 13.3 toolkit (`tests/test_kampylos_opencl.cpp:14`). According to NVIDIA's CUDA 13.0
  release notes (not checked here) CUDA 13 toolkits no longer compile for compute capability below 7.5 (Maxwell,
  Pascal, Volta), GPUs that volunteers still use.
* Nothing records the minimum compute capability of the CUDA app, and the CUDA wrapper prints neither the device name
  nor its compute capability (`boinc/main_kampylos_gpu_boinc.cpp:92-97`; the OpenCL wrapper prints the device name).

## Fix

Maintainers: write the CUDA release build line (toolkit version, `-gencode` list with PTX for the newest
architecture) into `boinc/README.md`, state the minimum compute capability there and in the plan class, and print
the device name and compute capability to stderr at start (`cudaGetDeviceProperties`).

## Test

`cuobjdump --list-elf --list-ptx` on each shipped CUDA binary, compared with the plan class's minimum compute
capability.

## Log

- 2026-10-27: recorded while checking PAT-024; the repository side verified at 45a41d5, the shipped binaries not.
