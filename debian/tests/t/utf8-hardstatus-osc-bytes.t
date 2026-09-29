#!/bin/sh

echo 1..6

. `dirname $0`/boilerplate.sh

LC_ALL=C.UTF-8
export LC_ALL

workdir=$(mktemp -d)
trap 'rm -rf "$workdir"; screen -wipe >/dev/null 2>&1 || true' EXIT

SCREENDIR="$workdir/sock"
mkdir -p "$SCREENDIR"
chmod 700 "$SCREENDIR"
export SCREENDIR

run_case() {
    label="$1"
    title="$2"
    rc="$workdir/$label.screenrc"
    in="$workdir/$label.input"
    out="$workdir/$label.typescript"

    cat > "$rc" <<'RC'
startup_message off
defscrollback 0
vbell off
hardstatus alwayslastline "%=%t Z"
screen -t sh env PS1='NPROMPT> ' bash --noprofile --norc -i
select 0
RC

    cat > "$in" <<EOF
printf '\033]0;%s\033\\' "$title"
echo __DONE__
exit
EOF

    TERM=xterm-256color script -q -c "$SCREEN -c $rc" "$out" <"$in" >/dev/null 2>&1 || true

    got_hex=$(perl -0777 -ne 'while (/\e\][02];(.*?)\e\\/sg) { $p = $1 } END { print unpack("H*", $p) if defined $p }' "$out")
    exp_hex=$(printf '%s' "$title" | od -An -tx1 -v | tr -d ' \n')

    [ -n "$got_hex" ]
    check_exit_code_true "$label: OSC title payload captured"

    [ "$got_hex" = "$exp_hex" ]
    rc_cmp="$?"
    if [ "$rc_cmp" != 0 ]; then
        echo "# $label expected hex: $exp_hex"
        echo "# $label observed hex: $got_hex"
    fi
    [ "$rc_cmp" = 0 ]
    check_exit_code_true "$label: OSC title payload preserves UTF-8 bytes"
}

run_case ascii 'abc'
run_case extlatin 'Fußgängerąbergänge'
run_case mixed 'abc|ó|ą|⭍|💩'
