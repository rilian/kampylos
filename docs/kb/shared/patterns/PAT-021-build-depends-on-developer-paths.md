---
id: PAT-021
title: Build scripts or defaults depend on paths that exist only on one developer's machine
status: active
severity: low
platforms: [all]
area: build
applies_to: build scripts, Makefiles, data-preparation scripts
check: '(^|[^A-Za-z])[A-Za-z]:[\\/]|/mnt/[a-z]/|/home/[^/$ ]+/|/Users/[^/$ ]+/' -- '*.sh' '*.ps1' '*.bat' '*.py' 'Makefile*' '*.mk' '*.cmake'
origin: planodia PLA-008
reference_fix:
added: 2026-10-27
---

## The defect

Absolute paths such as `D:/...`, `C:\...`, `/mnt/c/...`, `/home/<user>/...` or `/Users/<user>/...` in build scripts
or as default data locations: a clean clone does not build elsewhere, a release build can silently pick up stale
headers or libraries from that path, and nobody else can reproduce a release.

## Why it happens

Scripts grow on one machine and are committed as they are.

## How to check a repository

```
git grep -nE '(^|[^A-Za-z])[A-Za-z]:[\\/]|/mnt/[a-z]/|/home/[^/$ ]+/|/Users/[^/$ ]+/' -- '*.sh' '*.ps1' '*.bat' '*.py' 'Makefile*' '*.mk' '*.cmake'
```

* `affected`: a hit that the build or the default run needs and that cannot be overridden.
* `not-affected`: such paths only as overridable defaults (environment variable or argument), documented in
  `AGENTS.md`.
* `n/a`: no hits.

## How to fix

An environment variable or argument with the current value as the default, and a line in the "Building" section of
`AGENTS.md`.

## How to test

Build from a fresh clone in a container that does not have the path.
