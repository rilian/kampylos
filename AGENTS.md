# Working on Kampylos (notes for AI agents and new contributors)

Kampylos is the binary-lens microlensing grid search of the bitboinc BOINC project. The same search exists for CPU,
CUDA, OpenCL and Apple Metal, and the four apps must stay compatible in input, output and resume behaviour. Read this
file before changing anything. The source comments are the detailed documentation and are long on purpose: keep
that style (explain why, date the change, name the forum thread or test that motivated it). Written 2026-10-27
against commit 45a41d5; it starts from an AGENTS.md of 2026-10-12 that was never merged.

## What one work unit is

Two input files. `in` is one line, `<mag|flux> s_min s_max n_s q_min q_max n_q [log10|ln] [part i K]`
(`src/kampylos_wu.h` is the only parser); `photometry` is the event's light curve, one `time value error` per line,
magnitudes or fluxes. The grid cells are row-major over (s, q). For each cell the app fits (t0, u0, ln tE, alpha,
ln rho) from the seed set of `kampylos_make_seeds()` (240 seeds on the synthetic planet), one Nelder-Mead pass per
seed plus up to two restarts, with VBMicrolensing as the magnification engine. The cell's final chi2, fs, fb and
diagnostics are recomputed on the host, and one 16-field line is appended to `out` after two header lines
(`# grid=...` and `# anchor format=2 backend=...`, see README.md). With `part i K` the task fits only the seeds
k with k % K == i; the server keeps the lowest chi2 per cell over the K parts. A cell takes minutes on an ordinary
event and days on an event whose single-lens anchor has no baseline (measurements in
`kampylos-report-2026-10-16.html`); one task is one cell or a short row of cells.

## Layout

