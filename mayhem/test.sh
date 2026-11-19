#!/usr/bin/env bash
#
# mayhem/test.sh — run glslang's own golden-output suite (Test/runtests) against the CLI that
# mayhem/build.sh built with upstream's normal flags (build/test/StandAlone/glslang).
# Every `diff -b baseResults/X localResults/X` in runtests is one test case: the CLI's actual
# output (AST dumps, SPIR-V disassembly, reflection, link and error logs) must equal the checked-in
# golden file. A neutered CLI produces empty output and fails every diff.
# Does not compile anything; emits a CTRF summary.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

EXE="$SRC/build/test/StandAlone/glslang"
if [ ! -x "$EXE" ]; then
  echo "test.sh: $EXE missing — mayhem/build.sh did not build the oracle CLI" >&2
  emit_ctrf "glslang-runtests" 0 1 0
  exit 1
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/glslang-test.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
# runtests writes its outputs (and stray .spv files) into its cwd; run it on a copy of Test/.
cp -a "$SRC/Test" "$WORK/Test"
mkdir -p "$WORK/bin"
LOG="$WORK/diff.log"
: > "$LOG"
# A `diff` wrapper that records one PASS/FAIL per golden comparison (bash + /usr/bin/diff only).
cat > "$WORK/bin/diff" <<WRAP
#!/usr/bin/env bash
if /usr/bin/diff "\$@" > "$WORK/last.diff" 2>&1; then
  echo "PASS \${@: -1}" >> "$LOG"
else
  echo "FAIL \${@: -1}" >> "$LOG"
  head -20 "$WORK/last.diff"
fi
exit 0
WRAP
chmod +x "$WORK/bin/diff"

(cd "$WORK/Test" && PATH="$WORK/bin:$PATH" bash ./runtests localResults "$EXE" 1 > "$WORK/runtests.out" 2>&1)
grep -E "returned [0-9]+$" "$WORK/runtests.out" | head -5 || true

passed=$(grep -c '^PASS ' "$LOG" || true)
failed=$(grep -c '^FAIL ' "$LOG" || true)
grep '^FAIL ' "$LOG" | head -20 || true
# runtests holds ~79 golden comparisons; far fewer means the suite did not really run.
MIN_CASES=70
if [ $((passed + failed)) -lt "$MIN_CASES" ]; then
  echo "test.sh: only $((passed + failed)) golden comparisons ran (expected >= $MIN_CASES)" >&2
  failed=$((failed + 1))
fi
emit_ctrf "glslang-runtests" "$passed" "$failed" 0
