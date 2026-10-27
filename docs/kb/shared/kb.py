#!/usr/bin/env python3
"""Knowledge-base tool shared by the bitboinc application repositories.

This file lives in docs/kb/shared/ and is identical in every repository (see PROCESS.md).
Standard library only, Python 3.8 or newer.

    python3 docs/kb/shared/kb.py lint [--strict]      check this repository's knowledge base
    python3 docs/kb/shared/kb.py index                rewrite docs/kb/INDEX.md
    python3 docs/kb/shared/kb.py list [--open] [--action fix] [--platform windows] [--area checkpoint] [--patterns]
    python3 docs/kb/shared/kb.py new issue "Title" [--pattern PAT-001] [--type bug]
    python3 docs/kb/shared/kb.py new pattern "Title" [--siblings ../repo ...]
    python3 docs/kb/shared/kb.py sync ../repo [../repo ...] [--dry-run]
    python3 docs/kb/shared/kb.py status ../repo [../repo ...]
    python3 docs/kb/shared/kb.py init --name NAME --prefix ABC

sync makes docs/kb/shared/ identical in this repository and the given ones and adds an
"unchecked" ledger row for every pattern a repository has not looked at yet. status prints a
cross-repository report (pattern ledger matrix, drift of files copied between repositories,
open issues); it is printed only, never written into a repository.
"""
import argparse
import datetime
import hashlib
import re
import shutil
import subprocess
import sys
from pathlib import Path

SHARED = Path(__file__).resolve().parent          # docs/kb/shared
KB = SHARED.parent                                # docs/kb
ROOT = KB.parent.parent                           # repository root

ISSUE_FIELDS = ["id", "title", "type", "status", "severity", "platforms", "area", "action", "next", "blocked_by",
                "pattern", "aliases", "reported", "source", "where", "fixed_in"]
ISSUE_REQUIRED = ["id", "title", "type", "status", "severity", "platforms", "area", "reported"]
PATTERN_REQUIRED = ["id", "title", "status", "severity", "platforms", "area", "applies_to", "added"]
CLOSED = {"fixed", "wont-fix", "not-a-bug", "duplicate", "documented"}
SEVERITY_ORDER = ["critical", "high", "medium", "low", "info"]
LEDGER_COLUMNS = ["Pattern", "Status", "Checked", "Commit", "Issue", "Evidence"]
SEV_SHORT = {"critical": "crit", "high": "high", "medium": "med", "low": "low", "info": "info"}
ACTION_SECTIONS = [
    ("fix", "Fix next", "The cause is known and the fix is clear. Highest severity first."),
    ("decide", "Waiting for a maintainer decision",
     "Questions for the maintainers, and issues whose fix depends on an answer."),
    ("verify", "Verify first", "Reported or suspected but not confirmed: reproduce or measure before fixing."),
    ("review", "Fix written, not merged yet", "Review, rebase if needed, merge."),
]
MANIFEST = "MANIFEST"
DATE_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")


# ---------------------------------------------------------------- parsing

def parse_front_matter(path):
    """Return (fields, body) of a markdown file with a '---' front matter block."""
    text = path.read_text(encoding="utf-8")
    if not text.startswith("---\n"):
        raise ValueError("no front matter (the file must start with a '---' line)")
    end = text.find("\n---\n", 4)
    if end < 0:
        raise ValueError("front matter is not closed by a '---' line")
    fields = {}
    for n, line in enumerate(text[4:end].splitlines(), 2):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        m = re.match(r"^([a-z_]+):(.*)$", line)
        if not m:
            raise ValueError("line %d: expected 'key: value'" % n)
        value = re.sub(r"\s{2,}#(\s.*)?$", "", m.group(2)).strip()   # template hints: two or more spaces, then #
        if value.startswith("[") and value.endswith("]"):
            value = [v.strip() for v in value[1:-1].split(",") if v.strip()]
        fields[m.group(1)] = value
    return fields, text[end + 5:]


def action_of(issue):
    """The next action for an issue: its 'action' field, or what its type and status imply."""
    if issue.get("action"):
        return issue["action"]
    status = issue.get("status")
    if status in CLOSED:
        return "none"
    if issue.get("type") == "question":
        return "decide"
    return {"new": "verify", "fix-proposed": "review"}.get(status, "fix")


