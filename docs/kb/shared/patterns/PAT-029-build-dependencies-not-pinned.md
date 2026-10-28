---
id: PAT-029
title: Build scripts fetch dependencies without pinning a version, so a rebuild is not the same build
status: active
severity: low
platforms: [all]
area: build
applies_to: build scripts that clone, download or install libraries or toolchains (BOINC, clFFT, container images, pip packages)
check: 'git clone|git fetch|curl |wget |pip install|apt(-get)? install|docker (run|pull)|FROM ' -- '*.sh' '*.ps1' '*.bat' '*.py' 'Makefile*' 'Dockerfile*' '*.yaml' '*.yml'
origin: not public
reference_fix:
added: 2026-10-27
---

## The defect

A build script clones a dependency's default branch, pulls a container image by a moving tag, or installs packages
without versions. Two builds of the same commit then differ: a BOINC API change, a new library release or a new
compiler in the image changes behaviour or results, and an old release cannot be rebuilt to find out why.

## Why it happens

`git clone <url>` and `FROM image:latest` work today; the drift shows months later.

## How to check a repository

```
git grep -nE 'git clone|git fetch|curl |wget |pip install|apt(-get)? install|docker (run|pull)|FROM ' -- '*.sh' '*.ps1' '*.bat' '*.py' 'Makefile*' 'Dockerfile*' '*.yaml' '*.yml'
```

* `affected`: a dependency that ends up in a shipped binary is fetched without a fixed version (commit, tag,
  checksum, image digest or exact package version).
* `not-affected`: every such fetch is pinned, or the script records the exact version it used next to the binary.
* `n/a`: the scripts fetch nothing; dependencies come from the build machine (then document where in `AGENTS.md`).

## How to fix

Pin commits and tags (`git clone --branch <tag>` or `git checkout <commit>`), image tags with a date or digest,
package versions; print the versions into the build log and, where cheap, into the binary's stderr.

## How to test

Build the same commit twice, a week apart, in clean containers, and compare the binaries' version output (and the
results of a small work unit).
