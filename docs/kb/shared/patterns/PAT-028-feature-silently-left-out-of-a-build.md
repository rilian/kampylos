---
id: PAT-028
title: A build silently leaves out a feature the work units need (missing -D flag, configure option dropped)
status: active
severity: high
platforms: [all]
area: build
applies_to: code with preprocessor guards (#ifdef FEATURE) or configure-time options, built by more than one script
origin: not public
reference_fix:
added: 2026-10-27
---

## The defect

Code that matters at run time (reading the GPU assignment from `init_data`, an output format, an optional
file-format or compression library) is inside `#ifdef SOME_FLAG` or depends on a configure check. One build script defines the
flag, another does not, or configure quietly turns the feature off when a library is missing. The build succeeds,
the binary runs, and the feature is just not there: tasks run on the wrong GPU, or fail only when a work unit needs
the feature.

## Why it happens

Preprocessor guards and configure options fail silently by design. Several build scripts (one per platform) drift
apart, and the developer's own build is the one that has the flag.

## How to check a repository

List the guards: `git grep -nE '^[[:space:]]*#[[:space:]]*(if|ifdef|ifndef|elif).*[A-Z_]{4,}'` in the application's sources, and for
each guard that enables run-time behaviour, find where it is defined in every build script (and in configure logs or
`config.h`). Also look for configure options that turn a feature off when a library is missing.

* `affected`: a shipped build lacks a flag or library that another build has and that work units can need.
* `not-affected`: every shipped build defines the same feature flags, or a missing feature fails the build (`#error`,
  configure `--enable-x` that errors when x is missing) or is reported at run time.
* `n/a`: no feature guards or configure options in the shipped code.

## How to fix

Make missing features loud: `#error` when a required flag is missing for a given target, configure options that
fail instead of silently disabling, and a line in the app's stderr listing the compiled-in features. Keep the flags
of all build scripts in one place.

## How to test

Run each shipped binary with `--version` (or at start-up) and check the printed feature list; run a work unit that
needs each optional feature on every platform build.