def as_list(value):
    if isinstance(value, list):
        return value
    return [value] if value else []


def parse_vocabulary(shared):
    """Allowed values per field, from the tables in vocabulary.md (first column, in backticks)."""
    vocab, field = {}, None
    for line in (shared / "vocabulary.md").read_text(encoding="utf-8").splitlines():
        m = re.match(r"^## `([a-z_]+)`", line)
        if m:
            field = m.group(1)
            vocab[field] = []
            continue
        m = re.match(r"^\| `([^`]+)` \|", line)
        if m and field:
            vocab[field].append(m.group(1))
    return vocab


def parse_conf(kb):
    conf = {"shared-code": []}
    path = kb / "kb.conf"
    if not path.exists():
        return None
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        key, _, value = (s.strip() for s in line.partition("="))
        if key == "shared-code":
            name, _, rel = value.partition(" ")
            conf["shared-code"].append((name.strip(), rel.strip()))
        else:
            conf[key] = value
    return conf


def split_row(line):
    return [c.strip() for c in line.strip().strip("|").split("|")]


def parse_ledger(kb):
    """Rows of ledger.md as dicts keyed by LEDGER_COLUMNS, in file order."""
    path = kb / "ledger.md"
    rows = []
    if not path.exists():
        return rows
    for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.startswith("| PAT-"):
            continue
        cells = split_row(line)
        cells += [""] * (len(LEDGER_COLUMNS) - len(cells))
        row = dict(zip(LEDGER_COLUMNS, cells))
        row["_line"] = n
        rows.append(row)
    return rows


class Repo:
    """One repository's knowledge base."""

    def __init__(self, kb):
        self.kb = Path(kb).resolve()
        self.root = self.kb.parent.parent
        self.shared = self.kb / "shared"
        self.conf = parse_conf(self.kb)
        self.name = (self.conf or {}).get("name") or self.root.name
        self.errors, self.warnings = [], []
        self.issues, self.patterns = {}, {}
        self.vocab = parse_vocabulary(self.shared) if (self.shared / "vocabulary.md").exists() else {}
        self._load()

    def err(self, where, msg):
        self.errors.append("%s: %s" % (where, msg))

    def warn(self, where, msg):
        self.warnings.append("%s: %s" % (where, msg))

    def rel(self, path):
        return str(Path(path).relative_to(self.root))

    def _load(self):
        for path in sorted((self.shared / "patterns").glob("PAT-*.md")):
            self._load_one(path, self.patterns)
        for path in sorted((self.kb / "issues").glob("*.md")):
            self._load_one(path, self.issues)
        self.ledger = parse_ledger(self.kb)

    def _load_one(self, path, into):
        try:
            fields, body = parse_front_matter(path)
        except ValueError as e:
            self.err(self.rel(path), str(e))
            return
        fields["_path"], fields["_body"] = path, body
        key = fields.get("id") or path.stem
        if key in into:
            self.err(self.rel(path), "duplicate id %s (also %s)" % (key, self.rel(into[key]["_path"])))
        into[key] = fields

    def ledger_row(self, pid):
        for row in self.ledger:
            if row["Pattern"] == pid:
                return row
        return None

    def active_patterns(self):
        return {k: v for k, v in self.patterns.items() if v.get("status") != "retired"}


# ---------------------------------------------------------------- lint

def check_values(repo, where, fields, names):
    for name in names:
        allowed = repo.vocab.get(name)
        if not allowed:
            continue
        for value in as_list(fields.get(name)):
            if value not in allowed:
                repo.err(where, "%s '%s' is not in vocabulary.md (allowed: %s)" % (name, value, ", ".join(allowed)))