| path | what |
|---|---|
| `src/kampylos_wu.h` | the work-unit contract: input parsing (grid marker, `part i K`), result header and line, `kampylos_finalize_cell()`, `kampylos_run_cells()`, result-file count and trim helpers |
| `src/lightcurve.h` | `DataPoint`, `load_photometry()` (sorts by time), `flux_fit_solve()`: the one (fs, fb) solve of every backend |
| `src/single_lens.h`, `src/pspl_prefit.h` | the single-lens anchor (PSPL, FSPL, FSPL+LD) every cell is compared with |
| `src/simplex.h`, `src/nelder_mead_stepper.h` | blocking Nelder-Mead (CPU) and its steppable twin (GPU batching, checkpointable state) |
| `src/binary_fit.h` | CPU objective and fitters, `kampylos_make_seeds()`, `kampylos_seed_part()` |
| `src/kampylos_cpu_cell.h` | CPU seed loop with the seed-level checkpoint (`KampylosCellHooks`) |
| `src/kampylos_batched_fit.h` | backend-independent batched driver of the GPU apps, in-cell checkpoint (state version 2), long rounds split into chunks |
| `src/kampylos_gpu_types.h`, `src/kampylos_gpu_complete.h`, `src/kampylos_cpu_patch_pool.h` | kernel I/O structs (`KAMPYLOS_MAX_BAD_POINTS` 512), host completion of each candidate (finite-source points; whole candidate on overflow), the persistent worker pool for that step |
| `src/kampylos_gpu_mag.cuh`, `src/kampylos_gpu_kernel.cu`, `src/gpu_fit_binary.{cu,h}` | CUDA |
| `src/kampylos_kernel.cl`, `src/embed_kampylos_cl.py`, `src/kampylos_cl_embedded.h`, `src/kampylos_opencl_fit.{cpp,h}` | OpenCL (the embedded header is generated) |
| `src/kampylos_metal_fit.{mm,h}` | Metal: MSL kernel as a string (double-single arithmetic), host side, self-test |
| `src/fit_event.cpp` | standalone CLI; backend chosen with `-DKAMPYLOS_CLI_{CUDA,OPENCL,METAL}`; ignores `part i K` (KAM-026) |
| `src/model_curve_cli.cpp` | model light curve for the web site's plots (natural-log s, q) |
| `boinc/` | the four BOINC mains, `kampylos_boinc_common.h` (input, output, checkpoints, CPU budget), `boinc/README.md` |
| `tests/` | standalone tests (each file's header comment has its build line), `tests/boinc_stub/` (fake BOINC runtime), `tests/regression/` (end-to-end suite, expected results, synthetic planet light curve) |
| `vendor/VBMicrolensing` | submodule, third-party: `alplix/VBMicrolensing` branch `thread-safe-statics` (v5.6 with `thread_local` statics, defines `VBM_THREAD_SAFE`); do not edit |
| `vendor/patch_vbm_thread_local.py` | makes plain upstream v5.6 equivalent to the fork, or `--strip`s the fork back to upstream (CPU build) |
| `kampylos-report-2026-10-16.html` | measurements: CPU build speed, shipped binaries, cost of no-baseline events |

## Invariants (break one and results become silently wrong)

- **Natural logs inside, grid units outside.** Every `log_s`/`log_q` passed to a fit function is a natural log.
  Work units since 2026-10-08 carry `log10`; `kampylos_grid_to_ln()` converts and the result line gets the grid value
  back. The mains never convert themselves.
- **One parser, one writer.** Input, header, result line and diagnostics live only in `src/kampylos_wu.h`; the
  server parses result files with one parser (not in this repository). Do not change the format without the server.
- **Same seeds, same answer on every backend.** All backends use `kampylos_make_seeds()`; the seed split is k % K == i
  in both `kampylos_cpu_cell.h` and `kampylos_seed_part()`. The final chi2/fs/fb of a cell is always recomputed on
  the host (`kampylos_finalize_cell`). Backends and thread counts still differ at rounding level (KAM-012).
- **Metal: fast math off, double-single arithmetic.** Metal has no `double`; fast math may delete the error-free
  transformations. `metal_fit_select_device()` turns it off and a self-test kernel refuses to run otherwise. Never
  "simplify" two_sum/two_prod or go back to float sums (KAM-013).
- **GPU calls are checked.** Every Metal command buffer's status is read and a failed one re-submitted; every
  OpenCL call goes through `CL_CHECK`; CUDA calls through `CUDA_CHECK_RT`. A result read after an unchecked launch is
  the previous round's.
- **Determinism.** Seeds, rounds and the completion step are deterministic for one backend: bad points are added in
  index order, and within one evaluation call candidate i goes to pool thread i % n. A resumed CPU cell is
  byte-identical to an uninterrupted one. New nondeterminism (thread order, unordered containers, uninitialised
  fields) breaks the resume tests.
- **VBMicrolensing is thread-safe only when patched.** `KampylosCpuPatchPool` uses more than one thread only if the
  library header defines `VBM_THREAD_SAFE` or `VBM_THREAD_LOCAL_STATICS`. In the GPU BOINC apps the thread count is
  the client's CPU budget (`init_data` ncpus, rounded down, at least 1; since 66a1ab3). The workers are created once and
  never exit; the pool is created with `new` and never deleted (MinGW, KAM-010). A suspend stops only the worker
  thread, so pool threads finish their share of the current call first (KAM-027).
- **Include order in the BOINC mains:** Kampylos headers (VBMicrolensing) before `boinc_api.h`/`filesys.h`, because
  windows.h's `scr2` macro collides with VBMicrolensing (KAM-014). Metal files need `-fobjc-arc`.
- **Bad-point capacity:** `KAMPYLOS_MAX_BAD_POINTS` (`kampylos_gpu_types.h`, CUDA and OpenCL) and
  `KAMPYLOS_MSL_MAX_BAD_POINTS(_HOST)` (`kampylos_metal_fit.mm`) are both 512 and must stay equal.
- **OpenCL source:** after editing `kampylos_kernel.cl` or `kampylos_gpu_types.h`, run
  `python3 src/embed_kampylos_cl.py`. At 45a41d5 the embedded copy differs from `kampylos_gpu_types.h` in one comment.
- **Checkpoint formats** (below) are read by binaries that resume tasks started by older versions: keep reading the
  old version (as v2 of the batched state still reads v1) or accept that such tasks redo the current cell.
- **Version strings** are build flags (`-DKAMPYLOS_VERSION='"12"'`, `KAMPYLOS_GPU_VERSION`, `KAMPYLOS_OPENCL_VERSION`,
  `KAMPYLOS_METAL_VERSION`) and must match the server's `version_num`; the defaults in the mains are fallbacks.

## Checkpoint/resume contract

| file | written | contents |
|---|---|---|
| `out` | one line per finished cell, `fflush` after each (`kampylos_run_cells`) | the authoritative record of finished cells |
| `kampylos_checkpoint` | after every cell, temp file + `boinc_rename`, in a critical section | number of cells done |
| `kampylos_seed_checkpoint` | CPU: after a seed when `boinc_time_to_checkpoint()` is 1 | cell, next seed, best fit so far (`%.17g`) |
| `kampylos_batched_checkpoint` | GPU: after a round, or between chunks of a long round, when `boinc_time_to_checkpoint()` is 1 | `kampylos_batched_fit 2` state, a few hundred KB of text |

- **Start** (`kampylos_boinc_open_output`): `have` = complete lines of `out` (a torn last line is dropped; -1 if the
  file cannot be read or does not start with `# grid=`); `checkpoint` = the integer or -1. The task resumes at
  `min(checkpoint, have)` (or `have` without a checkpoint) and cuts `out` back to that many lines. With `have` = -1 it
  starts at cell 0 and opens `out` with `"w"`.
- **Known gaps at 45a41d5:** an `out` that exists but cannot be opened is truncated (KAM-001); a checkpoint that lags
  `out` by one cell makes the task cut that finished cell and redo it, and the cut rewrites `out` in place on every
  resume (KAM-007; fix proposed in 7da5519, tag `archive/fix/resume-trust-output`, not merged); a failed checkpoint
  write is still reported (KAM-004); write errors on `out` are not checked (KAM-003); no fsync (KAM-017, by design:
  `out` is the authority); progress is not reported at resume start (KAM-019); text-mode files on Windows (KAM-023).
- **In-cell checkpoints** are used only for the cell they name (GPU: also the same seed count and restart count);
  otherwise the cell starts over. They are deleted right after the cell checkpoint write (also when it failed).
- **Critical sections:** each `boinc_time_to_checkpoint()` that returns 1 is followed by exactly one
  `boinc_checkpoint_completed()`; the cell checkpoint is written between `boinc_begin_critical_section()` and
  `boinc_checkpoint_completed()`. Keep one call per opportunity (shared pattern PAT-001).
- **Cadence:** the client's checkpoint period (60 s default) for in-cell state; inside a long GPU round the checkpoint
  waits for the end of a chunk (target `KAMPYLOS_BATCHED_CHUNK_SECONDS` = 15 s, measured per call).
- **What a resume must reproduce:** a byte-identical `out` for the CPU app (`tests/test_boinc_resume.sh`); for the GPU
  apps byte-identical with one completion thread, rounding-level differences with more (KAM-012, question KAM-021).

## Building

There is no build system: header-only C++17 plus one VBMicrolensing translation unit. Exact lines are in
`boinc/README.md` (CPU on Linux, Metal on macOS) and in each test's header comment. From the repository root:

```
git submodule update --init                      # vendor/VBMicrolensing, the thread-safe fork
VBM=vendor/VBMicrolensing/VBMicrolensing/lib
# CPU BOINC app (boinc/README.md): BOINC's api/ and lib/ headers and static libraries for the target
g++ -O2 -std=c++17 -I src -I $VBM -I <boinc>/api -I <boinc>/lib -c boinc/main_kampylos_boinc.cpp -o main.o
g++ -O2 -std=c++17 -I $VBM -c $VBM/VBMicrolensingLibrary.cpp -o vbm.o
g++ main.o vbm.o <boinc>/api/libboinc_api.a <boinc>/lib/libboinc.a -lpthread -o kampylos
# Metal BOINC app (boinc/README.md)
clang++ -O2 -std=c++17 -fobjc-arc -I src -I $VBM -I <boinc>/api -I <boinc>/lib \
    boinc/main_kampylos_metal_boinc.cpp src/kampylos_metal_fit.mm $VBM/VBMicrolensingLibrary.cpp \
    <boinc>/api/libboinc_api.a <boinc>/lib/libboinc.a -framework Metal -framework Foundation -framework CoreGraphics -o kampylos_metal
# any wrapper without BOINC: replace the BOINC paths by -I tests/boinc_stub and add tests/boinc_stub/boinc_stub.cpp
```

- Add the version flag of the app (Invariants). The faster CPU release build (`patch_vbm_thread_local.py --strip`, then
  jemalloc or mimalloc, byte-identical results) is in `boinc/README.md` "Faster CPU build"; restore the fork with
  `git -C vendor/VBMicrolensing checkout -- .` before building a GPU app.
- CUDA: no BOINC build line in this repository. `src/gpu_fit_binary.h` explains that the wrapper `.cpp` and the `.cu`
  file go to `nvcc` as separate inputs; `tests/test_gpu_fit_binary.cu` shows an `nvcc -gencode` line, for `sm_120`
  only. The architectures and the minimum compute capability of the shipped CUDA app are not recorded (KAM-029).
- OpenCL: needs `cl_dynload.{c,h}` from the parent repository (not here) and OpenCL headers; each vendor binary must be
  built with `-DEXPECTED_GPU_VENDOR="..."` (`src/kampylos_opencl_fit.cpp`). Without it the vendor check is silently off
  and the first platform with any device is used (KAM-028).
- Dependencies: VBMicrolensing is pinned by the submodule commit (a56b7bf). The BOINC libraries, the CUDA toolkit,
  OpenCL headers and `cl_dynload` come from the build machine or the parent repository; their versions are not
  recorded here.
- `VBMicrolensingLibrary.cpp` compiles with warnings; filter them (`2>&1 | grep -v VBMicrolensingLibrary`).
- Release builds, BOINC library builds and Linux containers are made outside this repository (see "Not in this
  repository").

## Tests and what to run for which change

| test | covers | runtime |
|---|---|---|
| `tests/test_cpu_resume.cpp` | seed checkpoint round trip, resumable CPU cell (also `part i K`), result-file count/trim helpers | ~1 min |
| `tests/test_batched_resume.cpp` | batched driver state round trip, kills at random rounds and inside rounds (chunks of 3), bit-identical resume, monotonic progress | minutes |
| `sh tests/test_boinc_resume.sh [workdir]` | real CPU wrapper on the stub, SIGKILLed repeatedly, cell checkpoint deleted once: `out` byte-identical; stale checkpoint (ahead of `out`); the "checkpoint behind" and "torn line" cases exist only in 7da5519. `KAMPYLOS_RESUME_BACKEND=metal` runs the Metal wrapper | ~10 min |
| `tests/test_metal_fit.mm [--cell]` | Metal objective vs CPU point by point (self-test, no Metal-only rejections); `--cell` fits a whole cell | seconds / minutes |
| `tests/test_vbm_threads.cpp` | VBMicrolensing from 4 threads: fails on unpatched upstream, passes on the fork | seconds |
| `tests/test_nelder_mead_stepper.cpp`, `test_ds_primitives.cpp`, `test_kampylos_mag_ds.cpp`, `test_kampylos_mag_f32.cpp`, `test_kampylos_gpu_mag.cpp` | stepper == blocking NM; double-single primitives and magnification; GPU fast-path test on the CPU | seconds |
| `tests/test_single_lens.cpp`, `test_pspl_recovery.cpp`, `test_binary_recovery.cpp`, `test_vbm_sanity.cpp`, `test_real_data_smoke.cpp` | single-lens anchor, fit recovery, library sanity, a real light curve | seconds to minutes |
| `tests/test_gpu_fit_binary.cu`, `test_kampylos_gpu_kernel.cu`, `test_kampylos_opencl.cpp`, `test_opencl_fit_binary.cpp` | CUDA and OpenCL against the CPU reference (need the GPU; OpenCL needs `cl_dynload.c` or an OpenCL import library) | minutes |
| `tests/regression/run_regression.py --cli cpu=PATH [--cli metal=PATH ...]` | synthetic planet WU lands on the planet cell; backends agree; `--suite real` runs archival events. Needs the server's `kampylos_analyze.py` (`--analyze`) | minutes / hours |

- `src/kampylos_wu.h`, `boinc/` -> `test_cpu_resume` + `tests/test_boinc_resume.sh`.
- `src/kampylos_batched_fit.h`, `nelder_mead_stepper.h` -> `test_batched_resume` (+ the Metal resume run on a Mac).
- a kernel, `kampylos_gpu_complete.h`, `kampylos_cpu_patch_pool.h` -> the backend's objective test and the regression
  suite with that backend.
- seeds or the objective -> the regression suite; a legitimate change of the numbers in
  `tests/regression/expected_results.json` is explained in the commit.
- On 2026-10-27, `test_cpu_resume` built with the line in its header passed at 45a41d5 (macOS arm64, Apple clang 14).

## Environment notes

- A Mac with Apple Silicon builds and runs everything except CUDA and OpenCL. Keep Metal runs short when a BOINC
  client also uses the GPU: the driver then aborts command buffers ("Internal Error (0000000e)"); the app retries.
- The stub checkpoints every `KAMPYLOS_STUB_CHECKPOINT_SECONDS` (0 = every opportunity; default 60) and reports a CPU
  budget of `KAMPYLOS_STUB_NCPUS` (default 1, so a GPU wrapper on the stub runs one completion thread). It kills with
  SIGKILL only: it does not imitate the real client's quit, which flushes stdio (`docs/kb/shared/boinc-api.md`).
- `thread_local` is expensive on macOS (`_tlv_get_addr`) and with emulated TLS (MinGW-w64 GCC, Android NDK below API
  29); MinGW-w64 also crashes when threads with VBMicrolensing `thread_local` objects exit (KAM-010).
- Windows: `boinc_fopen` text modes write CRLF (KAM-023); the stub's `boinc_fopen` is plain `fopen`.
- Scratch binaries and work directories do not belong in the repository; `tests/test_boinc_resume.sh` takes a work dir.

## Conventions for changes

- Small branches, one concern each; one fix per commit with the test that shows it. The commit message says what was
  wrong, how it was reproduced or measured, and what the fix changes for volunteers (checkpoints, credit, runtime),
  and names the issue (`KB: KAM-NNN`) and the shared pattern if any (`Pattern: PAT-NNN`).
- Keep the backends symmetric: a fix to one wrapper or one `*_fit_binary_seeds()` usually belongs in all of them.
- Do not edit `vendor/VBMicrolensing`; change the fork or `vendor/patch_vbm_thread_local.py`.
- Comments say why, carry the date of the change, and name the forum thread, report or test behind it.
- Numbers in comments (chi2 411.22 on the synthetic planet, 0.69 s per Metal round, ...) were measured; if a change
  moves them, update the comment with the new measurement and the date. Claims in commits and docs are measured or
  say that they are not.

## Not in this repository

- The server side: `kampylos_assimilate.sh`, `kampylos_validate.sh`, `kampylos_analyze.py` (the regression suite
  calls it), the work-unit generator, plan classes (`avg_ncpus` sets the GPU apps' completion threads), the web page
  that calls `model_curve_cli`. They live in the parent `bitboinc` repository, which vendors this one at
  `vendor/kampylos` (paths in comments use that prefix).
- `cl_dynload.{c,h}`, the OpenCL loader the OpenCL build links (`src/kampylos_opencl_fit.h` includes it). It is a copy
  shared with other bitboinc apps, but since the file is not here, `docs/kb/kb.conf` has no `shared-code` line for it.
- Code of the other apps that comments compare with (`opencl_search.c`, `metal_search.mm`, Keraunos, Potamos).
- Release build scripts, BOINC library builds, Linux build containers, Android builds (CPU only, per README.md).
- The MUSA GPU backend that README.md names (KAM-016).

## Knowledge base

`docs/kb/` holds this repository's known bugs, platform traps and their fixes, and the bug patterns shared with the
other bitboinc applications. Start at [docs/kb/INDEX.md](docs/kb/INDEX.md): its first table, "Fix next", is the work
that is ready, with the next step for each issue. Before debugging a symptom, search it (`grep -ril "<words>" docs/kb`).
After a fix or a new finding, update the issue. Workflow: [docs/kb/shared/PROCESS.md](docs/kb/shared/PROCESS.md).

- Overview without opening files: `python3 docs/kb/shared/kb.py list --open [--platform windows] [--area checkpoint]`
- New report: `python3 docs/kb/shared/kb.py new issue "symptom in a few words"`
- A fix commit names its issue (`KB: KAM-NNN`) and, if any, its pattern (`Pattern: PAT-NNN`).
- Before a pull request: `python3 docs/kb/shared/kb.py index && python3 docs/kb/shared/kb.py lint --strict`
