---
id: PAT-000
title: One line that names the class of defect
status: active       # active | retired (never delete a pattern: retire it and say why)
severity: medium     # the usual severity when a repository has it
platforms: [all]
area: checkpoint
applies_to:          # which code makes a repository a candidate, e.g. "calls boinc_time_to_checkpoint()"
check:               # arguments of `git grep -nE` that find the candidate code, e.g. 'boinc_fopen|fopen' -- src
origin:              # public repository and issue where it was first found, e.g. planodia PLA-001
reference_fix:       # repository@commit of a fix others can copy, once one exists
added: 2026-01-01
---

## The defect

What goes wrong and what a volunteer sees. Written for someone who does not know the repository it was found in.

## Why it happens

The mechanism, with the facts it rests on (link shared/boinc-api.md where it is a BOINC API fact).

## How to check a repository

Concrete steps another repository can run: the grep commands, what a hit must look like to count as `affected`,
what makes it `not-affected`, and what makes it `n/a`. Record the result with that repository's `kb.py ledger`.

## How to fix

The fix in general terms, and the reference fix if there is one.

## How to test

A test or a manual reproduction that fails before the fix and passes after it.