def lint(repo, strict=False):
    if repo.conf is None:
        repo.err("docs/kb/kb.conf", "missing (run: kb.py init --name NAME --prefix ABC)")
        return
    prefix = repo.conf.get("prefix", "")
    if not re.match(r"^[A-Z]{2,5}$", prefix):
        repo.err("docs/kb/kb.conf", "prefix must be 2-5 capital letters")

    for pid, p in repo.patterns.items():
        where = repo.rel(p["_path"])
        for f in PATTERN_REQUIRED:
            if not p.get(f):
                repo.err(where, "missing field '%s'" % f)
        if not re.match(r"^PAT-\d{3}$", pid) or not p["_path"].name.startswith(pid + "-"):
            repo.err(where, "id must be PAT-NNN and the file name must start with it")
        check_values(repo, where, p, ["severity", "platforms", "area"])
        if p.get("status") not in ("active", "retired"):
            repo.err(where, "status must be active or retired")
        if p.get("added") and not DATE_RE.match(p["added"]):
            repo.err(where, "added must be YYYY-MM-DD")

    for iid, i in repo.issues.items():
        where = repo.rel(i["_path"])
        for f in ISSUE_REQUIRED:
            if not i.get(f):
                repo.err(where, "missing field '%s'" % f)
        for f in i:
            if not f.startswith("_") and f not in ISSUE_FIELDS:
                repo.err(where, "unknown field '%s' (known: %s)" % (f, ", ".join(ISSUE_FIELDS)))
        if not re.match(r"^%s-\d{3,}$" % re.escape(prefix), iid) or not i["_path"].name.startswith(iid + "-"):
            repo.err(where, "id must be %s-NNN and the file name must start with it" % prefix)
        check_values(repo, where, i, ["type", "status", "severity", "platforms", "area", "action"])
        if i.get("status") in CLOSED and i.get("action") not in (None, "", "none"):
            repo.err(where, "status %s is closed, so action must be none or empty" % i.get("status"))
        if action_of(i) != "none":
            if not i.get("next"):
                repo.err(where, "open issue needs 'next:', one line: the change to make, the question, what to verify")
            elif len(i["next"]) > 200:
                repo.warn(where, "'next:' is %d characters; keep it to one line (200 at most)" % len(i["next"]))
        for bid in as_list(i.get("blocked_by")):
            if bid not in repo.issues:
                repo.err(where, "blocked_by %s: no such issue" % bid)
        if i.get("reported") and not DATE_RE.match(i["reported"]):
            repo.err(where, "reported must be YYYY-MM-DD")
        if i.get("status") == "fixed" and not i.get("fixed_in"):
            repo.err(where, "status fixed needs fixed_in (commit or PR)")
        for pid in as_list(i.get("pattern")):
            if pid not in repo.patterns:
                repo.err(where, "pattern %s does not exist in docs/kb/shared/patterns" % pid)
            else:
                row = repo.ledger_row(pid)
                if not row or iid not in re.findall(r"[A-Z]+-\d+", row["Issue"]):
                    repo.err(where, "links %s but the ledger row for %s does not list %s" % (pid, pid, iid))

    lpath = "docs/kb/ledger.md"
    if not (repo.kb / "ledger.md").exists():
        repo.err(lpath, "missing")
    seen = set()
    allowed = repo.vocab.get("ledger", [])
    for row in repo.ledger:
        where = "%s:%d" % (lpath, row["_line"])
        pid, status = row["Pattern"], row["Status"]
        if pid in seen:
            repo.err(where, "second row for %s" % pid)
        seen.add(pid)
        if pid not in repo.patterns:
            repo.err(where, "unknown pattern %s" % pid)
            continue
        if allowed and status not in allowed:
            repo.err(where, "status '%s' not in vocabulary.md (allowed: %s)" % (status, ", ".join(allowed)))
        if status == "unchecked":
            (repo.err if strict else repo.warn)(where, "%s not checked in this repository yet" % pid)
            continue
        if not DATE_RE.match(row["Checked"]):
            repo.err(where, "Checked must be YYYY-MM-DD")
        if not re.match(r"^[0-9a-f]{7,40}$", row["Commit"]):
            repo.err(where, "Commit must be the commit that was checked (7+ hex digits)")
        if not row["Evidence"]:
            repo.err(where, "Evidence is empty: say what was looked at (file:line or why n/a)")
        ids = re.findall(r"[A-Z]+-\d+", row["Issue"])
        if status in ("affected", "fixed") and not ids:
            repo.err(where, "status %s needs a local issue in the Issue column" % status)
        for iid in ids:
            issue = repo.issues.get(iid)
            if not issue:
                repo.err(where, "issue %s does not exist" % iid)
            elif pid not in as_list(issue.get("pattern")):
                repo.err(where, "issue %s does not link %s (pattern: field)" % (iid, pid))
            elif status == "fixed" and issue.get("status") != "fixed":
                repo.err(where, "ledger says fixed but %s has status %s" % (iid, issue.get("status")))
    for pid in repo.active_patterns():
        if pid not in seen:
            repo.err(lpath, "no row for %s (run kb.py sync, or add an 'unchecked' row)" % pid)

    for name, rel in repo.conf["shared-code"]:
        if not (repo.root / rel).exists():
            repo.err("docs/kb/kb.conf", "shared-code %s: %s does not exist" % (name, rel))

    manifest = read_manifest(repo.shared)
    actual = hash_tree(repo.shared)
    if not manifest:
        repo.warn("docs/kb/shared", "no MANIFEST yet; run kb.py sync with the other repositories")
    elif manifest != actual:
        changed = sorted(set(manifest) ^ set(actual) | {p for p in manifest if p in actual and manifest[p] != actual[p]})
        more = " and %d more" % (len(changed) - 5) if len(changed) > 5 else ""
        repo.warn("docs/kb/shared", "differs from MANIFEST (%s%s); run kb.py sync with the other repositories"
                  % (", ".join(changed[:5]), more))

    index = repo.kb / "INDEX.md"
    if not index.exists() or index.read_text(encoding="utf-8") != render_index(repo):
        repo.err("docs/kb/INDEX.md", "out of date (run kb.py index)")


