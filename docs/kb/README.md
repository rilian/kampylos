# Knowledge base

Known bugs, platform traps and their solutions for this repository, and the bug patterns shared with the other
bitboinc applications.

* **What to work on:** `kb.py list --open --action fix` (below), one line per issue with its next step. The same
  for reading on GitHub: [INDEX.md](INDEX.md), whose first table, "Fix next", is followed by what waits for a
  maintainer decision, what must be verified, and fixes waiting for merge.
* **Across all the repositories:** `kb.py todo` groups the open issues of every repository checked out next to
  this one by shared pattern, so one fix is read once and made everywhere (`kb.py todo --pattern PAT-NNN`).
* **A report came in or something broke:** [shared/PROCESS.md](shared/PROCESS.md#workflow-for-an-incoming-report).
* **BOINC API behaviour the code relies on:** [shared/boinc-api.md](shared/boinc-api.md).
* **Bug classes from all the bitboinc applications:** [shared/patterns/](shared/patterns), with this repository's
  result for each in [ledger.md](ledger.md).

```
python3 docs/kb/shared/kb.py list --open --action fix     # what is ready to fix, with its next step
python3 docs/kb/shared/kb.py new issue "symptom in a few words" [area=... next=...]
python3 docs/kb/shared/kb.py set ABC-001 status=fixed fixed_in=<commit> --log "..."   # ledger and INDEX follow
python3 docs/kb/shared/kb.py index && python3 docs/kb/shared/kb.py lint --strict
```
