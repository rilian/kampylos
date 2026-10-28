---
id: XXX-000
title: One line that names the defect, not the fix
type: bug            # bug | gotcha | design-note | question (see shared/vocabulary.md)
status: new          # new | confirmed | fix-proposed | fixed | documented | wont-fix | not-a-bug | duplicate
severity: medium     # critical | high | medium | low | info
platforms: [all]     # narrowest true tags: [windows, opencl], [android, aarch64], ...
area:                # one value from shared/vocabulary.md (kb.py new issue prints them)
action:              # optional: fix | decide | verify | review | none (derived from type and status when empty)
next:                # one line: the change to make, the question to answer, or what to verify
blocked_by: []       # issues that must be answered or fixed first
pattern:             # PAT-NNN, or [PAT-NNN, PAT-MMM], when this is an instance of cross-repository patterns
aliases: []          # other names this issue had: plan or audit ids, forum thread titles
reported: 2026-01-01
source:              # where it came from: forum thread N, task stderr, audit YYYY-MM-DD, maintainer
where:               # file:line that the fix changes @ commit (the commit the line numbers refer to)
fixed_in:            # commit(s) or pull request, once fixed
---

## Symptom

What a volunteer or developer sees. Paste the exact error text or stderr lines, name the client version and platform.

## Cause

Why it happens, with file:line references at the commit in `where`. Say what was verified and how (read the code,
reproduced, test), and what is still a guess.

## Fix

What was changed, or is proposed, and why this way. Alternatives that were rejected and why. What it changes for
volunteers (checkpoints, credit, runtime, results).

## Test

How to reproduce it, and which test covers it now (command line).

## Log

- 2026-01-01: created.
