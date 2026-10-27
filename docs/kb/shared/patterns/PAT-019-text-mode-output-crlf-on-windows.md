---
id: PAT-019
title: Result or checkpoint files opened in text mode get CRLF line ends on Windows
status: active
severity: low
platforms: [windows]
area: io
applies_to: applications that write or parse files and ship a Windows build
origin: phygas PHY-006
reference_fix:
added: 2026-10-27
---

## The defect

On Windows `fopen(path, "w")` turns every `\n` into `\r\n`. The output's bytes differ from every other platform:
checksums and byte-comparing validators disagree, byte offsets used to trim a file on resume are wrong, and a parser
that splits on `\n` keeps a `\r` at the end of the last field. Reading in text mode also treats Ctrl-Z as end of file
in some C runtimes.

## Why it happens

The C runtime's text mode on Windows (MSVC and MinGW alike). See [boinc-api.md](../boinc-api.md#files).

## How to check a repository

```
git grep -nE 'fopen\([^)]*"[rwa]\+?"'
```

For files the application produces or trims (output, checkpoint, state) and for the inputs it parses.

* `affected`: a produced file opened in text mode in a Windows build, or a parser that breaks on CRLF input.
* `not-affected`: binary mode (`"wb"`, `"ab"`, `"rb"`) for every produced file, and parsers that accept `\r\n`; or
  the only text-mode files are private to the application (a checkpoint holding numbers read back with `fscanf`) or
  placeholders that carry no data, and nothing compares or trims them by bytes.
* `n/a`: no Windows build.

## How to fix

Open produced files in binary mode; strip a trailing `\r` in parsers.

## How to test

Build with MinGW, run a work unit under Wine or on Windows, and `cmp` the output with the Linux output; `grep -c $'\r'`
must be 0.
