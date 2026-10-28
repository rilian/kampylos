# How the knowledge base works

Every bitboinc application repository has the same `docs/kb/` directory. It records what is known about the
application's bugs, the traps of each platform it runs on, and how they were solved, so the next person (or coding
agent) who meets the same symptom finds the answer instead of rediscovering it. It also carries bug *classes* from one
repository to the others, so a bug fixed in one application is looked for, and tracked, in every other one.

This file and everything else in `docs/kb/shared/` is identical in every repository. Change it in one repository and
copy it to the others with `kb.py sync` (below); `kb.py lint` warns when the local copy was edited without a sync.

## Layout

```
docs/kb/
  README.md          this repository's entry page (short; points here)
  INDEX.md           generated: what to work on (fix, decide, verify, review), notes, closed, patterns, platforms
  kb.conf            repository name, issue id prefix, files copied from or to other repositories
  ledger.md          one row per cross-repository pattern: checked at which commit, result, evidence
  issues/            one file per issue of this repository: <PREFIX>-NNN-short-title.md
  shared/            identical in every repository
    PROCESS.md       this file
    vocabulary.md    allowed values of type, status, severity, platforms, area, ledger status
    boinc-api.md     BOINC API behaviour the applications depend on, checked against the BOINC source
    patterns/        cross-repository bug classes: PAT-NNN-short-title.md
    templates/       issue.md, pattern.md, ledger.md, README.md
    kb.py            the tool (Python 3.8+, standard library only)
    .gitignore       keeps Python cache files out of git
    MANIFEST         sha256 of the shared files at the last sync
```

There are two kinds of record:

* An **issue** belongs to one repository: a bug, a gotcha (a platform trap the code must live with), a design note
  (a choice that looks like a bug), or a question for the maintainers. Its id carries the repository's prefix
  (`kb.conf`), for example `PLA-004`.
* A **pattern** is a class of defect that can occur in any of the applications, written so that someone who does not
  know the repository it was found in can check another one: what goes wrong, why, *how to check a repository*, how
  to fix it, how to test it. Its id is `PAT-NNN` and it is the same in every repository.

The **ledger** connects them: each repository has one row per pattern saying whether it was checked, at which commit,
with what result (`affected`, `fixed`, `not-affected`, `n/a`, or still `unchecked`) and the evidence. An affected
repository also has a local issue for it, linked both ways (`pattern:` in the issue, the issue id in the ledger row).

Front matter is one `key: value` per line; a value may contain `#`, but two or more spaces followed by `#` start a
comment (the hints in the templates).

Every open issue says what has to happen next: `action:` (`fix`, `decide`, `verify`, `review`; derived from type and
status when empty) and `next:`, one line with the change to make, the question for the maintainers, or what to
verify. `INDEX.md` is ordered by that, so its first table is the work that is ready to be done.

Platform knowledge is not kept in a separate file per platform: every issue and pattern carries `platforms:` tags.
`INDEX.md` lists the open issues per platform, and `kb.py list --platform android` (add `--patterns` for the
patterns) prints everything tagged with a platform, one line each. To add a platform, add a row to `vocabulary.md`.

## Reading and writing it cheaply

