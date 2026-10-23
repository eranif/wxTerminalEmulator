#!/bin/sh
# Launcher of GlyphT, installed as bin/glypht. The program itself is in
# ../lib/glypht, next to its themes and icon.
#
# GTK overlay scroll bars are drawn on top of the window and show only when the
# mouse is near the edge. The scroll bar of the OpenGL view is then hidden, so
# use classic scroll bars. A value set by the user wins.
: "${GTK_OVERLAY_SCROLLING:=0}"
export GTK_OVERLAY_SCROLLING

exec "$(dirname "$0")/../lib/glypht/glypht" "$@"
