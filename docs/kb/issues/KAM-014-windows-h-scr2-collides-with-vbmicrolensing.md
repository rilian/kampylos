---
id: KAM-014
title: windows.h defines scr2, which breaks VBMicrolensing's header when the BOINC headers come first
type: gotcha
status: documented
severity: low
platforms: [windows]
area: build
pattern:
aliases: []
reported: 2026-10-10
source: comment added with the wrappers (b5f5ef2)
where: boinc/main_kampylos_boinc.cpp:26-37 @ 45a41d5
fixed_in:
---

## Symptom

A Windows build of a BOINC wrapper fails with "expected unqualified-id before numeric constant" inside
`VBMicrolensingLibrary.h`.

## Cause

On Windows `boinc_api.h` and `filesys.h` include `windows.h`, which defines a set of short dialog-control macros,
among them `scr2`. VBMicrolensing declares members and locals with that name (`VBMicrolensingLibrary.h:108`
`double scr2, sscr2;`, `VBMicrolensingLibrary.cpp:1218`). If the macro is defined first, the declarations become
numbers.

## Fix

The workaround in place, verified at 45a41d5: every main includes the Kampylos headers (which pull in VBMicrolensing)
before `kampylos_boinc_common.h`, and that file includes `boinc_api.h` and `filesys.h` only after its own Kampylos
includes (`boinc/kampylos_boinc_common.h:9-12`, `:32-35`; `boinc/main_kampylos_boinc.cpp:26-37`; the GPU mains have
the same note). The vendored library is not patched. Keep this order in any new main or test that uses both.

## Test

A MinGW or MSVC build of a wrapper against the real BOINC headers.

## Log

- 2026-10-27: recorded as a gotcha, verified at 45a41d5 by reading.
