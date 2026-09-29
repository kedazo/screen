#!/bin/sh

# Rendering check for UTF-8 hardstatus alignment.
# Keep format minimal (%=%t | %c), run at 80x25, and assert absolute
# rendered width (no overflow/spill). This variant is safe on python3-minimal
# (no ctypes dependency).

echo 1..14

. `dirname $0`/boilerplate.sh

LC_ALL=C.UTF-8
export LC_ALL

workdir=$(mktemp -d)
trap 'rm -rf "$workdir"; screen -wipe >/dev/null 2>&1 || true' EXIT
SCREENDIR="$workdir/sock"
mkdir -p "$SCREENDIR"
chmod 700 "$SCREENDIR"
export SCREENDIR

extract_metrics() {
    out="$1"
    title="$2"
    cols="$3"
    python3 - <<'PY' "$out" "$title" "$cols"
import re
import sys
import unicodedata

raw = open(sys.argv[1], "rb").read()
title = sys.argv[2]
cols = int(sys.argv[3])

def wcwidth_py(ch):
    cat = unicodedata.category(ch)
    if cat[0] == "C":
        return 0
    if unicodedata.combining(ch):
        return 0
    if unicodedata.east_asian_width(ch) in ("W", "F"):
        return 2
    return 1

def cell_width(s):
    return sum(wcwidth_py(ch) for ch in s)

best = None
for m in re.finditer(rb"\x1b\[(?:[0-9]+;[0-9]+)?H", raw):
    seg = raw[m.end():m.end() + 1200]
    seg = re.split(rb"\x1b\[(?:[0-9]+;[0-9]+)?H", seg, maxsplit=1)[0]
    seg = re.sub(rb"\x1b\[[0-9;?]*[ -/]*[@-~]", b"", seg)
    seg = re.sub(rb"\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)", b"", seg)
    seg = seg.replace(b"\r", b"").replace(b"\n", b"")
    line = seg.decode("utf-8", "replace")
    if title not in line:
        continue
    if best is None or len(line) > len(best):
        best = line

if best is None:
    print("-1")
    raise SystemExit(0)

cells = cell_width(best)
spill = cells - cols
if spill < 0:
    spill = 0
trail = len(best) - len(best.rstrip(" "))
i = best.find(title)
pre = cell_width(best[:i]) if i >= 0 else -1

print(f"{spill} {cells} {trail} {pre}")
PY
}

run_case() {
    label="$1"
    title="$2"
    cols="$3"
    out="$workdir/$label.typescript"
    rc="$workdir/$label.screenrc"
    session="$TESTNAME.$label"

    cat > "$rc" <<EOF
startup_message off
vbell off
defutf8 on
hardstatus alwayslastline "%=%t | %c "
screen -t "$title" sh -c 'sleep 20'
select 0
utf8 on
EOF

    (
        sleep 8
        screen -S "$session" -X quit >/dev/null 2>&1 || true
    ) &

    TERM=xterm-256color script -q -c "stty cols $cols rows 25; screen -U -S $session -c $rc" "$out" >/dev/null 2>&1 || true
    metrics=$(extract_metrics "$out" "$title" "$cols")
    set -- $metrics
    spill="$1"
    cells="$2"
    trail="$3"
    pre="$4"
    [ "$spill" -ge 0 ]
    check_exit_code_true "$label: rendered title frame found"
    echo "# $label: cells=$cells spill=$spill pre_title_cells=$pre trail_spaces=$trail"
    echo "$spill" > "$workdir/$label.spill"
    echo "$pre" > "$workdir/$label.pre"
    echo "$trail" > "$workdir/$label.trail"
}

# ASCII sanity control should not spill.
run_case ascii12 'AAAAAAAAAAAA' 80
[ "$(cat "$workdir/ascii12.spill")" -eq 0 ]
check_exit_code_true ascii12: no overflow
[ "$(cat "$workdir/ascii12.trail")" -eq 1 ]
check_exit_code_true ascii12: keep literal trailing space

# Mixed UTF-8 case should also not spill.
run_case mixed 'abcóą⭍💩ZZZZ' 80
[ "$(cat "$workdir/mixed.spill")" -eq 0 ]
check_exit_code_true mixed: no overflow
[ "$(cat "$workdir/mixed.pre")" -eq "$(cat "$workdir/ascii12.pre")" ]
check_exit_code_true mixed: same pre-title cell width as ascii12 control
[ "$(cat "$workdir/mixed.trail")" -eq 1 ]
check_exit_code_true mixed: keep literal trailing space

# Second ASCII sanity control (same visual width as Latin test) should not spill.
run_case ascii18 'AAAAAAAAAAAAAAAAAA' 80
[ "$(cat "$workdir/ascii18.spill")" -eq 0 ]
check_exit_code_true ascii18: no overflow
[ "$(cat "$workdir/ascii18.trail")" -eq 1 ]
check_exit_code_true ascii18: keep literal trailing space

# Latin-extended UTF-8 case should not spill (currently regresses).
run_case latin 'Fußgängerübergänge' 80
[ "$(cat "$workdir/latin.spill")" -eq 0 ]
check_exit_code_true latin: no overflow
[ "$(cat "$workdir/latin.pre")" -eq "$(cat "$workdir/ascii18.pre")" ]
check_exit_code_true latin: same pre-title cell width as ascii18 control
[ "$(cat "$workdir/latin.trail")" -eq 1 ]
check_exit_code_true latin: keep literal trailing space
