---
id: KAM-016
title: README.md names a MUSA GPU backend that has no code in this repository
type: bug
status: confirmed
severity: low
platforms: [all]
area: docs
action: decide
next: Decide where the MUSA build comes from and document it, or remove MUSA from README.md line 24
pattern:
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan §3.3)
where: README.md:24 @ 45a41d5
fixed_in:
---

## Symptom

README.md:24 lists the GPU backends as "CUDA, OpenCL, Apple Metal, and MUSA". A contributor looking for the MUSA
code finds none.

## Cause

`git grep -i musa` at 45a41d5 finds only that line. `boinc/README.md` lists four apps (CPU, CUDA, OpenCL, Metal) and
`src/` has no MUSA source. Whether a MUSA build exists elsewhere (in the parent repository, or the CUDA source built
with another toolchain) is not known here.

## Fix

Maintainers: say where the MUSA build comes from (and add it to `AGENTS.md` "Not in this repository"), or drop it
from README.md. This knowledge-base step changes no existing document.

## Test

`git grep -i musa`.

## Log

- 2026-10-27: recorded from the 2026-10-21 audit, verified at 45a41d5.
