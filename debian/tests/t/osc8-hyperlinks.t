#!/bin/sh

# OSC 8 hyperlinks: stored per cell, forwarded to capable terminals only.

echo 1..20

. `dirname $0`/boilerplate.sh

LC_ALL=C.UTF-8
export LC_ALL
SYSSCREENRC=/dev/null
export SYSSCREENRC

workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

SCREENDIR="$workdir/sock"
mkdir -p "$SCREENDIR"
chmod 700 "$SCREENDIR"
export SCREENDIR

# run_case LABEL TERM EXTRA_RC < commands
# Runs the commands in a shell inside screen; the outer terminal output
# (what a real terminal would get) ends up in $workdir/LABEL.out
run_case() {
    label="$1"
    term="$2"
    extra="$3"
    rc="$workdir/$label.screenrc"
    in="$workdir/$label.input"

    cat > "$in"
    echo exit >> "$in"
    {
        echo 'startup_message off'
        echo 'defscrollback 100'
        echo 'vbell off'
        printf '%s\n' "$extra"
        echo "screen -t sh env PS1='P> ' bash --noprofile --norc -i"
    } > "$rc"
    TERM="$term" timeout 60 script -q -c "$SCREEN -c $rc" "$workdir/$label.out" <"$in" >/dev/null 2>&1 || true
    # never leave a session behind, even if the case went wrong
    $SCREEN -X quit >/dev/null 2>&1 || true
}

# out_has LABEL PERL_REGEX: does the terminal output of LABEL match?
out_has() {
    perl -0777 -ne 'BEGIN { $re = shift } exit(/$re/s ? 0 : 1)' "$2" "$workdir/$1.out"
}

# perl regex snippets
ST='\e\\'				# string terminator ESC \
LINK='\e\]8;id=scr\d+-\d+;'		# open, up to the URI
CLOSE="\\e\\]8;;$ST"			# close
CSI='(?:\e\[[0-9;?]*[A-Za-z]|[\r\n])*'	# optional cursor movement etc.

# 1-2: basic link with UTF-8 in the URI, closed afterwards
run_case basic xterm-256color '' <<'EOF'
printf 'A \033]8;;https://example.com/\303\244\033\\LINK\033]8;;\033\\ Z\n'
EOF
out_has basic "${LINK}https://example\\.com/\\xc3\\xa4${ST}LINK"
check_exit_code_true "link forwarded with synthesized id and intact UTF-8"
out_has basic "LINK${CLOSE} Z"
check_exit_code_true "link closed after its text"

# 3-4: application ids: same id+URI is one link, no id means separate links
run_case ids xterm-256color '' <<'EOF'
printf '\033]8;id=x;https://e.com/\033\\AA\033]8;;\033\\ \033]8;id=x;https://e.com/\033\\BB\033]8;;\033\\\n'
printf '\033]8;;https://n.com/\033\\CC\033]8;;\033\\ \033]8;;https://n.com/\033\\DD\033]8;;\033\\\n'
EOF
out_has ids "\\e\\]8;id=(scr\\d+-\\d+);https://e\\.com/${ST}AA.*\\e\\]8;id=\\1;https://e\\.com/${ST}BB"
check_exit_code_true "same application id and URI share one link id"
out_has ids "\\e\\]8;id=(scr\\d+-\\d+);https://n\\.com/${ST}CC.*\\e\\]8;id=(?!\\1;)scr\\d+-\\d+;https://n\\.com/${ST}DD"
check_exit_code_true "links without application id stay separate"

# 5-8: who gets hyperlinks
run_case linux linux '' <<'EOF'
printf '\033]8;;https://example.com/\033\\LINK\033]8;;\033\\\n'
EOF
out_has linux '\e\]8;'
check_exit_code_false "no OSC 8 for TERM=linux (not in the allow list)"
out_has linux 'LINK'
check_exit_code_true "linked text still shown for TERM=linux"

run_case linuxhl linux 'termcapinfo linux HL' <<'EOF'
printf '\033]8;;https://example.com/\033\\LINK\033]8;;\033\\\n'
EOF
out_has linuxhl "${LINK}https://example\\.com/${ST}LINK"
check_exit_code_true "termcapinfo HL enables hyperlinks"