A coding agent starts with `kb.py list --open --action fix` (one line per issue with its next step; `INDEX.md` says
the same for people on GitHub, at three times the size), then one issue file, then the pattern it links (its "How
to fix"), then `boinc-api.md` only for the facts that pattern cites. `kb.py list` filters by `--action`,
`--platform`, `--area` and `--pattern`. Issue files start with the facts that matter (front matter: status, action,
next, where); the body is for whoever works on that issue.

Bookkeeping does not need the files opened: `kb.py set` changes front-matter fields and appends to the Log, and
`kb.py ledger` writes a ledger row (date and commit filled in). Both keep the ledger, the issues' `pattern:` links
and `INDEX.md` consistent. Open a file only to write its Symptom, Cause, Fix or Test.

**Work by pattern, not repository by repository.** Most open issues are instances of a pattern that several
repositories share. `kb.py todo ../kampylos ../phygas ...` lists the open issues of every repository grouped by
pattern, with each one's next step and the pattern's `reference_fix`: read the pattern and the reference fix once,
then make the same fix in each repository. `kb.py check PAT-NNN ../kampylos ../phygas ...` does the first pass of a
pattern everywhere at once: the ledger row of each repository and the hits of the pattern's `check:` grep.

`AGENTS.md` (read at the start of every session) describes what stays true: rules, contracts, gotchas and design
notes, which it may cite by id. It does not list open issues; they stop being true the day they are fixed, so it
points to `kb.py list` instead. `kb.py lint` warns when `AGENTS.md` cites an open issue (an error with `--strict`).

## Workflow for an incoming report

1. **Record it at once** with `kb.py new issue "symptom in a few words"`. Status `new`. Fill in Symptom with the exact
   text the reporter gave (task stderr, error message, client version, platform) and `source:` (forum thread number,
   task page, audit, maintainer), and `next:` (what to verify). No user names, host names, e-mail or IP addresses: see
   Rules. An incomplete record is better than none; `INDEX.md` shows `new` issues under "Verify first".
2. **Search before debugging.** `grep -ril "<key words>" docs/kb ../*/docs/kb/issues` (this repository's knowledge
   base, shared files included, and the other repositories' issues, without five more copies of the shared files),
   and `kb.py list --patterns --area <area>` for the patterns of the area. If it is a known pattern, link it
   (`kb.py set PFX-NNN pattern=PAT-NNN`), and the pattern's "How to fix" is the starting point.
3. **Confirm.** Reproduce it, or confirm it by reading the code at a named commit. Fill in Cause with file:line and
   `where:` with the commit those line numbers refer to. Status `confirmed`, and `next:` the change to make. If the fix
   needs a maintainer's answer, `action: decide` with the question in `next:` (or a separate `type: question` issue
   in `blocked_by:`). If it is not a bug: `not-a-bug` with the reason; if it is a duplicate: `duplicate`, naming the
   other issue.
4. **Ask: is this a class?** If the cause is not specific to this application (a BOINC API rule, a platform or
   toolchain trap, a mistake that copied code could repeat), make it a pattern, or link the existing one:
   `kb.py new pattern "..." --siblings ../other ../repos` picks a free number everywhere. Write "How to check a
   repository" so that it can be done without knowing this one: grep commands and what a hit must look like. Put
   the main grep into the front matter as `check:` (the arguments of `git grep -nE`), which `kb.py check` runs.
5. **Fix** on a branch, one fix per commit, with a test that fails before the fix where that is possible. The commit
   message names the issue (`KB: PLA-004`) and the pattern if any (`Pattern: PAT-001`). Status `fix-proposed`, then
   `fixed` once it is merged upstream: `kb.py set PLA-004 status=fixed fixed_in=<commit> --log "..."`, which also
   turns the pattern's ledger row to `fixed` when every issue it lists is fixed. Write the Fix and Test sections for
   the next reader: what was changed, why this way, what was rejected, what it changes for volunteers. If the
   pattern has no `reference_fix:` yet, set it (`kb.py set PAT-001 reference_fix=planodia@<commit>`, then sync), and
   make the same fix in the other repositories the pattern affects (`kb.py todo`).
6. **Propagate.** Run `kb.py sync ../kampylos ../phygas ...` from the repository where the pattern was written. Every
   other repository gets the pattern file and an `unchecked` ledger row. Check each one (step 7). The pattern's
   `reference_fix:` names the commit others can copy.
7. **Check another repository for a pattern**: `kb.py check PAT-NNN ../kampylos ../phygas ...` shows where to look
   in every repository at once; follow the pattern's "How to check a repository", then record the result with that
   repository's copy of the tool: `python3 ../phygas/docs/kb/shared/kb.py ledger PAT-NNN not-affected --evidence
   "file:line, or why it does not apply"` (Checked and Commit are filled in from today and HEAD). If affected, create
   the local issue there (`kb.py new issue "..." --pattern PAT-NNN area=... next=...`, step 1), record the row with
   `--issue`, and fix it there (step 5).
8. **Before every pull request**: `kb.py index` and `kb.py lint --strict` (fails on errors and on unchecked patterns).
   `kb.py status ../other ../repos` prints the cross-repository matrix.

## Files copied between repositories

Some source files exist as copies in several repositories (for example `cl_dynload.c` and the orbit core shared by
two applications). A fix to one copy has to reach the others. Each repository lists its copies in `kb.conf`:

```
shared-code = cl_dynload.c src/cl_dynload.c
```

`kb.py status ../other ../repos` compares the copies with the same name and prints `DRIFT` with the date of the last
change of each copy when they differ. A drift is either an intended local difference (write it down as a design note
in the repository that differs) or a fix that has not been copied yet (record and fix it like any other issue).

## Rules

* **Everything here is public.** The application repositories are public. Do not write secrets, keys, server
  addresses, host names, user names, e-mail or IP addresses, private repository names or volunteers' data into an
  issue or a pattern. Refer to a report by forum thread number or task id. Shared files must make sense without any
  private repository: if a pattern was found in code that is not public, describe the defect in general terms and
  leave `origin:` as "not public".
* **One fact, one place.** Do not copy a pattern's text into issues: link it. Do not hand-edit `INDEX.md` or
  `shared/MANIFEST`.
* **Never delete or renumber** an issue or a pattern: other repositories, commits and pull requests refer to the ids.
  Close an issue with a status; retire a pattern with `status: retired` and a line saying why.
* **Measured, dated claims.** "Verified" means read at a named commit or reproduced; say which. Date the entries in
  an issue's Log.
* **Line numbers go stale.** Always give the commit they refer to (`where:` in an issue, Commit in the ledger).
* `kb.py status`, `todo` and `check` output may name repositories that are not public. Do not paste it into a public
  place.

## Commands

```
python3 docs/kb/shared/kb.py lint [--strict]
python3 docs/kb/shared/kb.py index
python3 docs/kb/shared/kb.py list [--open] [--action fix] [--platform windows] [--area checkpoint] [--patterns]
python3 docs/kb/shared/kb.py new issue "Title" [--pattern PAT-001] [--type gotcha] [area=io severity=low next=...]
python3 docs/kb/shared/kb.py new pattern "Title" --siblings ../kampylos ../phygas ...
python3 docs/kb/shared/kb.py set PLA-004 key=value ... [--log "text"]          (lists: platforms=windows,mingw)
python3 docs/kb/shared/kb.py ledger PAT-001 STATUS --evidence "..." [--issue PLA-004] [--commit HEAD]
python3 docs/kb/shared/kb.py todo ../kampylos ../phygas ... [--action fix]
python3 docs/kb/shared/kb.py check PAT-001 ../kampylos ../phygas ... [--limit 8]
python3 docs/kb/shared/kb.py sync ../kampylos ../phygas ... [--dry-run]
python3 docs/kb/shared/kb.py status ../kampylos ../phygas ...
python3 docs/kb/shared/kb.py init --name NAME --prefix ABC        (new repository, after a sync into it)
```

`sync` works out which copy of each shared file is newest by comparing it with each repository's `MANIFEST`: a file
that changed since its repository's last sync wins. If the same file was changed differently in two repositories,
sync stops without writing anything and names the file; merge it by hand in one repository and sync again. Run it
from the repository with the newest edits, with all the others as arguments, and commit `docs/kb` in each of them.

Adding a repository: `python3 ../planodia/docs/kb/shared/kb.py sync ../newrepo` copies the shared files into it, then
`python3 docs/kb/shared/kb.py init --name newrepo --prefix NEW` there creates `kb.conf`, `ledger.md` (every pattern
`unchecked`), `README.md` and `INDEX.md`. Then check the patterns.

## Issue id prefixes in use

| Repository | Prefix |
|---|---|
| kampylos | KAM |
| nostos | NOS |
| phygas | PHY |
| planodia | PLA |
| yarrow | YAR |

Prefixes of repositories that are not public are not listed here; pick one that is not in this table.
