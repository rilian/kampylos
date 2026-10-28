---
id: PAT-011
title: The GPU is chosen by a default or a command-line flag instead of the device the client assigned
status: active
severity: high
platforms: [cuda, opencl, metal]
area: device-selection
applies_to: GPU applications
check: 'gpu_opencl_dev_index|gpu_device_num|--device|device_id|boinc_get_opencl_ids'
origin: kampylos KAM-008
reference_fix: kampylos@b5f5ef2 (boinc/main_kampylos_opencl_boinc.cpp, boinc/main_kampylos_gpu_boinc.cpp)
added: 2026-10-27
---

## The defect

On a host with more than one GPU, the client assigns each task a device in `init_data.xml`. An application that uses
device 0, or the `--device` from the work unit's command line, runs every task on the same GPU: one card is
overloaded (and tasks slow down or time out), the others idle, and the client's bookkeeping is wrong. Reported on the
forum for kampylos on 2026-10-05.

## Why it happens

The assignment is in `APP_INIT_DATA`: `gpu_opencl_dev_index` for OpenCL, `gpu_device_num` for CUDA and generally
([boinc-api.md](../boinc-api.md#gpu-assignment-init_dataxml)). It is easy to miss because a single-GPU development
machine always gets device 0.

## How to check a repository

```
git grep -nE 'gpu_opencl_dev_index|gpu_device_num|--device|device_id|boinc_get_opencl_ids'
```

* `affected`: the device comes only from a default or the command line, or the command line overrides
  `init_data`.
* `not-affected`: `init_data` is read and wins whenever it carries a value (>= 0); the command line is only a fallback
  for standalone runs. Also check that the index is used on the same list BOINC counts (PAT-012).

Check that the `init_data` code is actually compiled into the shipped builds: compare its preprocessor guards
(`#ifdef`) with the `-D` flags of every build script. Metal: on Macs with more than one GPU (Intel Macs with a
discrete GPU, Mac Pro) the default device ignores BOINC's assignment; pick the device by `gpu_device_num` from
`MTLCopyAllDevices()`.
* `n/a`: no GPU build.

## How to fix

```
APP_INIT_DATA aid;
boinc_get_init_data(aid);
if (aid.gpu_opencl_dev_index >= 0) device = aid.gpu_opencl_dev_index;   /* OpenCL */
else if (aid.gpu_device_num >= 0)  device = aid.gpu_device_num;         /* CUDA, or an old client */
```

and log the device that was chosen, with its name, to stderr.

## How to test

Run against a stub `init_data` (or a stub BOINC library) with `gpu_opencl_dev_index = 1` and `--device 0` on the
command line: the log must show device 1.
