#!/bin/sh

echo 1..2

. `dirname $0`/boilerplate.sh

workdir=$(mktemp -d)
rc="$workdir/screenrc"
out="$workdir/typescript"

trap 'rm -rf "$workdir"; $SCREEN -X quit >/dev/null 2>&1 || true' EXIT

cat > "$rc" <<'RC'
startup_message off
defscrollback 0
term xterm
vbell off
hardstatus alwayslastline "%w"
rendition bell r
screen -t fg sh -c 'sleep 20'
screen -t bg sh -c 'sleep 3; printf "\\a"; sleep 20'
select 0
RC

(
    sleep 10
    $SCREEN -X quit
) &

TERM=xterm-256color script -q -c "$SCREEN -c $rc" "$out" >/dev/null 2>&1 || true

esc=$(printf '\033')
bell=$(printf '\a')
LC_ALL=C grep -F -q "Bell in window 1" "$out" || LC_ALL=C grep -F -q "$bell" "$out"
check_exit_code_true bell event from background window was emitted

LC_ALL=C grep -E -q "${esc}\\[[0-9;]*7m" "$out"
if [ "$?" != 0 ]; then
    echo "# reverse-video escape not found, escaped capture follows:"
    cat -v "$out" | sed -n '1,220p'
    false
fi
check_exit_code_true hardstatus windows list applies reverse bell rendition
