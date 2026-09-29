# GNU screen 5.0.2 + OSC 8 hyperlinks

This is the unmodified Debian source package of GNU screen **5.0.2-1**
(from Debian sid) with **one extra patch** that adds OSC 8 hyperlink
support, backported to Ubuntu 24.04 (noble).

The patch is
[`debian/patches/109-osc8-hyperlinks.patch`](debian/patches/109-osc8-hyperlinks.patch).
Everything else is upstream/Debian as released.

## What it does

Terminal hyperlinks (`ESC ]8;;URI ESC \ text ESC ]8;; ESC \`), as printed
by `ls --hyperlink=auto`, `gcc`, `systemd`, etc., are dropped by stock
screen. With this patch they are clickable inside screen too:

- links are stored with the text, so they survive redraws, window
  switches, split regions, copy mode / scrollback, resizing and reattach
- only sent to terminals that support them; others see plain text
- URIs up to 8192 bytes; control characters are rejected, so a link
  can't inject escape sequences into your terminal
- over-long OSC strings no longer spill garbage onto the screen

Sessions started by screen 4.x can still be reattached with `screen -r`,
but `screen -X` / `-Q` against them needs the old 4.x binary.

## Configuration

On by default for terminals whose `$TERM` matches `xterm*`,
`*-256color`, `*-direct`, `foot*`, `kitty*`, `alacritty*`, `wezterm*`,
`ghostty*`, `contour*`, `tmux*`, `rxvt-unicode*`, `mintty*`, `iterm*`,
`vte*`, `gnome*`, `konsole*` or `st-*`.

In `~/.screenrc`:

```
hyperlinks off                # disable globally (also at runtime: C-a :hyperlinks off)
termcapinfo linux HL          # enable for a terminal not in the list
termcapinfo xterm* HL@        # disable for a terminal
```

`C-a :hyperlinks` without argument shows the current state.

## Build

```sh
dpkg-buildpackage -rfakeroot -b     # binary .deb
dpkg-buildpackage -rfakeroot -S -sa # source package (e.g. for a PPA)
```

## PPA

Ubuntu builds (only for 24.04LTS for now) are available in my hacking PPA:
https://launchpad.net/~kedazo/+archive/ubuntu/ubuntu-hacking

```sh
sudo add-apt-repository ppa:kedazo/ubuntu-hacking
sudo apt update
sudo apt install screen
```

License: GPL-3.0-or-later, same as GNU screen.