# ---------------------------------------------------------------- index

def sev_key(item):
    sev = item.get("severity", "info")
    return (SEVERITY_ORDER.index(sev) if sev in SEVERITY_ORDER else 9, item.get("id", ""))


def esc(text):
    return str(text).replace("|", "\\|")


def render_index(repo):
    """INDEX.md: what to work on, in order. Reference-style links keep the raw text short."""
    issues = sorted(repo.issues.values(), key=sev_key)
    by_action = {}
    for i in issues:
        by_action.setdefault(action_of(i), []).append(i)
    notes = [i for i in issues if i.get("status") == "documented"]
    closed = [i for i in issues if i.get("status") in CLOSED - {"documented"}]
    opened = [i for i in issues if action_of(i) != "none"]
    refs = set()

    def ref(iid):
        refs.add(iid)
        return "[%s]" % iid

    ledger = {pid: (repo.ledger_row(pid) or {}) for pid in repo.patterns}
    groups = {}
    for pid in sorted(repo.patterns):
        status = ledger[pid].get("Status", "unchecked") if repo.patterns[pid].get("status") != "retired" else "retired"
        groups.setdefault(status, []).append(pid)

    counts = ", ".join("%d %s" % (len(by_action.get(a, [])), a) for a, _, _ in ACTION_SECTIONS if by_action.get(a))
    out = ["# %s: knowledge base index" % repo.name, "",
           "%d issues: %d open (%s), %d note(s), %d closed. Shared patterns here: %d affected, %d fixed, %d clear, "
           "%d unchecked." % (len(issues), len(opened), counts or "none", len(notes), len(closed),
                              len(groups.get("affected", [])), len(groups.get("fixed", [])),
                              len(groups.get("not-affected", [])) + len(groups.get("n/a", [])),
                              len(groups.get("unchecked", []))), "",
           "Work from the top: pick an issue, read its file, and its pattern's \"How to fix\" if it has one. Filter in a",
           "terminal instead of reading everything: `python3 docs/kb/shared/kb.py list --open --platform windows`.",
           "Workflow: [shared/PROCESS.md](shared/PROCESS.md). Generated by `kb.py index`; do not edit by hand.", ""]

    for action, title, intro in ACTION_SECTIONS:
        rows = by_action.get(action, [])
        if not rows:
            continue
        out.extend(["## %s (%d)" % (title, len(rows)), "", intro, "",
                    "| ID | Sev | Area | Problem | Next step |", "|---|---|---|---|---|"])
        for i in rows:
            nxt = i.get("next", "")
            blocked = as_list(i.get("blocked_by"))
            if blocked:
                nxt += " (blocked by %s)" % ", ".join(ref(b) for b in blocked)
            pats = as_list(i.get("pattern"))
            if pats:
                nxt += " " + ", ".join(ref(p) for p in pats)
            out.append("| %s | %s | %s | %s | %s |" % (ref(i["id"]), SEV_SHORT.get(i.get("severity"), ""),
                                                     i.get("area", ""), esc(i.get("title", "")), esc(nxt.strip())))
        out.append("")

    if notes:
        out.extend(["## Know before changing (%d)" % len(notes), "",
                    "Gotchas and design notes: nothing to fix, but they explain why the code is the way it is.", "",
                    "| ID | Area | Platforms | Note |", "|---|---|---|---|"])
        for i in notes:
            out.append("| %s | %s | %s | %s |" % (ref(i["id"]), i.get("area", ""),
                                                ", ".join(as_list(i.get("platforms"))), esc(i.get("title", ""))))
        out.append("")

    if closed:
        out.extend(["## Closed (%d)" % len(closed), ""])
        for i in sorted(closed, key=lambda x: x["id"]):
            how = "fixed in %s" % i["fixed_in"] if i.get("status") == "fixed" else i.get("status", "")
            out.append("- %s %s: %s" % (ref(i["id"]), how, i.get("title", "")))
        out.append("")

    out.extend(["## Shared patterns in this repository", "",
                "Bug classes found in any bitboinc application ([shared/patterns](shared/patterns)). The issues above "
                "name the pattern they are an instance of; the evidence for every verdict is in [ledger.md](ledger.md).",
                ""])
    for status, label in (("affected", "Affected"), ("fixed", "Fixed here"), ("unchecked", "Unchecked")):
        pids = groups.get(status, [])
        out.append("- %s: %s" % (label, ", ".join(ref(p) for p in pids) if pids else "none"))
    clear = sorted(groups.get("not-affected", []) + groups.get("n/a", []))
    out.append("- Checked and clear (not affected, or not applicable): %s" % (", ".join(clear) or "none"))
    out.append("")

    plat = {}
    for i in opened:
        for p in as_list(i.get("platforms")):
            if p != "all":
                plat.setdefault(p, []).append(i["id"])
    if plat:
        out.extend(["## Open issues by platform", "", "Issues tagged `all` are not repeated here.", ""])
        for p in repo.vocab.get("platforms", []):
            if p in plat:
                out.append("- %s: %s" % (p, ", ".join(sorted(plat[p]))))
        out.append("")

    for key in sorted(refs):
        item = repo.issues.get(key) or repo.patterns.get(key)
        if item:
            out.append("[%s]: %s" % (key, Path(item["_path"]).relative_to(repo.kb).as_posix()))
    return "\n".join(out).rstrip("\n") + "\n"


