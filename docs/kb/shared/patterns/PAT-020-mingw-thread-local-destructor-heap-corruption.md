---
id: PAT-020
title: MinGW-w64 builds crash with heap corruption when threads with thread_local objects exit
status: active
severity: high
platforms: [windows, mingw]
area: build
applies_to: Windows builds made with MinGW-w64 GCC (winpthreads, emulated TLS) whose threads use thread_local objects with destructors
origin: kampylos KAM-010
reference_fix: kampylos@5a5ffff (src/kampylos_cpu_patch_pool.h: persistent worker threads that never exit)
added: 2026-10-27
---

## The defect

The Windows build ends with exit code 0xC0000374 (`STATUS_HEAP_CORRUPTION`) early in the work unit. In kampylos it
happened when worker threads that had used VBMicrolensing's `thread_local` scratch objects exited: one of the
destructors freed memory it did not own. MSVC and Linux/glibc builds of the same code did not trip it.

## Why it happens

Thread-local destructors under MinGW-w64's winpthreads and emulated TLS run at thread exit in an order and context
that some objects do not survive. Only threads that exit are affected.

## How to check a repository

```
git grep -nE 'thread_local|__thread|std::thread|pthread_create|#pragma omp|std::async'
```

and check which of those are in a MinGW build (build scripts: `x86_64-w64-mingw32`, `llvm-mingw`).

* `affected`: in the MinGW build, threads that touch `thread_local` objects with non-trivial destructors (own or in a
  library) are created and exit, repeatedly or at the end.
* `not-affected`: threads are created once and never exit, or no `thread_local` object with a destructor is used in
  threads, or Windows is built with MSVC only.
* `n/a`: no threads, or no Windows build.

## How to fix

Create the worker threads once and keep them for the life of the process; create the pool with `new` and never
delete it, so no static destructor joins them at exit (kampylos 5a5ffff). Avoid `thread_local` objects with
destructors in new code.

## How to test

Run the threaded path of the MinGW build for a whole work unit on Windows or under Wine.
