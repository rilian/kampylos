# BOINC wrappers

The BOINC application front ends of Kampylos. The search itself lives in `../src`; these files only handle the
work-unit files, progress, checkpointing and result upload.

| file | backend |
|---|---|
| `main_kampylos_boinc.cpp` | CPU (all platforms); supports seed-split work units (`part i K` in the input line) |
| `main_kampylos_gpu_boinc.cpp` | CUDA |
| `main_kampylos_opencl_boinc.cpp` | OpenCL |
| `main_kampylos_metal_boinc.cpp` | Apple Metal |
| `kampylos_boinc_common.h` | shared input/output/checkpoint handling |

## Build (CPU, Linux example)

You need the BOINC client libraries (`libboinc_api`, `libboinc`) built for your target and the VBMicrolensing sources
(`git submodule update --init`, pinned to upstream v5.6).

For the GPU apps (CUDA, OpenCL, Metal) also run `python3 vendor/patch_vbm_thread_local.py` once after the submodule
checkout: it makes the library's function-local statics `thread_local`, which the completion step's worker threads
need (`src/kampylos_cpu_patch_pool.h`). Without the patch the library header lacks `VBM_THREAD_LOCAL_STATICS` and the
pool falls back to one thread automatically; `tests/test_vbm_threads.cpp` shows the difference.

```
VBM=vendor/VBMicrolensing/VBMicrolensing/lib
g++ -O2 -std=c++17 -I src -I $VBM -I <boinc>/api -I <boinc>/lib -c boinc/main_kampylos_boinc.cpp -o main.o
g++ -O2 -std=c++17 -I $VBM -c $VBM/VBMicrolensingLibrary.cpp -o vbm.o
g++ main.o vbm.o <boinc>/api/libboinc_api.a <boinc>/lib/libboinc.a -lpthread -o kampylos
```

Add `-DKAMPYLOS_VERSION='"12"'` (CPU; `KAMPYLOS_GPU_VERSION`, `KAMPYLOS_OPENCL_VERSION`, `KAMPYLOS_METAL_VERSION` for the
GPU apps) so the version the app prints to stderr is the `version_num` the server assigns to that binary.

Run it in a directory with the two input files the app expects, `in` (one line, see `src/kampylos_wu.h`) and
`photometry` (the event's light curve); it writes `out`. Outside a BOINC client it runs standalone.

## Build (Metal, macOS)

```
VBM=vendor/VBMicrolensing/VBMicrolensing/lib
clang++ -O2 -std=c++17 -fobjc-arc -I src -I $VBM -I <boinc>/api -I <boinc>/lib \
    boinc/main_kampylos_metal_boinc.cpp src/kampylos_metal_fit.mm $VBM/VBMicrolensingLibrary.cpp \
    <boinc>/api/libboinc_api.a <boinc>/lib/libboinc.a -framework Metal -framework Foundation -o kampylos_metal
```

`-fobjc-arc` is required: `kampylos_metal_fit.mm` relies on ARC to release Metal buffers it replaces. The kernel is
compiled at start-up with fast math off and a self-test refuses to run if the compiler broke the double-single
arithmetic; every command buffer is checked and re-submitted on failure (`src/kampylos_metal_fit.mm`). Validate a
build with `tests/test_metal_fit.mm` and `src/fit_event.cpp -DKAMPYLOS_CLI_METAL` under `tests/regression/run_regression.py`.

## Checkpoints and resume

Two files in the slot directory, both written atomically (temporary file + rename):

| file | contents |
|---|---|
| `kampylos_checkpoint` | number of grid cells completed and written to `out`; written after every cell |
| `kampylos_seed_checkpoint` | CPU app only: cell index, next seed, best fit so far (`src/kampylos_cpu_cell.h`); written whenever BOINC asks for a checkpoint, removed when the cell completes |
| `kampylos_batched_checkpoint` | GPU apps only: the batched Nelder-Mead driver's full state between two rounds (`src/kampylos_batched_fit.h`), a few hundred KB of text; same timing as the seed checkpoint |

On a restart the app resumes after the complete cell lines in `out` (a torn last line is cut off; the checkpoint
file is only cross-checked and logged): the result file is the authoritative record of finished cells, so a missing,
stale or not-yet-updated checkpoint file never costs a finished cell. Inside a cell both the CPU and the GPU apps
resume bit-identically (up to the last bits of VBMicrolensing's root-solver warm start, see `src/kampylos_batched_fit.h`),
and report progress after every seed / every round.

## Tests

- `tests/test_cpu_resume.cpp` -- the resumable seed loop and the result-file helpers (seconds).
- `tests/test_batched_resume.cpp` -- the GPU apps' driver, with a CPU evaluator: state round trip, kills at random
  rounds across all passes, bit-identical resume (a few minutes).
- `tests/test_boinc_resume.sh` -- builds the real CPU wrapper against the fake BOINC runtime in `tests/boinc_stub/`,
  kills it with SIGKILL at random moments and checks the resumed `out` is byte-identical to an uninterrupted run (minutes).

## VBMicrolensing

`vendor/VBMicrolensing` is a fork of valboz/VBMicrolensing v5.6 (`alplix/VBMicrolensing`, branch `thread-safe-statics`) in
which every function-local `static` is `thread_local` and `VBM_THREAD_SAFE` is defined. The GPU backends run the exact
finite-source completion on several CPU threads (`src/kampylos_cpu_patch_pool.h`); with the unpatched upstream library the pool
falls back to a single thread because upstream's statics are shared between threads. To build against plain upstream v5.6
instead, run `python3 vendor/patch_vbm_thread_local.py` (it recognises the fork as already patched).