def list_items(repo, args):
    """One line per issue (or pattern): a cheap overview for people and coding agents."""
    if args.patterns:
        for pid, p in sorted(repo.patterns.items()):
            if args.platform and args.platform not in as_list(p.get("platforms")):
                continue
            if args.area and p.get("area") != args.area:
                continue
            row = repo.ledger_row(pid) or {}
            print("%s  %-4s  %-12s  %-16s  %s  [%s]" % (pid, SEV_SHORT.get(p.get("severity"), ""),
                                                      row.get("Status", "unchecked"), p.get("area", ""),
                                                      p.get("title", ""), row.get("Issue", "")))
        return
    for i in sorted(repo.issues.values(), key=sev_key):
        action = action_of(i)
        if args.open and action == "none":
            continue
        if args.action and action != args.action:
            continue
        if args.platform and args.platform not in as_list(i.get("platforms")):
            continue
        if args.area and i.get("area") != args.area:
            continue
        if args.pattern and args.pattern not in as_list(i.get("pattern")):
            continue
        line = "%s  %-4s  %-6s  %-16s  %s" % (i["id"], SEV_SHORT.get(i.get("severity"), ""),
                                              action if action != "none" else i.get("status", ""),
                                              i.get("area", ""), i.get("title", ""))
        if i.get("next") and action != "none":
            line += "  | next: " + i["next"]
        print(line)


def write_index(repo):
    (repo.kb / "INDEX.md").write_text(render_index(repo), encoding="utf-8")


# ---------------------------------------------------------------- new

def slugify(title):
    return re.sub(r"[^a-z0-9]+", "-", title.lower()).strip("-")[:60].rstrip("-")


def next_number(ids, prefix):
    nums = [int(m.group(1)) for i in ids for m in [re.match(r"^%s-(\d+)$" % re.escape(prefix), i)] if m]
    return max(nums, default=0) + 1


