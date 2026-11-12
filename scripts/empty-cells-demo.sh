#!/usr/bin/env bash
# Animated progress bars made of "empty" cells: spaces that only have a
# style. Checks how the terminal draws them:
#  - every bar must be solid, with no gaps between the cells;
#  - the selection line must be highlighted across its spaces too;
#  - no caret may be drawn while it runs: the cursor is hidden (DECTCEM).
#
# Usage: scripts/empty-cells-demo.sh [width] [delay]

width=${1:-40}
delay=${2:-0.05}
text="Hello! What would you like to work on?"
lines=8 # lines drawn by one frame

cleanup() { printf '\e[0m\e[?25h'; }
trap cleanup EXIT
trap 'exit 130' INT

bar() { # style fill rest pct
  printf '\e[2K[\e[%sm%s\e[0m%s] %3d%%\n' "$1" "$2" "$3" "$4"
}

frame() {
  local i=$1 pct=$(($1 * 100 / width)) n=$(($1 * ${#text} / width))
  local fill rest
  printf -v fill '%*s' "$i" ''
  printf -v rest '%*s' "$((width - i))" ''
  printf '\e[2KReverse video (SGR 7):\n'
  bar 7 "$fill" "$rest" "$pct"
  printf '\e[2KUnderline (SGR 4):\n'
  bar 4 "$fill" "$rest" "$pct"
  printf '\e[2KBackground colour (SGR 44), drawn before the fix too:\n'
  bar 44 "$fill" "$rest" "$pct"
  printf '\e[2KSelection drawn by the program (SGR 7, like Codex):\n'
  printf '\e[2K\e[7m%s\e[0m%s\n' "${text:0:n}" "${text:n}"
}

printf '\e[?25l' # hide the cursor
for ((i = 0; i <= width; i++)); do
  frame "$i"
  if ((i < width)); then
    sleep "$delay"
    printf '\e[%dA' "$lines" # back to the first line of the frame
  fi
done
