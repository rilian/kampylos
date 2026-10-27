---
id: PAT-023
title: A resumable data download marks failed units as done, or duplicates rows when it stops between write and mark
status: active
severity: high
platforms: [all]
area: data-pipeline
applies_to: scripts that fetch or convert input data in units (files, chunks, pages) and keep a progress record to resume
origin: nostos NOS-006
reference_fix:
added: 2026-10-27
---

## The defect

Input data (catalogues, light curves, survey files) is downloaded or converted unit by unit, and a progress file
records which units are done so a stopped run can resume. Two failures:

* (a) a unit that failed (after its retries) is recorded as done anyway, so it is never fetched again and its rows
  are silently missing from every work unit made from the data;
* (b) the rows of a unit are appended to the output and the unit is recorded as done in two separate steps; a stop
  in between (crash, kill, power cut, Ctrl-C) means the next run appends the same rows again.

Both go unnoticed because the totals look plausible.

## Why it happens

Retry loops end with the same "mark done" as the success path, and append-then-mark looks atomic on a developer
machine that is never interrupted.

## How to check a repository

`git grep -nE 'progress|done|resume|retry|requests\.|urllib|urlopen|wget|curl' -- '*.py' '*.sh'` and read each
download or conversion loop.

* `affected`: a unit is marked done after a failure; or output rows and the done mark are written in two steps with
  no way to detect the overlap (no per-unit output file, no recorded offset, no deduplication).
* `not-affected`: failures are logged and left for the next run, and each unit's output is written to its own file
  (or to a temporary file renamed into place, or with a recorded offset) before it is marked done.
* `n/a`: the repository does not download or prepare data in resumable units.

## How to fix

Mark a unit done only after success. Write each unit's rows to its own file (temporary name, then rename), or record
the output size together with the done mark and cut the output back to it on resume; flush and `fsync` before the
mark. At the end, report the units that failed and the expected versus actual row counts.

## How to test

Mock the network: make one unit fail every time and check it is not marked done; kill the script between write and
mark (inject an exception) and check that a resumed run produces no duplicate rows.
