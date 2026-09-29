#!/bin/sh

echo 1..2

. `dirname $0`/boilerplate.sh

workdir=$(mktemp -d)
trap 'rm -rf "$workdir"; screen -wipe >/dev/null 2>&1 || true' EXIT

session="$TESTNAME.atwindows"
screen_cmd="screen -S $session"
query_out="$workdir/query-atwindows.out"
typescript="$workdir/query-atwindows.typescript"

$screen_cmd -d -m sh -c 'sleep 20'
sleep 1
$screen_cmd -X screen -t SIL702MARK sh -c 'sleep 20'
(
    sleep 2
    $screen_cmd -Q @windows >"$query_out" 2>/dev/null || true
    sleep 2
    $screen_cmd -X quit >/dev/null 2>&1 || true
) &
TERM=xterm-256color script -q -c "$screen_cmd -r" "$typescript" >/dev/null 2>&1 || true
wait

rc=1
if [ -f "$query_out" ] && LC_ALL=C grep -F -q 'SIL702MARK' "$query_out"; then
    rc=0
fi
[ "$rc" -eq 0 ]
check_exit_code_true -Q @windows returns query output

rc=2
if [ -f "$typescript" ]; then
    LC_ALL=C grep -F -q 'SIL702MARK' "$typescript"
    rc=$?
fi
[ "$rc" -eq 1 ]
check_exit_code_true -Q @windows stays silent on attached display
