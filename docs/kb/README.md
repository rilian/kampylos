# Knowledge base

Known bugs, platform traps and their solutions for this repository, and the bug patterns shared with the other
bitboinc applications.

* **What to work on:** [INDEX.md](INDEX.md). Its first table, "Fix next", lists what is ready to fix, with the next
  step for each issue; then what waits for a maintainer decision, what must be verified, and fixes waiting for merge.
* **A report came in or something broke:** [shared/PROCESS.md](shared/PROCESS.md#workflow-for-an-incoming-report).
* **BOINC API behaviour the code relies on:** [shared/boinc-api.md](shared/boinc-api.md).
* **Bug classes from all the bitboinc applications:** [shared/patterns/](shared/patterns), with this repository's
  result for each in [ledger.md](ledger.md).

```
python3 docs/kb/shared/kb.py list --open                  # one line per open issue, with its next step
python3 docs/kb/shared/kb.py new issue "symptom in a few words"
python3 docs/kb/shared/kb.py index && python3 docs/kb/shared/kb.py lint --strict
```
