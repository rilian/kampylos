# How the knowledge base works

Every bitboinc application repository has the same `docs/kb/`: the application's bugs, the traps of the platforms it
runs on and how they were solved, so the next person or coding agent who meets a symptom finds the answer instead of
rediscovering it. It also carries bug *classes* between repositories, so a bug fixed in one application is looked for,
and tracked, in all of them. Everything in `docs/kb/shared/`, this file included, is identical in every repository:
edit it in one, then `kb.py sync` it to the others (`kb.py lint` warns about an edit that was not synced).

## Records

* An **issue** (`issues/<PREFIX>-NNN-title.md`) belongs to one repository: a bug, a gotcha (a platform trap the code
  must live with), a design note (a choice that looks like a bug) or a question for the maintainers. Its front matter
  comes first and holds what matters: `status`; `action` (`fix`, `decide`, `verify`, `review`; derived from type and
  status when empty); `next`, one line with the change to make, the question, or what to verify; `where`, the
  file:line and the commit the line numbers refer to. The body (Symptom, Cause, Fix, Test, Log) is for whoever works
  on that issue. Allowed values of every field: `shared/vocabulary.md`.
* A **pattern** (`shared/patterns/PAT-NNN-title.md`) is a class of defect that can occur in any application, written
  for someone who does not know where it was found: the defect, why it happens, how to check a repository (its main
  grep is the `check:` line), how to fix and how to test it, and a `reference_fix` to copy.
* The **ledger** (`ledger.md`) has one row per pattern: `unchecked`, `affected`, `fixed`, `not-affected` or `n/a`,
  the date and commit that were checked, and the evidence. An `affected` or `fixed` row names the local issue, and the
  issue names the pattern (`pattern:`).

`INDEX.md` is generated from them (`kb.py index`): open issues ordered by action, then notes, closed issues, patterns
and open issues per platform. Platforms have no files of their own: issues and patterns carry `platforms:` tags
(`kb.py list --platform android [--patterns]`; a new platform is a new row in `vocabulary.md`). `kb.conf` names the
repository, its issue prefix and the files it shares with other repositories. `shared/boinc-api.md` holds the BOINC
API facts the code depends on, read in the BOINC source.

## Reading and writing it cheaply

