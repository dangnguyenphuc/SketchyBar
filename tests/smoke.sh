#!/bin/sh
# Smoke tests for a built sketchybar binary.
# Usage: tests/smoke.sh [path/to/sketchybar]   (default: bin/sketchybar)
#
# These tests only exercise the CLI surface; they never start the bar itself,
# so they are safe to run on a headless CI runner or next to a running bar.

BIN="${1:-bin/sketchybar}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/sketchybar-smoke.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

PASS=0
FAIL=0

pass() { PASS=$((PASS + 1)); printf '  \033[32mok\033[0m    %s\n' "$1"; }
fail() { FAIL=$((FAIL + 1)); printf '  \033[31mFAIL\033[0m  %s\n' "$1"; [ -n "$2" ] && printf '        %s\n' "$2"; }

# Run a command, capture stdout/stderr/exit code into $OUT, $ERR, $RC.
run() {
  "$@" >"$WORK/out" 2>"$WORK/err"
  RC=$?
  OUT="$(cat "$WORK/out")"
  ERR="$(cat "$WORK/err")"
}

echo "Smoke testing: $BIN"

# --- binary -----------------------------------------------------------------
if [ -x "$BIN" ]; then pass "binary exists and is executable"
else fail "binary exists and is executable" "not found: $BIN"; echo; echo "0 passed, 1 failed"; exit 1; fi

if lipo -archs "$BIN" 2>/dev/null | grep -qw arm64; then pass "binary contains arm64 slice"
else fail "binary contains arm64 slice" "archs: $(lipo -archs "$BIN" 2>&1)"; fi

LIBS="$(otool -L "$BIN" 2>/dev/null)"
for fw in SkyLight DisplayServices MediaRemote AppKit CoreAudio; do
  if echo "$LIBS" | grep -q "/$fw.framework/"; then pass "links $fw.framework"
  else fail "links $fw.framework"; fi
done

# --- version ----------------------------------------------------------------
MAJOR="$(sed -n 's/^#define MAJOR \([0-9]*\).*/\1/p' "$ROOT/src/sketchybar.c")"
MINOR="$(sed -n 's/^#define MINOR \([0-9]*\).*/\1/p' "$ROOT/src/sketchybar.c")"
PATCH="$(sed -n 's/^#define PATCH \([0-9]*\).*/\1/p' "$ROOT/src/sketchybar.c")"
EXPECTED="sketchybar-v$MAJOR.$MINOR.$PATCH"

for opt in --version -v; do
  run "$BIN" "$opt"
  if [ "$RC" -eq 0 ] && [ "$OUT" = "$EXPECTED" ]; then pass "$opt prints $EXPECTED"
  else fail "$opt prints $EXPECTED" "rc=$RC out='$OUT' err='$ERR'"; fi
done

# --- help -------------------------------------------------------------------
for opt in --help -h; do
  run "$BIN" "$opt"
  if [ "$RC" -eq 0 ] && echo "$OUT" | grep -q '^Usage:'; then pass "$opt prints usage"
  else fail "$opt prints usage" "rc=$RC err='$ERR'"; fi
done

# --- argument errors --------------------------------------------------------
for opt in --config -c; do
  run "$BIN" "$opt"
  if [ "$RC" -eq 1 ] && printf '%s\n%s\n' "$OUT" "$ERR" | grep -q "Too few arguments"; then pass "$opt without file is rejected"
  else fail "$opt without file is rejected" "rc=$RC out='$OUT' err='$ERR'"; fi
done

# --- client with no server --------------------------------------------------
# The mach service name is derived from basename(argv[0]), so a renamed copy
# can never reach a real bar that happens to be running on this machine.
cp "$BIN" "$WORK/sketchybar_smoke_noserver"
for args in "--query bar" "--set foo label=bar" "-m --query bar"; do
  # shellcheck disable=SC2086
  run "$WORK/sketchybar_smoke_noserver" $args
  if [ "$RC" -eq 0 ] && [ -z "$OUT" ]; then pass "client '$args' without server exits cleanly"
  else fail "client '$args' without server exits cleanly" "rc=$RC out='$OUT' err='$ERR'"; fi
done

echo
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