def fill_template(name, values):
    text = (SHARED / "templates" / name).read_text(encoding="utf-8")
    head, sep, body = text.partition("\n---\n")
    text = re.sub(r"(?m)[ \t]{2,}#\s.*$", "", head) + sep + body    # drop the hints from the front matter
    for key, value in values.items():
        text = re.sub(r"(?m)^%s:.*$" % key, lambda m: "%s: %s" % (key, value), text, count=1)
    return text.replace("2026-01-01", today())


def new_issue(repo, title, pattern, kind):
    prefix = repo.conf["prefix"]
    iid = "%s-%03d" % (prefix, next_number(repo.issues, prefix))
    path = repo.kb / "issues" / ("%s-%s.md" % (iid, slugify(title)))
    path.parent.mkdir(exist_ok=True)
    values = {"id": iid, "title": title, "type": kind, "reported": today()}
    if pattern:
        values["pattern"] = pattern
    path.write_text(fill_template("issue.md", values), encoding="utf-8")
    print("created", repo.rel(path))
    if pattern:
        print("now list %s in the Issue column of the %s row in docs/kb/ledger.md" % (iid, pattern))


def new_pattern(repo, title, siblings):
    ids = set(repo.patterns)
    for s in siblings:
        ids |= set(Repo(kb_of(s)).patterns)
    pid = "PAT-%03d" % next_number(ids, "PAT")
    path = repo.shared / "patterns" / ("%s-%s.md" % (pid, slugify(title)))
    path.write_text(fill_template("pattern.md", {"id": pid, "title": title, "added": today()}), encoding="utf-8")
    add_ledger_rows(repo.kb, [pid])
    print("created", repo.rel(path), "and an 'unchecked' ledger row")
    print("fill it in, set this repository's ledger row, then run kb.py sync with every other repository")


def today():
    return datetime.date.today().isoformat()


# ---------------------------------------------------------------- sync

def hash_tree(shared):
    result = {}
    for path in sorted(shared.rglob("*")):
        rel = path.relative_to(shared).as_posix()
        if path.is_file() and rel != MANIFEST and "__pycache__" not in rel and path.name != ".DS_Store":
            result[rel] = hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def read_manifest(shared):
    result = {}
    path = shared / MANIFEST
    if path.exists():
        for line in path.read_text(encoding="utf-8").splitlines():
            if line and not line.startswith("#"):
                digest, rel = line.split(None, 1)
                result[rel] = digest
    return result


def write_manifest(shared, hashes):
    lines = ["# sha256 of every file in docs/kb/shared as of the last kb.py sync; do not edit by hand"]
    lines += ["%s  %s" % (hashes[rel], rel) for rel in sorted(hashes)]
    (shared / MANIFEST).write_text("\n".join(lines) + "\n", encoding="utf-8")


def add_ledger_rows(kb, pids):
    path = kb / "ledger.md"
    if not path.exists():
        return []
    text = path.read_text(encoding="utf-8")
    have = {row["Pattern"] for row in parse_ledger(kb)}
    new = [pid for pid in sorted(pids) if pid not in have]
    if new:
        rows = "".join("| %s | unchecked |  |  |  |  |\n" % pid for pid in new)
        path.write_text(text.rstrip("\n") + "\n" + rows, encoding="utf-8")
    return new


def kb_of(repo_dir):
    path = Path(repo_dir).resolve()
    if path.name == "kb" and path.parent.name == "docs":
        return path
    return path / "docs" / "kb"