run_case xtermoff xterm-256color 'termcapinfo xterm* HL@' <<'EOF'
printf '\033]8;;https://example.com/\033\\LINK\033]8;;\033\\\n'
EOF
out_has xtermoff '\e\]8;'
check_exit_code_false "termcapinfo HL@ disables hyperlinks"

# 9: global switch
run_case off xterm-256color 'hyperlinks off' <<'EOF'
printf '\033]8;;https://example.com/\033\\LINK\033]8;;\033\\\n'
EOF
out_has off '\e\]8;'
check_exit_code_false "hyperlinks off disables hyperlinks"

# 10-12: URI length limit, over long OSC strings are swallowed cleanly
run_case long xterm-256color '' <<'EOF'
printf '\033]8;;https://x/%s\033\\OKLEN\033]8;;\033\\\n' "$(head -c 8000 /dev/zero | tr '\0' k)"
printf '\033]8;;https://x/%s\033\\TOOLONG\033]8;;\033\\\n' "$(head -c 9000 /dev/zero | tr '\0' u)"
EOF
out_has long "${LINK}https://x/k{8000}${ST}OKLEN"
check_exit_code_true "8000 byte URI is forwarded"
out_has long 'u{100}'
check_exit_code_false "over long URI does not leak onto the screen"
out_has long "${LINK}[^\\e]*${ST}${CSI}TOOLONG"
check_exit_code_false "text after an over long URI is not linked"

# 13-14: control characters in the URI must never reach the terminal
run_case inject xterm-256color '' <<'EOF'
printf '\033]8;;http://x/\033[31mEVIL\033\\SAFE\033]8;;\033\\\n'
EOF
out_has inject '\e\]8;;http://x/'
check_exit_code_false "URI with ESC is not forwarded"
out_has inject "${LINK}[^\\e]*${ST}${CSI}SAFE"
check_exit_code_false "text after a rejected link is not linked"

# 15: DECSC/DECRC do not save/restore the open link
run_case decsc xterm-256color '' <<'EOF'
printf '\033]8;;https://s/\033\\S\0337\033]8;;\033\\\0338Q\n'
EOF
out_has decsc "S${CSI}${CLOSE}${CSI}Q"
check_exit_code_true "restored cursor does not reopen the link"

# 16-17: links are stored: a redisplay and a window switch redraw them
run_case redraw xterm-256color '' <<'EOF'
printf '\033]8;;https://r.com/\033\\REDRAW\033]8;;\033\\\n'
screen -X redisplay; sleep 1
screen -X screen; sleep 1; screen -X select 0; sleep 1
screen -X quit
EOF
out_has redraw "\\e\\]8;id=(scr\\d+-\\d+);https://r\\.com/${ST}REDRAW.*\\e\\]8;id=\\1;https://r\\.com/${ST}REDRAW"
check_exit_code_true "redisplay redraws the link with the same id"
out_has redraw "(?:${LINK}https://r\\.com/${ST}REDRAW.*){3}"
check_exit_code_true "switching back to the window redraws the link"

# 18-20: ls --hyperlink
mkdir -p "$workdir/lsdir"
touch "$workdir/lsdir/alpha" "$workdir/lsdir/beta"
run_case ls xterm-256color '' <<EOF
ls --hyperlink=always --color=never "$workdir/lsdir"
EOF
out_has ls "${LINK}file://[^\\e]*/lsdir/alpha${ST}alpha${CSI}${CLOSE}"
check_exit_code_true "ls --hyperlink links alpha"
# the close may come after cursor movement, right before the next text
out_has ls "${LINK}file://[^\\e]*/lsdir/beta${ST}beta${CSI}${CLOSE}"
check_exit_code_true "ls --hyperlink links beta"
out_has ls "scr\\d+-(\\d+);file://[^\\e]*/alpha.*scr\\d+-(?!\\1;)\\d+;file://[^\\e]*/beta"
check_exit_code_true "ls entries are separate links"
