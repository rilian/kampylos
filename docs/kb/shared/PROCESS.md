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

## Reading it cheaply

For a person or a coding agent the order is: `INDEX.md` (what to work on), then one issue file, then the pattern it
links (its "How to fix"), then `boinc-api.md` only for the facts that pattern cites. `kb.py list` prints one line per
issue with filters (`--open`, `--action fix`, `--platform windows`, `--area checkpoint`, `--pattern PAT-003`), which is
cheaper than opening files. Issue files start with the facts that matter (front matter: status, action, next, where);
the body is for whoever works on that issue.

## Workflow for an incoming report

1. **Record it at once** with `kb.py new issue "symptom in a few words"`. Status `new`. Fill in Symptom with the exact
   text the reporter gave (task stderr, error message, client version, platform) and `source:` (forum thread number,
   task page, audit, maintainer), and `next:` (what to verify). No user names, host names, e-mail or IP addresses: see
   Rules. An incomplete record is better than none; `INDEX.md` shows `new` issues under "Verify first".
2. **Search before debugging.** `grep -ril "<key words>" docs/kb` here and in the sibling repositories, and look at the
   patterns for the area and platform. If it is a known pattern, link it (`pattern:`), and the pattern's "How to fix"
   is the starting point.
3. **Confirm.** Reproduce it, or confirm it by reading the code at a named commit. Fill in Cause with file:line and
   `where:` with the commit those line numbers refer to. Status `confirmed`, and `next:` the change to make. If the fix
   needs a maintainer's answer, `action: decide` with the question in `next:` (or a separate `type: question` issue
   in `blocked_by:`). If it is not a bug: `not-a-bug` with the reason; if it is a duplicate: `duplicate`, naming the
   other issue.
4. **Ask: is this a class?** If the cause is not specific to this application (a BOINC API rule, a platform or
   toolchain trap, a mistake that copied code could repeat), make it a pattern, or link the existing one:
   `kb.py new pattern "..." --siblings ../other ../repos` picks a free number everywhere. Write "How to check a
   repository" so that it can be done without knowing this one: grep commands and what a hit must look like.
5. **Fix** on a branch, one fix per commit, with a test that fails before the fix where that is possible. The commit
   message names the issue (`KB: PLA-004`) and the pattern if any (`Pattern: PAT-001`). Status `fix-proposed`, then
   `fixed` with `fixed_in:` once it is merged upstream. Write the Fix and Test sections for the next reader: what was
   changed, why this way, what was rejected, what it changes for volunteers.
6. **Propagate.** Run `kb.py sync ../kampylos ../phygas ...` from the repository where the pattern was written. Every
   other repository gets the pattern file and an `unchecked` ledger row. Check each one (step 7). The pattern's
   `reference_fix:` names the commit others can copy.
7. **Check another repository for a pattern**: follow the pattern's "How to check a repository", then fill in that
   repository's ledger row: Status, Checked (date), Commit (`git rev-parse --short HEAD` of what you read), Issue,
   Evidence (file:line, or why it does not apply). If affected, create the local issue there (step 1, with
   `--pattern`) and fix it there (step 5).
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
* `kb.py status` output may name repositories that are not public. Do not paste it into a public place.

## Commands

```
python3 docs/kb/shared/kb.py lint [--strict]
python3 docs/kb/shared/kb.py index
python3 docs/kb/shared/kb.py list [--open] [--action fix] [--platform windows] [--area checkpoint] [--patterns]
python3 docs/kb/shared/kb.py new issue "Title" [--pattern PAT-001] [--type gotcha]
python3 docs/kb/shared/kb.py new pattern "Title" --siblings ../kampylos ../phygas ...
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