def sync(siblings, dry_run):
    kbs = [KB] + [kb_of(s) for s in siblings]
    for kb in kbs[1:]:
        if not kb.parent.parent.is_dir():
            sys.exit("not a directory: %s" % kb.parent.parent)
    current = {kb: hash_tree(kb / "shared") if (kb / "shared").is_dir() else {} for kb in kbs}
    recorded = {kb: read_manifest(kb / "shared") for kb in kbs}
    plan, conflicts = {}, []
    for rel in sorted(set().union(*current.values())):
        present = [kb for kb in kbs if rel in current[kb]]
        changed = {current[kb][rel] for kb in present if current[kb][rel] != recorded[kb].get(rel)}
        if len(changed) > 1:
            conflicts.append("%s changed differently in: %s" % (rel, ", ".join(
                str(kb.parent.parent.name) for kb in present if current[kb][rel] in changed)))
            continue
        if changed:
            winner = changed.pop()
        else:
            contents = {current[kb][rel] for kb in present}
            if len(contents) == 1:
                winner = contents.pop()
            elif rel in current[KB]:
                winner = current[KB][rel]
            else:
                conflicts.append("%s differs between repositories and none has edited it since its last sync" % rel)
                continue
        source = next(kb for kb in present if current[kb][rel] == winner)
        plan[rel] = (source, winner)

    ids = {}
    for rel in plan:
        m = re.match(r"^patterns/(PAT-\d+)-", rel)
        if m:
            ids.setdefault(m.group(1), []).append(rel)
    for pid, rels in ids.items():
        if len(rels) > 1:
            conflicts.append("%s is used by more than one file: %s (renumber one of them)" % (pid, ", ".join(rels)))
    if conflicts:
        print("sync stopped, nothing was written:")
        for c in conflicts:
            print("  " + c)
        sys.exit(1)

    final = {rel: digest for rel, (source, digest) in plan.items()}
    for kb in kbs:
        name = kb.parent.parent.name
        for rel, (source, digest) in plan.items():
            if current[kb].get(rel) != digest:
                print("%-14s %s %s (from %s)" % (name, "update" if rel in current[kb] else "add   ", rel, source.parent.parent.name))
                if not dry_run:
                    (kb / "shared" / rel).parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(source / "shared" / rel, kb / "shared" / rel)
        if not dry_run:
            write_manifest(kb / "shared", final)
            pids = [m.group(1) for rel in final for m in [re.match(r"^patterns/(PAT-\d+)-", rel)] if m]
            for pid in add_ledger_rows(kb, pids):
                print("%-14s ledger: %s unchecked" % (name, pid))
            if parse_conf(kb) is not None and (kb / "INDEX.md").exists():
                write_index(Repo(kb))
    print("dry run, nothing written" if dry_run else "shared files identical in %d repositories" % len(kbs))


# ---------------------------------------------------------------- status

ABBREV = {"affected": "AFFECTED", "fixed": "fixed", "not-affected": "ok", "n/a": "n/a", "unchecked": "?"}


def git(root, *args):
    try:
        return subprocess.run(["git", "-C", str(root)] + list(args), capture_output=True, text=True).stdout.strip()
    except OSError:
        return ""


def status(siblings):
    repos = [Repo(KB)] + [Repo(kb_of(s)) for s in siblings]
    names = [r.name for r in repos]
    width = max(9, max(len(n) for n in names) + 1)
    pids = sorted(set().union(*(r.patterns for r in repos)))
    titles = {}
    for r in repos:
        for pid, p in r.patterns.items():
            titles.setdefault(pid, p.get("title", ""))
    print("Pattern ledger (? = unchecked, - = pattern not synced to that repository)\n")
    print("%-8s " % "" + "".join("%-*s" % (width, n[:width - 1]) for n in names))
    for pid in pids:
        cells = []
        for r in repos:
            if pid not in r.patterns:
                cells.append("-")
            else:
                row = r.ledger_row(pid)
                cells.append(ABBREV.get(row["Status"], row["Status"]) if row else "?")
        print("%-8s " % pid + "".join("%-*s" % (width, c) for c in cells) + " " + titles[pid][:70])

    print("\nShared knowledge-base files (docs/kb/shared)")
    reference = hash_tree(repos[0].shared)
    for r in repos:
        h = hash_tree(r.shared)
        print("  %-14s %s" % (r.name, "same as %s" % repos[0].name if h == reference else "DIFFERENT: run kb.py sync"))

    print("\nFiles copied between repositories (shared-code in kb.conf)")
    groups = {}
    for r in repos:
        for name, rel in (r.conf or {}).get("shared-code", []):
            path = r.root / rel
            digest = hashlib.sha256(path.read_bytes()).hexdigest()[:8] if path.exists() else "missing"
            when = git(r.root, "log", "-1", "--format=%cs %h", "--", rel) or "uncommitted"
            groups.setdefault(name, []).append((r.name, rel, digest, when))
    if not groups:
        print("  none registered")
    for name in sorted(groups):
        copies = groups[name]
        drift = len({c[2] for c in copies}) > 1
        print("  %s%s" % (name, "  DRIFT: the copies differ, see which one is newest" if drift else ""))
        for c in copies:
            print("    %-14s %-34s %s  last change %s" % c)

    print("\nOpen issues")
    for r in repos:
        open_ = [i for i in r.issues.values() if i.get("status") not in CLOSED]
        by = {}
        for i in open_:
            by[i.get("severity", "?")] = by.get(i.get("severity", "?"), 0) + 1
        print("  %-14s %3d  %s" % (r.name, len(open_), ", ".join("%s %d" % (s, by[s]) for s in SEVERITY_ORDER if s in by)))


