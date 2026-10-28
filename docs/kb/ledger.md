# Pattern ledger

One row per cross-repository pattern ([shared/patterns](shared/patterns)): has this repository been checked for it,
at which commit, and what was found. `kb.py sync` adds an `unchecked` row whenever a new pattern arrives; checking it
means following the pattern's "How to check a repository" section and filling in the row. Status values:
[shared/vocabulary.md](shared/vocabulary.md#ledger). A row that is `affected` or `fixed` names the local issue, and that
issue names the pattern in its `pattern:` field; its Evidence says where, in one clause, and the issue has the rest.
Write rows with `kb.py ledger` (date and commit filled in) rather than by hand.

When code that a row describes changes a lot, check the pattern again and update Checked, Commit and Evidence.

| Pattern | Status | Checked | Commit | Issue | Evidence |
|---|---|---|---|---|---|
| PAT-001 | not-affected | 2026-10-27 | 45a41d5 |  | one boinc_time_to_checkpoint() per opportunity, each 1 followed by boinc_checkpoint_completed(): src/kampylos_cpu_cell.h:88-91, src/kampylos_batched_fit.h:207-210 and 258-261; cell checkpoint begin_critical_section + checkpoint_completed, boinc/kampylos_boinc_common.h:223-228 |
| PAT-002 | not-affected | 2026-10-27 | 45a41d5 |  | resume at min(checkpoint, complete lines of out), torn last line dropped, out cut back before appending (boinc/kampylos_boinc_common.h:186-205, src/kampylos_wu.h:295-337); the line is flushed before the cell checkpoint (src/kampylos_wu.h:283-284); one line per cell, no end marker; one path for all four backends. The cut is in place: PAT-003 |
| PAT-003 | affected | 2026-10-27 | 45a41d5 | KAM-004, KAM-007 | boinc/kampylos_boinc_common.h:224-228, 248-253, 265-270 report a failed checkpoint write (d); src/kampylos_wu.h:329-337 trims out in place, unchecked (a, b) |
| PAT-004 | not-affected | 2026-10-27 | 45a41d5 |  | in-cell checkpoints gated by boinc_time_to_checkpoint() (src/kampylos_cpu_cell.h:88, src/kampylos_batched_fit.h:207, 258); the cell checkpoint after every cell is deliberate (the seed loop consumes the flag, boinc/kampylos_boinc_common.h:20-23): one integer, atomic, in a critical section (:221-229), cells take minutes to days |
| PAT-005 | affected | 2026-10-27 | 45a41d5 | KAM-001 | src/kampylos_wu.h:296-297 fopen failure returns -1 = no output |
| PAT-006 | affected | 2026-10-27 | 45a41d5 | KAM-003 | boinc/kampylos_boinc_common.h:236-238 fclose unchecked then boinc_finish(0) |
| PAT-007 | affected | 2026-10-27 | 45a41d5 | KAM-019 | boinc_fraction_done only after a cell, seed or round and at the end (boinc/kampylos_boinc_common.h:222, 237, 254, 271) |
| PAT-008 | n/a | 2026-10-27 | 45a41d5 |  | no boinc_get_status/quit_request/abort_request polling, direct_process_action left at its default in all four mains; quit and abort handled by the library |
| PAT-009 | n/a | 2026-10-27 | 45a41d5 |  | no trickle messages (git grep boinc_send_trickle_up: none) |
| PAT-010 | affected | 2026-10-27 | 45a41d5 | KAM-024 | src/kampylos_metal_fit.mm:726 usleep(200000 * attempt) retry backoff, result ignored |
| PAT-011 | fixed | 2026-10-27 | 45a41d5 | KAM-008 | init_data wins: boinc/main_kampylos_gpu_boinc.cpp:67-71 (gpu_device_num), boinc/main_kampylos_opencl_boinc.cpp:71-77 (gpu_opencl_dev_index), since b5f5ef2, no preprocessor guard around it; Metal takes the default device, the Metal binary seen in the field is macos_arm64 (one GPU per Apple Silicon Mac) |
| PAT-012 | affected | 2026-10-27 | 45a41d5 | KAM-022, KAM-028, KAM-009 | src/kampylos_opencl_fit.cpp:30-36, 70-87: vendor match only when EXPECTED_GPU_VENDOR is defined, device count and list not GPU-only |
| PAT-013 | affected | 2026-10-27 | 45a41d5 | KAM-002 | CUDA exit(1) (src/gpu_fit_binary.cu:23-29, src/gpu_fit_binary.h:49-54) and boinc_finish(1) on device failure |
| PAT-014 | affected | 2026-10-27 | 45a41d5 | KAM-005 | src/kampylos_kernel.cl:16 enables cl_khr_fp64 |
| PAT-015 | affected | 2026-10-27 | 45a41d5 | KAM-006 | src/kampylos_opencl_fit.cpp:171-186 local size 128, no limit queried |
| PAT-016 | affected | 2026-10-27 | 45a41d5 | KAM-011 | CUDA and OpenCL launch every candidate of a round (or of a 15 s chunk) at once: src/gpu_fit_binary.cu:45-48, src/kampylos_opencl_fit.cpp:184-186, not measured |
| PAT-017 | n/a | 2026-10-27 | 45a41d5 |  | no Android GPU build here: the OpenCL loader cl_dynload.c is in the parent repository, no __ANDROID__ or dlopen in this repository, README.md lists Android for the CPU app only |
| PAT-018 | affected | 2026-10-27 | 45a41d5 | KAM-025 | GCC 13 emits 67 FMAs in src/fit_event.cpp and 3414 in VBMicrolensingLibrary.cpp at -O2 on arm64, Apple clang 14 none (counted 2026-10-27) |
| PAT-019 | affected | 2026-10-27 | 45a41d5 | KAM-023 | out opened with boinc_fopen "w"/"a" (boinc/kampylos_boinc_common.h:205), checkpoint temp file "w" (:46) |
| PAT-020 | fixed | 2026-10-27 | 45a41d5 | KAM-010 | persistent pool workers that never exit, pool never deleted: src/kampylos_cpu_patch_pool.h:52-62, 108-117, 175-191 (5a5ffff); no other threads in src/ and boinc/ |
| PAT-021 | n/a | 2026-10-27 | 45a41d5 |  | no build scripts in this repository; the grep finds nothing; the only absolute paths are a Windows CUDA toolkit path in a build-line comment, tests/test_kampylos_opencl.cpp:14, 16 |
| PAT-022 | n/a | 2026-10-27 | 45a41d5 |  | no Linux build script or container in this repository; release binaries are built elsewhere and were not available to check |
| PAT-023 | n/a | 2026-10-27 | 45a41d5 |  | no download or data-preparation scripts in this repository (the grep finds only comments of tests/test_boinc_resume.sh); work units and light curves are made on the server side |
| PAT-024 | affected | 2026-10-27 | 45a41d5 | KAM-029 | no CUDA release build line here; the only -gencode is sm_120 without PTX (tests/test_gpu_fit_binary.cu:10) |
| PAT-025 | affected | 2026-10-27 | 45a41d5 | KAM-027 | GPU apps: completion-pool threads finish their share of the current evaluation call after a suspend (src/kampylos_cpu_patch_pool.h:143-191) |
| PAT-026 | not-affected | 2026-10-27 | 45a41d5 |  | sorts with ties that reach the output are the Nelder-Mead vertex sorts (src/simplex.h:44, src/nelder_mead_stepper.h:156; ties at the 1e18 sentinel, 6 elements): every tie pattern of 3 to 7 elements from 3 values sorts the same with libc++ (Apple clang 14) and libstdc++ (GCC 13), tested 2026-10-27, MSVC STL not tested; the other sorts order plain values, use stable_sort, or keep the best of fitted chi2 values |
| PAT-027 | not-affected | 2026-10-27 | 45a41d5 |  | CUDA: every cudaMemcpy and cudaGetLastError after the launch checked (src/gpu_fit_binary.cu:43-51, src/gpu_fit_binary.h:49-74); OpenCL: CL_CHECK on every write, argument, launch, finish and read (src/kampylos_opencl_fit.cpp:168-191); Metal: command-buffer status checked and retried (src/kampylos_metal_fit.mm:700-729); Metal buffer allocations are not checked for nil (a crash, not stale data) |
| PAT-028 | affected | 2026-10-27 | 45a41d5 | KAM-028 | EXPECTED_GPU_VENDOR defaults to "" (src/kampylos_opencl_fit.cpp:30-32), which turns the vendor match off silently, and no build line in this repository defines it |
| PAT-029 | n/a | 2026-10-27 | 45a41d5 |  | no script fetches anything; VBMicrolensing is pinned by the submodule commit a56b7bf; BOINC libraries, CUDA toolkit, OpenCL headers and cl_dynload come from the build machine or the parent repository (AGENTS.md, Building) |
| PAT-030 | not-affected | 2026-10-27 | 45a41d5 |  | only the Metal fault paths (device unavailable :83, command buffers keep failing :111 in boinc/main_kampylos_metal_boinc.cpp), 300 s delay: the client gives up after about 8 h, which the comment at :10-16 intends |
