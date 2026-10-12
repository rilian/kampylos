# Working on Kampylos (notes for AI agents and new contributors)

Kampylos is the binary-lens microlensing grid search of the bitboinc BOINC project. One work unit = one light curve
plus a small range of grid cells in (s, q); for every cell the app runs a multistart Nelder-Mead fit of
(t0, u0, tE, alpha, rho) with VBMicrolensing as the magnification engine and writes one result line. The same search
exists for CPU, CUDA, OpenCL and Apple Metal, and the four must stay byte-compatible in input, output and resume
behaviour. Read this file before changing anything; the source comments are the detailed documentation and are kept
deliberately long -- preserve that style (explain *why*, date the change, name the forum thread or test that motivated it).

## Layout

| path | what |
|---|---|
| `src/lightcurve.h` | `DataPoint`, `load_photometry()` (sorts by time), the one closed-form (fs, fb) solve `flux_fit_solve()` shared by every backend |
| `src/single_lens.h`, `src/pspl_prefit.h` | the single-lens anchor (PSPL / FSPL / FSPL+LD) every cell is compared against |
| `src/simplex.h`, `src/nelder_mead_stepper.h` | blocking Nelder-Mead (CPU) and its steppable twin (GPU batching, checkpointable state) |
| `src/binary_fit.h` | the CPU objective and fitters, `kampylos_make_seeds()` (seed sets A-D) |
| `src/kampylos_wu.h` | **the work-unit contract**: input line parsing (`log10`/`ln` marker, `part i K`), result header/line format, per-cell finalisation, `kampylos_run_cells()`, result-file resume helpers |
| `src/kampylos_cpu_cell.h` | CPU per-cell seed loop with the seed-level checkpoint (`KampylosCellHooks`) |
| `src/kampylos_batched_fit.h` | backend-independent batched driver for the GPU paths, with the in-cell checkpoint (`KampylosBatchedHooks`) |
| `src/kampylos_gpu_types.h`, `src/kampylos_gpu_complete.h`, `src/kampylos_cpu_patch_pool.h` | GPU kernel I/O structs, host-side completion of the candidates the kernel could not finish (exact finite-source points), the worker pool for that step |
| `src/kampylos_gpu_mag.cuh`, `src/kampylos_gpu_kernel.cu`, `src/gpu_fit_binary.{cu,h}` | CUDA |
| `src/kampylos_kernel.cl`, `src/embed_kampylos_cl.py` -> `src/kampylos_cl_embedded.h`, `src/kampylos_opencl_fit.{cpp,h}` | OpenCL (regenerate the embedded header after editing the `.cl`) |
| `src/kampylos_metal_fit.{mm,h}` | Metal: MSL kernel as a string (double-single arithmetic), host orchestration |
| `src/fit_event.cpp` | standalone CLI, same code path as the BOINC apps, backend chosen with `-DKAMPYLOS_CLI_{CUDA,OPENCL,METAL}` |
| `boinc/` | the four BOINC mains (thin) + `kampylos_boinc_common.h` (input/output/checkpoint plumbing), `boinc/README.md` (build lines, checkpoint files) |
| `tests/` | unit/integration tests (each file's header comment has its build line), `tests/boinc_stub/` (fake BOINC runtime), `tests/regression/` (end-to-end suite + expected results) |
| `vendor/VBMicrolensing` | submodule: `alplix/VBMicrolensing`, branch `thread-safe-statics` (v5.6 with `thread_local` statics, defines `VBM_THREAD_SAFE`); `vendor/patch_vbm_thread_local.py` makes plain upstream equivalent |

Not in this repository: the BOINC server side (`kampylos_assimilate.sh`, `kampylos_analyze.py`, `kampylos_validate.sh`),
the OpenCL loader `cl_dynload.{h,c}` the OpenCL build links against, and the other bitboinc apps (Keraunos, Potamos,
...) whose Metal/OpenCL code the comments compare with. They live in the parent `bitboinc` repository, which vendors
this one at `vendor/kampylos`.

## Invariants (break one and results become silently wrong)

- **Natural logs inside, grid units outside.** Every `log_s`/`log_q` passed into a fit function is a natural log
  (VBMicrolensing's convention). Work units created since 2026-10-08 carry the `log10` marker; `kampylos_grid_to_ln()`
  converts, the result line gets the grid value back. The four mains must never do this conversion themselves.
- **One parser, one writer.** Input parsing, the header, the result line and the per-cell diagnostics live only in
  `kampylos_wu.h`; the server parses the result files with one parser. Do not touch the format without the server side.
- **Same answer on every backend.** The GPU kernels compute the chi2 the optimiser steers on; the final chi2/fs/fb of
  a cell are always recomputed on the host (`kampylos_finalize_cell`) with VBMicrolensing, so a parameter vector reports
  the same numbers whichever device found it. `tests/regression/run_regression.py` checks backends against each other.
- **Metal: fast math OFF, double-single arithmetic.** Metal has no `double`; the kernel is written in float-float
  ("ds") arithmetic whose error-free transformations fast math is allowed to delete. `metal_fit_select_device()` sets
  `fastMathEnabled = NO` / `MTLMathModeSafe` and a self-test kernel refuses to run otherwise. Never "simplify" the
  two_sum/two_prod code, never return to float sums (the 0.1-0.4 chi2 noise of float products steered the fit into
  the wrong basin: 691 vs 411 on the synthetic planet cell).
- **Command buffers / OpenCL calls are checked.** Every Metal command buffer's status is read and a failed one is
  re-submitted; every OpenCL call goes through `CL_CHECK`. Reading results after an unchecked launch returns the
  previous round's numbers.
- **Determinism.** Seeds, rounds and the completion step are deterministic for a backend; a resumed cell must be
  bit-identical to an uninterrupted one. The completion step adds bad points in index order for this reason. Any new
  source of nondeterminism (thread order, unordered containers, uninitialised fields) breaks the resume tests.
- **VBMicrolensing is only thread-safe when patched.** `KampylosCpuPatchPool` runs on several threads only if
  `VBM_THREAD_SAFE` or `VBM_THREAD_LOCAL_STATICS` is defined by the library header (the submodule fork / the patch
  script); otherwise it falls back to one thread. Do not raise `KAMPYLOS_PATCH_THREADS` against plain upstream.
  The pool's workers are created once and never joined (thread_local destructors crash under MinGW).
- **Include order in the BOINC mains:** Kampylos headers (which pull in VBMicrolensing) *before* `boinc_api.h` /
  `filesys.h` (windows.h macros collide with VBMicrolensing identifiers). Metal files need `-fobjc-arc`.
- **Checkpoint files** (`kampylos_checkpoint`, `kampylos_seed_checkpoint`, `kampylos_batched_checkpoint`) are written
  via temporary file + rename, inside a BOINC critical section; the result file `out` is the authoritative record of
  finished cells and is never truncated while it has complete lines. See `boinc/kampylos_boinc_common.h`'s top comment.
- **Version strings** are build flags (`-DKAMPYLOS_VERSION='"12"'`, `KAMPYLOS_GPU_VERSION`, `KAMPYLOS_OPENCL_VERSION`,
  `KAMPYLOS_METAL_VERSION`) and must match the server's `version_num`; the defaults in the mains are fallbacks only.
- The GPU backends refuse seed-split work units (`part i K`); only the CPU app can run them.

## Building

Everything is header-only C++17 plus one VBMicrolensing translation unit; there is no build system. Each test file's
header comment carries its exact build line. The patterns (from the repo root, `VBM=vendor/VBMicrolensing/VBMicrolensing/lib`):

```
# CPU test / CLI
c++ -O2 -std=c++17 -I src -I $VBM tests/test_cpu_resume.cpp $VBM/VBMicrolensingLibrary.cpp -o test_cpu_resume
c++ -O2 -std=c++17 -I src -I $VBM src/fit_event.cpp $VBM/VBMicrolensingLibrary.cpp -o fit_event_cpu
# Metal (macOS)
clang++ -O2 -std=c++17 -fobjc-arc -I src -I $VBM tests/test_metal_fit.mm src/kampylos_metal_fit.mm \
    $VBM/VBMicrolensingLibrary.cpp -framework Metal -framework Foundation -framework CoreGraphics -o test_metal_fit
# BOINC wrappers: see boinc/README.md (needs BOINC's api/ and lib/ headers and static libs);
# without a BOINC checkout, build them against tests/boinc_stub/ to run them as plain processes.
```

`VBMicrolensingLibrary.cpp` compiles with warnings; filter them (`2>&1 | grep -v VBMicrolensingLibrary`) rather than
touching the vendored source. CUDA needs `nvcc`; OpenCL needs the parent repository's `cl_dynload.c` and an OpenCL SDK
(a loader stub with the same `cl_api` table is enough for a syntax check).

## Tests and what to run for which change

| test | covers | runtime |
|---|---|---|
| `tests/test_cpu_resume.cpp` | seed checkpoint round trip, resumable CPU cell (incl. `part i K`), result-file trim/count helpers | seconds |
| `tests/test_batched_resume.cpp` | batched driver state round trip, kills at random rounds, bit-identical resume, progress monotonic | minutes |
| `tests/test_boinc_resume.sh [workdir]` | the real CPU wrapper under the stub, SIGKILLed repeatedly: resumed `out` byte-identical; stale / behind / torn checkpoint scenarios. `KAMPYLOS_RESUME_BACKEND=metal` runs the Metal wrapper instead | ~10 min |
| `tests/test_metal_fit.mm [--cell]` | Metal objective vs CPU objective point by point (fast-math self-test, no Metal-only rejections, chi2 noise); `--cell` fits the whole cell | seconds / minutes |
| `tests/test_vbm_threads.cpp` | VBMicrolensing from 4 threads: crashes on an unpatched library, passes on the fork | seconds |
| `tests/test_nelder_mead_stepper.cpp`, `test_ds_primitives.cpp`, `test_kampylos_mag_ds.cpp`, `test_kampylos_mag_f32.cpp` | stepper == blocking NM; double-single primitives and magnification vs double | seconds |
| `tests/regression/run_regression.py --cli cpu=PATH [--cli metal=PATH ...]` | end-to-end: synthetic planet WU must land on the planet cell; backends must agree; `--suite real` runs the archival events (slow, needs the data dir) | minutes / hours |

Rules of thumb: anything in `kampylos_wu.h` or the mains -> `test_cpu_resume` + `test_boinc_resume.sh`; anything in
`kampylos_batched_fit.h` / `nelder_mead_stepper.h` -> `test_batched_resume` (+ the Metal resume run on a Mac);
anything in a kernel or in `kampylos_gpu_complete.h` -> the matching objective test and the regression suite with
that backend; anything touching seeds or the objective -> regression suite (expected numbers in
`tests/regression/expected_results.json`; a legitimate change of the numbers must be explained in the commit).

## Environment notes

- A Mac with Apple Silicon can build and run everything except CUDA and OpenCL. Keep Metal runs short when the GPU
  is shared with a running BOINC client: another process on the GPU makes the driver abort command buffers
  ("Internal Error (0000000e)"), which the app retries, but it slows measurements.
- The stub (`tests/boinc_stub`) checkpoints every `KAMPYLOS_STUB_CHECKPOINT_SECONDS` seconds (0 = every opportunity);
  the real client allows a checkpoint at most once per its `disk_interval` (60 s default).
- Scratch binaries and work directories do not belong in the repo; `tests/test_boinc_resume.sh` takes a work dir.

## Conventions for changes

- Small, reviewable branches, one concern each; commit messages state what was wrong, how it was measured, and
  what the fix changes for volunteers (checkpoints, credit, runtime).
- Keep the four backends symmetric: a fix to one wrapper or one `*_fit_binary_seeds()` usually belongs in all four.
- Do not edit `vendor/VBMicrolensing` directly; change the fork or `vendor/patch_vbm_thread_local.py`.
- Numbers quoted in comments (chi2 411.22 on the synthetic planet, 0.69 s command buffers, ...) were measured; if a
  change moves them, update the comment with the new measurement and the date.