# ---------------------------------------------------------------- init

def init(name, prefix):
    if parse_conf(KB) is not None:
        sys.exit("docs/kb/kb.conf already exists")
    (KB / "issues").mkdir(parents=True, exist_ok=True)
    (KB / "kb.conf").write_text(
        "# Settings read by docs/kb/shared/kb.py\nname = %s\nprefix = %s\n"
        "# Files that are copies of a file in another repository: shared-code = <name> <path>\n" % (name, prefix),
        encoding="utf-8")
    (KB / "ledger.md").write_text((SHARED / "templates" / "ledger.md").read_text(encoding="utf-8"), encoding="utf-8")
    if not (KB / "README.md").exists():
        (KB / "README.md").write_text((SHARED / "templates" / "README.md").read_text(encoding="utf-8"), encoding="utf-8")
    add_ledger_rows(KB, list(Repo(KB).patterns))
    write_index(Repo(KB))
    print("created docs/kb/kb.conf, ledger.md (every pattern unchecked), README.md, INDEX.md")


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("lint", help="check this repository's knowledge base")
    p.add_argument("--strict", action="store_true", help="unchecked patterns are errors (use before a pull request)")
    sub.add_parser("index", help="rewrite docs/kb/INDEX.md")
    p = sub.add_parser("list", help="one line per issue (or pattern), with filters")
    p.add_argument("--open", action="store_true", help="only issues that still need something done")
    p.add_argument("--action", help="fix, decide, verify or review")
    p.add_argument("--platform", help="a platform tag, e.g. windows (issues tagged 'all' are not included)")
    p.add_argument("--area", help="an area, e.g. checkpoint")
    p.add_argument("--pattern", help="issues that are instances of this pattern")
    p.add_argument("--patterns", action="store_true", help="list the shared patterns with their status here")
    p = sub.add_parser("new", help="create an issue or a pattern from the template")
    p.add_argument("what", choices=["issue", "pattern"])
    p.add_argument("title")
    p.add_argument("--pattern", help="issue: the cross-repository pattern it is an instance of")
    p.add_argument("--type", default="bug", help="issue: bug, gotcha, design-note or question")
    p.add_argument("--siblings", nargs="*", default=[], help="pattern: other repositories, so the number is free everywhere")
    p = sub.add_parser("sync", help="make docs/kb/shared identical in this and the given repositories")
    p.add_argument("repos", nargs="+")
    p.add_argument("--dry-run", action="store_true")
    p = sub.add_parser("status", help="cross-repository report (printed only)")
    p.add_argument("repos", nargs="*")
    p = sub.add_parser("init", help="start a knowledge base in this repository")
    p.add_argument("--name", required=True)
    p.add_argument("--prefix", required=True, help="issue id prefix, 2-5 capital letters")
    args = ap.parse_args()

    if args.cmd == "sync":
        return sync(args.repos, args.dry_run)
    if args.cmd == "status":
        return status(args.repos)
    if args.cmd == "init":
        return init(args.name, args.prefix)
    repo = Repo(KB)
    if args.cmd == "index":
        write_index(repo)
        print("wrote docs/kb/INDEX.md")
    elif args.cmd == "list":
        list_items(repo, args)
    elif args.cmd == "new":
        if repo.conf is None:
            sys.exit("docs/kb/kb.conf missing (run kb.py init first)")
        if args.what == "issue":
            new_issue(repo, args.title, args.pattern, args.type)
        else:
            new_pattern(repo, args.title, args.siblings)
    elif args.cmd == "lint":
        lint(repo, args.strict)
        for w in repo.warnings:
            print("warning: " + w)
        for e in repo.errors:
            print("error: " + e)
        print("%s: %d error(s), %d warning(s)" % (repo.name, len(repo.errors), len(repo.warnings)))
        sys.exit(1 if repo.errors else 0)


if __name__ == "__main__":
    main()
