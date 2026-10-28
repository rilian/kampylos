# Knowledge base reference

The records and their fields, the files copied between repositories, and the commands of `kb.py`. The procedure is in
[PROCESS.md](PROCESS.md); the allowed values of every field are in [vocabulary.md](vocabulary.md).

## Records

* An **issue** (`issues/<PREFIX>-NNN-title.md`) belongs to one repository: a bug, a gotcha (a platform trap the code
  must live with), a design note (a choice that looks like a bug) or a question for the maintainers. Its front matter
  comes first and holds what matters: `status`; `action` (`fix`, `decide`, `verify`, `review`; derived from type and
  status when empty); `next`, one line with the change to make, the question, or what to verify; `where`, the
  file:line that the fix changes and the commit the line numbers refer to. The body (Symptom, Cause, Fix, Test, Log)
  is for whoever works on that issue.
* A **pattern** (`shared/patterns/PAT-NNN-title.md`) is a class of defect that can occur in any application, written
  for someone who does not know where it was found: the defect, why it happens, how to check a repository (its main
  grep is the `check:` line), how to fix and how to test it, and a `reference_fix` to copy.
* The **ledger** (`ledger.md`) has one row per pattern: `unchecked`, `affected`, `fixed`, `not-affected` or `n/a`,
  the date, the commit of the code that was checked, and the evidence. An `affected` or `fixed` row names the local
  issue, and the issue names the pattern (`pattern:`).

Front matter is one `key: value` per line; two spaces followed by `#` start a comment (the templates' hints).

`INDEX.md` is generated from the records (`kb.py index`): open issues ordered by action, then notes, closed issues,
patterns and open issues per platform. Platforms have no files of their own: issues and patterns carry `platforms:`
tags (`kb.py list --platform android [--patterns]`; a new platform is a new row in `vocabulary.md`). `kb.conf` names
the repository, its issue prefix and the files it shares with other repositories. `shared/boinc-api.md` holds the
BOINC API facts the code depends on, read in the BOINC source.

`AGENTS.md`, at the root of each repository and read at the start of every session, says what is true of the code
today: rules, contracts, gotchas, design notes, and the known defects that anyone working on that code must know
about, each with its issue id. The issues themselves, with their next steps, stay in the knowledge base.

## Files copied between repositories

Some source files exist as copies in several repositories (`cl_dynload.c`, the orbit core of two applications). Each
repository lists its copies in `kb.conf` (`shared-code = cl_dynload.c src/cl_dynload.c`), and `kb.py status` prints
`DRIFT` with the date of each copy's last change when copies differ. A drift is an intended local difference (write a
design note in the repository that differs) or a fix not copied yet (record and fix it like any other issue).

## Commands

`todo`, `check`, `status`, `show` and `new pattern` read every repository checked out next to this one
(`../*/docs/kb`) when no repositories are given; `sync`, which writes, takes them explicitly. Each command's `--help`
says more; `sync -h` and `init -h` explain sharing and adding a repository.

```
python3 docs/kb/shared/kb.py list [--open] [--action fix] [--platform windows] [--area checkpoint,io] [--patterns]
python3 docs/kb/shared/kb.py show KAM-001 [PHY-015 PAT-005 ...] [--section Cause,Fix]
python3 docs/kb/shared/kb.py new issue "Title" area=io [--pattern PAT-001] [--type gotcha] [severity=low next=...]
python3 docs/kb/shared/kb.py new pattern "Title" [--siblings ../kampylos ../phygas ...]
python3 docs/kb/shared/kb.py set PLA-004 key=value ... [--log "text"]          (lists: platforms=windows,mingw)
python3 docs/kb/shared/kb.py ledger PAT-001 STATUS --evidence "..." [--issue PLA-004] [--commit abc1234]
python3 docs/kb/shared/kb.py todo [--pattern PAT-001] [--action fix] [../kampylos ../phygas ...]
python3 docs/kb/shared/kb.py check PAT-001 [--limit 8] [../kampylos ../phygas ...]
python3 docs/kb/shared/kb.py index && python3 docs/kb/shared/kb.py lint [--strict]
python3 docs/kb/shared/kb.py sync ../kampylos ../phygas ... [--dry-run]
python3 docs/kb/shared/kb.py status [../kampylos ../phygas ...]
python3 docs/kb/shared/kb.py init --name NAME --prefix ABC
```

## Issue id prefixes in use

| Repository | Prefix |
|---|---|
| kampylos | KAM |
| nostos | NOS |
| phygas | PHY |
| planodia | PLA |
| yarrow | YAR |

Prefixes of repositories that are not public are not listed here; pick one that is not in this table.
