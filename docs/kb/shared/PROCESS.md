# How to work with the knowledge base

Every bitboinc application repository has the same `docs/kb/`: the application's bugs, the traps of the platforms it
runs on and how they were solved, so the next person or coding agent who meets a symptom finds the answer instead of
rediscovering it. It also carries bug *classes* between repositories, so a bug fixed in one application is looked for,
and tracked, in all of them.

This file is the procedure. [REFERENCE.md](REFERENCE.md) describes the records and their fields, the files copied
between repositories and every command. Everything in `docs/kb/shared/`, these two files included, is identical in
every repository: edit it in one, then `kb.py sync` it to the others (`kb.py lint` warns about an edit that was not
synced).

The records, in short:

* An **issue** (`issues/<PREFIX>-NNN-title.md`) is one repository's bug, gotcha, design note or question. Its front
  matter says what to do next (`action`, `next`) and where (`where`: file:line @ commit); its body (Symptom, Cause,
  Fix, Test, Log) is for whoever works on it.
* A **pattern** (`shared/patterns/PAT-NNN-title.md`) is a class of defect that any application can have: how to find
  it (`check:`, a grep), how to fix and test it, and a `reference_fix` to copy.
* The **ledger** (`ledger.md`) has one row per pattern: whether this repository is affected, at which commit that was
  checked, and the evidence.

## Finding work and reading it

* **One repository:** `kb.py list --open --action fix` prints one line per issue with its next step. Then
  `kb.py show PFX-NNN` prints the issue and `kb.py show PAT-NNN` the pattern it links, followed by the sections of
  `boinc-api.md` that the pattern cites, so that file is not read whole.
* **Work by pattern, not repository by repository.** Most open issues are instances of a pattern that several
  repositories share. `kb.py todo` reads every repository checked out next to this one and prints one line per
  pattern; `kb.py todo --pattern PAT-NNN` adds each issue's next step and `where:`. Read the pattern and its
  `reference_fix` once, then make the same fix in each repository.
* **Code:** `where:` and the issue's Cause name the lines. Read those functions rather than whole files.
* **Other repositories:** read them through the shell: `kb.py show`, `git -C ../<repo> grep -n`, `sed -n`. Opening
  one of their files in an editor tool can also load that repository's whole AGENTS.md into the session; do that only
  in the repository you change.
* **Bookkeeping** needs no file opened: `kb.py set` changes front-matter fields and appends to the Log, and
  `kb.py ledger` writes a ledger row; both keep the ledger, the issues' `pattern:` links and `INDEX.md` consistent.
  Open an issue only to write its Symptom, Cause, Fix or Test.
* **AGENTS.md** states the known defects of the code, each with its issue id. When an issue's status changes,
  `kb.py set` prints the passages of `AGENTS.md` that cite it: make them true again in the same commit.

## Workflow

From a report to a fix in every repository that has the bug. A fix picked from `kb.py list` or `kb.py todo` starts
at step 5.

1. **Record it at once:** `kb.py new issue "symptom in a few words" area=<area> next="what to verify"` (without
   `area=` it prints the allowed values). Status `new`. Fill in Symptom with the exact text the reporter gave (task
   stderr, error message, client version, platform) and `source:` (forum thread, task page, audit, maintainer). An
   incomplete record is better than none.
2. **Search before debugging:** `grep -ril "<key words>" ../*/docs/kb/issues docs/kb/shared/patterns` (the issues of
   every repository, this one included, and the patterns once), then `kb.py show` the ids it finds; and
   `kb.py list --patterns --area <area>`. A known pattern is linked (`kb.py set PFX-NNN pattern=PAT-NNN`); its
   "How to fix" is the starting point.
3. **Confirm:** reproduce it, or read the code at a named commit. Fill in Cause with file:line, and `where:` with the
   place the fix changes and that commit; status `confirmed`, `next:` the change to make. A fix that needs a
   maintainer's answer gets `action: decide` and the question in `next:` (or a `type: question` issue in
   `blocked_by:`). Otherwise `not-a-bug` with the reason, or `duplicate` naming the other issue.
4. **Is it a class?** If the cause is not specific to this application (a BOINC API rule, a platform or toolchain
   trap, a mistake copied code could repeat), link the existing pattern or write one: `kb.py new pattern "..."` picks
   a number that is free in every repository next to this one. Write "How to check a repository" so that it works
   without knowing this one (grep commands, what a hit must look like) and put the main grep into `check:` (the
   arguments of `git grep -nE`).
5. **Fix** on a branch, one fix per commit, with a test that fails before the fix where possible. The commit message
   names the issue (`KB: PLA-004`) and the pattern if any (`Pattern: PAT-001`).
6. **Record the fix:** `kb.py set PLA-004 status=fix-proposed --log "..."`, and once it is merged upstream
   `status=fixed fixed_in=<commit>`, which turns the pattern's ledger row to `fixed` when every issue it lists is
   fixed. Update the passages of `AGENTS.md` that `set` prints. Write Fix and Test for the next reader: what changed,
   why this way, what was rejected, what it changes for volunteers. If the pattern has no `reference_fix:` yet, set
   it (`kb.py set PAT-001 reference_fix=planodia@<commit>`, then sync) and make the same fix in the other
   repositories it affects (`kb.py todo --pattern PAT-001`).
7. **Propagate** a new or changed pattern: `kb.py sync ../kampylos ../phygas ...` from the repository where it was
   written. Every other repository gets the file and an `unchecked` ledger row.
8. **Check the other repositories for a pattern:** `kb.py check PAT-NNN` prints each repository's ledger row and the
   hits of the pattern's `check:` grep. Follow its "How to check a repository", reading through the shell, and record
   the result with that repository's copy of the tool:
   `python3 ../phygas/docs/kb/shared/kb.py ledger PAT-NNN not-affected --evidence "file:line, or why it does not
   apply"`. If it is affected, create the issue with that same copy
   (`python3 ../phygas/docs/kb/shared/kb.py new issue "..." --pattern PAT-NNN area=... next=...`), set the row to
   `affected` with `--issue` and the place in one clause, and fix it there (steps 5 and 6).
9. **Before every pull request:** `kb.py index` and `kb.py lint --strict` (fails on errors and on unchecked
   patterns). `kb.py status` prints the cross-repository matrix.

## Rules

* **Everything here is public.** No secrets, keys, server addresses, host names, user names, e-mail or IP addresses,
  private repository names or volunteers' data in an issue or a pattern; refer to a report by forum thread or task
  id. Shared files must make sense without any private repository: a pattern found in code that is not public
  describes the defect in general terms, with `origin:` "not public". The output of `kb.py status`, `todo`, `check`
  and `show` may name or quote repositories that are not public: never paste it into a public place.
* **One fact, one place.** Link a pattern instead of copying its text into issues. Do not hand-edit `INDEX.md` or
  `shared/MANIFEST`.
* **Never delete or renumber** an issue or a pattern: other repositories, commits and pull requests refer to the ids.
  Close an issue with a status; retire a pattern with `status: retired` and a line saying why.
* **Measured, dated claims.** "Verified" means read at a named commit or reproduced; say which. Line numbers always
  come with the commit they refer to (`where:`, the ledger's Commit). Log entries are dated.
