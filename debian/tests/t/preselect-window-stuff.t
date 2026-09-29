#!/bin/sh

echo 1..10

. `dirname $0`/boilerplate.sh

workdir=$(mktemp -d)
trap 'rm -rf "$workdir"; screen -wipe >/dev/null 2>&1 || true' EXIT

win1_out="$workdir/win1.out"
win2_out="$workdir/win2.out"
token="BUG1138176_TOKEN"

# Window 0: keep session alive.
create_session_3_tests sh -c 'sleep 60'

# Window 1 and 2: read stdin into separate files.
$SCREEN -X screen -t W1 sh -c "cat > '$win1_out'"
check_exit_code_true Create window 1
$SCREEN -X screen -t W2 sh -c "cat > '$win2_out'"
check_exit_code_true Create window 2
sleep 1

$SCREEN -p 2 -X stuff "$token$(printf '\r')"
check_exit_code_true Send token to preselected window 2
sleep 1

grep -F -q "$token" "$win2_out"
check_exit_code_true Token received in window 2

! grep -F -q "$token" "$win1_out"
check_exit_code_true Token not received in window 1

kill_session_2_tests
