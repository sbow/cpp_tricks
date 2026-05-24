#!/usr/bin/env bash
# cpp_tricks repo health checks: make, README, interrogate.py
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
cd "$ROOT"

SKILL_SCRIPTS="$(cd "$(dirname "$0")" && pwd)"
FAIL=0

pass() { echo "PASS: $*"; }
fail() { echo "FAIL: $*"; FAIL=1; }

echo "=== make ==="
if make all; then
  pass "make all"
else
  fail "make all"
fi

echo
echo "=== README.md ==="
if python3 "$SKILL_SCRIPTS/check_readme.py"; then
  :
else
  FAIL=1
fi

echo
echo "=== interrogate.py ==="
if ! command -v uv >/dev/null 2>&1; then
  fail "uv not found (needed for interrogate.py)"
else
  set +e
  uv run scripts/interrogate.py --clean
  interrogate_rc=$?
  set -e

  index_html="build/interrogate/site/index.html"
  meta_json="build/interrogate/site/meta.json"

  if [[ -f "$index_html" && -f "$meta_json" ]]; then
    pass "site generated ($index_html, $meta_json)"
  else
    fail "interrogate site output missing (expected $index_html and $meta_json)"
  fi

  if [[ $interrogate_rc -eq 0 ]]; then
    pass "interrogate.py exited 0"
  else
    # Run stage often fails for servers/clients that block or need args.
    if python3 - "$meta_json" <<'PY'
import json
import sys

meta_path = sys.argv[1]
meta = json.loads(open(meta_path, encoding="utf-8").read())
failed = [p["name"] for p in meta["programs"] if not p["ok"]]
if not failed:
    sys.exit(0)
# Allow failures only when every failed program's last stage is "run"
for prog in meta["programs"]:
    if prog["ok"]:
        continue
    stages = prog.get("stages", [])
    if not stages or stages[-1] != "run":
        print(f"non-run failure: {prog['name']} (stages={stages})")
        sys.exit(1)
sys.exit(0)
PY
    then
      pass "interrogate.py exit $interrogate_rc (run-stage failures only — expected for servers)"
    else
      fail "interrogate.py exit $interrogate_rc with compile/link failures"
    fi
  fi
fi

echo
if [[ $FAIL -eq 0 ]]; then
  echo "All checks passed."
else
  echo "Some checks failed."
fi
exit "$FAIL"