A coding agent starts with `kb.py list --open --action fix` (one line per issue with its next step; `INDEX.md` says
the same for people on GitHub, at three times the size), then one issue file, then the pattern it links (its "How to
fix"), then `boinc-api.md` only for the facts that pattern cites. `kb.py list` filters by `--action`, `--platform`,
`--area` (comma-separated) and `--pattern`.

Bookkeeping does not need the files opened: `kb.py set` changes front-matter fields and appends to the Log, and
`kb.py ledger` writes a ledger row with today's date and HEAD. Both keep the ledger, the issues' `pattern:` links and
`INDEX.md` consistent. Open a file only to write its Symptom, Cause, Fix or Test.

**Work by pattern, not repository by repository.** Most open issues are instances of a pattern that several
repositories share. `kb.py todo ../kampylos ../phygas ...` lists the open issues of every repository grouped by
pattern, with each next step and the pattern's `reference_fix`: read the pattern and the reference fix once, then make
the same fix in each repository. `kb.py check PAT-NNN ../kampylos ../phygas ...` is the first pass of a pattern
everywhere at once: each repository's ledger row and the hits of the pattern's `check:` grep.

`AGENTS.md`, read at the start of every session, says what stays true: rules, contracts, gotchas and design notes,
which it may cite by id. It does not list open issues, which stop being true the day they are fixed; it points to
`kb.py list` instead, and `kb.py lint` warns when it cites an open issue (an error with `--strict`).

## Workflow for an incoming report

1. **Record it at once:** `kb.py new issue "symptom in a few words" next="what to verify"`. Status `new`. Fill in
   Symptom with the exact text the reporter gave (task stderr, error message, client version, platform) and
   `source:` (forum thread, task page, audit, maintainer). An incomplete record is better than none.
2. **Search before debugging:** `grep -ril "<key words>" docs/kb ../*/docs/kb/issues` (this knowledge base and the
   other repositories' issues, without five more copies of the shared files) and `kb.py list --patterns --area
   <area>`. A known pattern is linked (`kb.py set PFX-NNN pattern=PAT-NNN`); its "How to fix" is the starting point.
3. **Confirm:** reproduce it, or read the code at a named commit. Fill in Cause with file:line and `where:` with that
   commit; status `confirmed`, `next:` the change to make. A fix that needs a maintainer's answer gets `action:
   decide` and the question in `next:` (or a `type: question` issue in `blocked_by:`). Otherwise `not-a-bug` with the
   reason, or `duplicate` naming the other issue.
4. **Is it a class?** If the cause is not specific to this application (a BOINC API rule, a platform or toolchain
   trap, a mistake copied code could repeat), link the existing pattern or write one: `kb.py new pattern "..."
   --siblings ../other ../repos` picks a number that is free everywhere. Write "How to check a repository" so that it
   works without knowing this one (grep commands, what a hit must look like) and put the main grep into `check:`
   (the arguments of `git grep -nE`).
5. **Fix** on a branch, one fix per commit, with a test that fails before the fix where possible. The commit message
   names the issue (`KB: PLA-004`) and the pattern if any (`Pattern: PAT-001`). Then `kb.py set PLA-004
   status=fix-proposed`, and once merged upstream `status=fixed fixed_in=<commit> --log "..."`, which turns the
   pattern's ledger row to `fixed` when every issue it lists is fixed. Write Fix and Test for the next reader: what
   changed, why this way, what was rejected, what it changes for volunteers. If the pattern has no `reference_fix:`
   yet, set it (`kb.py set PAT-001 reference_fix=planodia@<commit>`, then sync) and make the same fix in the other
   repositories it affects (`kb.py todo`).
6. **Propagate** a new or changed pattern: `kb.py sync ../kampylos ../phygas ...` from the repository where it was
   written. Every other repository gets the file and an `unchecked` ledger row.
7. **Check another repository for a pattern:** `kb.py check PAT-NNN ../repos ...` shows where to look; follow the
   pattern's "How to check a repository" and record the result with that repository's copy of the tool:
   `python3 ../phygas/docs/kb/shared/kb.py ledger PAT-NNN not-affected --evidence "file:line, or why it does not
   apply"`. If affected, create the local issue there (`kb.py new issue "..." --pattern PAT-NNN area=... next=...`),
   record the row with `--issue` and the place in one clause, and fix it there (step 5).
8. **Before every pull request:** `kb.py index` and `kb.py lint --strict` (fails on errors, on unchecked patterns and
   on open issues cited in `AGENTS.md`). `kb.py status ../other ../repos` prints the cross-repository matrix.

## Files copied between repositories

Some source files exist as copies in several repositories (`cl_dynload.c`, the orbit core of two applications). Each
repository lists its copies in `kb.conf` (`shared-code = cl_dynload.c src/cl_dynload.c`), and `kb.py status` prints
`DRIFT` with the date of each copy's last change when copies differ. A drift is an intended local difference (write a
design note in the repository that differs) or a fix not copied yet (record and fix it like any other issue).

## Rules

* **Everything here is public.** No secrets, keys, server addresses, host names, user names, e-mail or IP addresses,
  private repository names or volunteers' data in an issue or a pattern; refer to a report by forum thread or task
  id. Shared files must make sense without any private repository: a pattern found in code that is not public
  describes the defect in general terms, with `origin:` "not public". `kb.py status`, `todo` and `check` output may
  name repositories that are not public: never paste it into a public place.
* **One fact, one place.** Link a pattern instead of copying its text into issues. Do not hand-edit `INDEX.md` or
  `shared/MANIFEST`.
* **Never delete or renumber** an issue or a pattern: other repositories, commits and pull requests refer to the ids.
  Close an issue with a status; retire a pattern with `status: retired` and a line saying why.
* **Measured, dated claims.** "Verified" means read at a named commit or reproduced; say which. Line numbers always
  come with the commit they refer to (`where:`, the ledger's Commit). Log entries are dated.

## Commands

Each command's `--help` says more; `sync -h` and `init -h` explain sharing and adding a repository.

```
python3 docs/kb/shared/kb.py list [--open] [--action fix] [--platform windows] [--area checkpoint,io] [--patterns]
python3 docs/kb/shared/kb.py new issue "Title" [--pattern PAT-001] [--type gotcha] [area=io severity=low next=...]
python3 docs/kb/shared/kb.py new pattern "Title" --siblings ../kampylos ../phygas ...
python3 docs/kb/shared/kb.py set PLA-004 key=value ... [--log "text"]          (lists: platforms=windows,mingw)
python3 docs/kb/shared/kb.py ledger PAT-001 STATUS --evidence "..." [--issue PLA-004] [--commit HEAD]
python3 docs/kb/shared/kb.py todo ../kampylos ../phygas ... [--action fix]
python3 docs/kb/shared/kb.py check PAT-001 ../kampylos ../phygas ... [--limit 8]
python3 docs/kb/shared/kb.py index && python3 docs/kb/shared/kb.py lint [--strict]
python3 docs/kb/shared/kb.py sync ../kampylos ../phygas ... [--dry-run]
python3 docs/kb/shared/kb.py status ../kampylos ../phygas ...
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
