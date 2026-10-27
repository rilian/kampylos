# Vocabulary

The allowed values of the front-matter fields of issues and patterns and of the ledger status. `kb.py lint` reads the
first column of each table below, so a new value is added here and nowhere else (then `kb.py sync`, so every repository
gets it). Keep values lower case, one word or hyphenated.

## `type`

| Value | Use for |
|---|---|
| `bug` | wrong behaviour that needs a code or build change |
| `gotcha` | a trap of a platform, toolchain or library that the code has to live with; documents the workaround |
| `design-note` | a deliberate choice that looks like a bug, or a limit, with the reason |
| `question` | something only the maintainers can decide; the answer goes into the issue when it comes |

## `status`

| Value | Meaning |
|---|---|
| `new` | reported, not yet confirmed (the inbox) |
| `confirmed` | reproduced, or confirmed by reading the code at a named commit |
| `fix-proposed` | a fix exists on a branch or in a pull request that is not merged yet |
| `fixed` | the fix is in the upstream default branch (`fixed_in` names the commit) |
| `documented` | a gotcha or design note: written down, nothing to fix |
| `wont-fix` | confirmed, but it will not be changed (the issue says why) |
| `not-a-bug` | the report turned out to be wrong or intended behaviour |
| `duplicate` | the same as another issue (named in the body) |

## `action`

What has to happen next. Optional in an issue: without it, `kb.py` derives it from type and status (a question:
`decide`; `new`: `verify`; `fix-proposed`: `review`; `confirmed`: `fix`; a closed status: `none`). Set it when that
guess is wrong, for example `decide` for a confirmed bug whose fix needs a maintainer's answer. `INDEX.md` is ordered
by it.

| Value | Meaning |
|---|---|
| `fix` | the cause is known and the fix is clear: ready to work on |
| `decide` | needs an answer from the maintainers first (the question is in `next:`) |
| `verify` | reproduce, measure or read further before deciding what to change |
| `review` | a fix is written (branch or commit in `next:`) and waits for review and merge |
| `none` | nothing to do: fixed, documented, or closed otherwise |

## `severity`

| Value | Meaning |
|---|---|
| `critical` | wrong science results accepted, lost volunteer work on many hosts, or a crash on most hosts |
| `high` | a whole platform or configuration fails, tasks error out or run forever, results lost |
| `medium` | some hosts or some work units affected, or a workaround exists |
| `low` | cosmetic, rare, or wasted time without wrong results |
| `info` | no effect today; knowledge worth keeping |

## `platforms`

A list; use the narrowest tags that are true. `all` means every platform the application ships for.

| Value | Covers |
|---|---|
| `all` | every platform |
| `windows` | Windows (any toolchain) |
| `macos` | macOS (Intel and Apple Silicon) |
| `linux` | Linux desktop and server |
| `android` | Android (BOINC client for Android) |
| `unix` | everything POSIX: linux, macos, android |
| `x86_64` | 64-bit x86 |
| `i686` | 32-bit x86 |
| `aarch64` | 64-bit ARM (Apple Silicon, Linux arm64, Android arm64) |
| `armhf` | 32-bit ARM |
| `ppc64le` | POWER little endian |
| `riscv64` | 64-bit RISC-V |
| `cuda` | NVIDIA CUDA builds |
| `opencl` | OpenCL builds (any vendor) |
| `metal` | Apple Metal builds |
| `mingw` | MinGW-w64 / llvm-mingw toolchain |
| `msvc` | Microsoft Visual C++ toolchain |
| `gcc` | GCC-specific behaviour |
| `clang` | Clang-specific behaviour (Apple clang, Android NDK, llvm-mingw) |
| `boinc-client` | depends on the BOINC client version or its settings |
| `mcu` | microcontroller firmware |

## `area`

| Value | Covers |
|---|---|
| `checkpoint` | checkpoint files, resume, output trimming |
| `boinc-lifecycle` | init, suspend, quit, abort, temporary exit, finish, progress, critical sections |
| `trickle` | trickle-up and trickle-down messages |
| `device-selection` | choosing the GPU, platform or serial/USB device |
| `gpu-runtime` | kernel launches, GPU errors, watchdogs, memory |
| `io` | files, output format, write errors, line endings |
| `determinism` | results that differ between runs, platforms or backends |
| `science` | the computation itself: algorithms, parameters, physics |
| `build` | build scripts, toolchains, linking, packaging |
| `data-pipeline` | scripts that download or prepare input data |
| `hardware-io` | serial ports, HID, USB devices |
| `firmware` | code that runs on a device |
| `security-privacy` | secrets, personal data, opt-in rules |
| `performance` | speed, memory, CPU or GPU use |
| `docs` | documentation that is wrong or missing |

## `ledger`

The status of one pattern in one repository (`docs/kb/ledger.md`).

| Value | Meaning |
|---|---|
| `unchecked` | the pattern arrived through `kb.py sync` and nobody has looked yet |
| `affected` | the repository has the bug; the Issue column names the local issue |
| `fixed` | it had the bug and the fix is merged; the Issue column names the local issue |
| `not-affected` | the code it applies to exists here and is correct (Evidence says where) |
| `n/a` | the code it applies to does not exist here (Evidence says why) |
