#!/bin/sh

echo 1..11

. `dirname $0`/boilerplate.sh

LC_ALL=C.UTF-8
export LC_ALL

workdir=$(mktemp -d)
trap 'rm -rf "$workdir"; screen -wipe >/dev/null 2>&1 || true' EXIT

query_file="$workdir/hstatus.out"

wait_for_hstatus() {
    expected="$1"
    i=0
    while [ "$i" -lt 10 ]; do
        $SCREEN -Q windows "%n [%h]" >"$query_file" 2>/dev/null || true
        if LC_ALL=C.UTF-8 grep -F -q "$expected" "$query_file"; then
            return 0
        fi
        i=$((i + 1))
        sleep 1
    done
    return 1
}

create_session_3_tests sh -c 'printf "\033]0;%s\033\\" "ascii-only-hstatus"; sleep 2; printf "\033]0;%s\033\\" "abc|ó|ą|⭍|💩"; sleep 2; printf "\033]0;%s\033\\" "Fußgängerübergänge"; sleep 120'

wait_for_hstatus 'ascii-only-hstatus'
check_exit_code_true ASCII hardstatus text is preserved

LC_ALL=C.UTF-8 grep -F -q 'ascii-only-hstatus' "$query_file"
check_exit_code_true ASCII hardstatus query returns exact expected text

wait_for_hstatus 'abc|ó|ą|⭍|💩'
check_exit_code_true UTF-8 hardstatus preserves mixed characters and separators

LC_ALL=C.UTF-8 grep -F -q 'abc|ó|ą|⭍|💩' "$query_file"
check_exit_code_true UTF-8 hardstatus contains expected separator layout

wait_for_hstatus 'Fußgängerübergänge'
check_exit_code_true UTF-8 hardstatus preserves Latin extended characters

LC_ALL=C.UTF-8 grep -F -q 'Fußgängerübergänge' "$query_file"
check_exit_code_true UTF-8 hardstatus query returns exact expected text

kill_session_2_tests
