#!/bin/sh
# Runs an Xfce desktop on an X server of this FreeBSD machine (Xvnc), to test
# GlyphT without X11 forwarding: GlyphT draws on this machine, and only the
# screen goes over the network. Install the packages first with:
#   freebsd-setup.sh --vnc
#
# Usage: freebsd-vnc.sh start|stop|status
#
# The server listens on 127.0.0.1 only, so it has no password: it is reached
# through an SSH tunnel. On the other computer:
#   ssh -L 5901:localhost:5901 <user>@<this machine>
# then connect a VNC viewer to localhost:5901.
#
# Environment:
#   VNC_DISPLAY   X display number (default: 1, port 5900 + display)
#   VNC_GEOMETRY  desktop size (default: 1600x1000)

set -e

DISPLAY_NUM=${VNC_DISPLAY:-1}
GEOMETRY=${VNC_GEOMETRY:-1600x1000}
PORT=$((5900 + DISPLAY_NUM))
LOG_DIR=${HOME}/.cache/glypht-vnc
SOCKET=/tmp/.X11-unix/X${DISPLAY_NUM}

info() {
  echo "[INFO] $*"
}

error() {
  echo "[ERROR] $*" >&2
}

xvnc_pid() {
  pgrep -U "$(id -u)" -f "Xvnc :${DISPLAY_NUM} " || true
}

start() {
  if [ "$(id -u)" -eq 0 ]; then
    error "Run it as your user, not as root"
    exit 1
  fi
  for tool in Xvnc startxfce4 dbus-launch; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
      error "${tool} not found: run freebsd-setup.sh --vnc first"
      exit 1
    fi
  done
  if [ -n "$(xvnc_pid)" ]; then
    info "Xvnc is already running on display :${DISPLAY_NUM}"
    status
    return 0
  fi

  mkdir -p "${LOG_DIR}"
  info "Starting Xvnc on display :${DISPLAY_NUM} (${GEOMETRY})"
  # daemon(8) detaches it from this shell, so it keeps running after logout.
  daemon -f -o "${LOG_DIR}/Xvnc.log" Xvnc ":${DISPLAY_NUM}" \
    -geometry "${GEOMETRY}" -depth 24 -localhost -SecurityTypes None \
    -AlwaysShared -rfbport "${PORT}"

  # Wait for the X server.
  i=0
  while [ ! -S "${SOCKET}" ]; do
    i=$((i + 1))
    if [ "${i}" -gt 50 ]; then
      error "Xvnc did not start; see ${LOG_DIR}/Xvnc.log"
      tail -20 "${LOG_DIR}/Xvnc.log" >&2 || true
      exit 1
    fi
    sleep 0.2
  done

  info "Starting Xfce"
  DISPLAY=":${DISPLAY_NUM}" daemon -f -o "${LOG_DIR}/xfce.log" \
    dbus-launch --exit-with-session startxfce4

  status
  info ""
  info "On your computer, open a tunnel and connect a VNC viewer to localhost:${PORT}:"
  info "  ssh -L ${PORT}:localhost:${PORT} $(id -un)@<this machine>"
  info "Then run GlyphT from a terminal on the desktop, or from SSH with:"
  info "  DISPLAY=:${DISPLAY_NUM} <wxTerminalEmulator>/.build-release/glypht"
}

stop() {
  pid=$(xvnc_pid)
  if [ -z "${pid}" ]; then
    info "Xvnc is not running on display :${DISPLAY_NUM}"
    return 0
  fi
  # The Xfce session ends with its X server.
  info "Stopping Xvnc (pid ${pid})"
  kill "${pid}"
}

status() {
  pid=$(xvnc_pid)
  if [ -n "${pid}" ]; then
    info "Xvnc is running on display :${DISPLAY_NUM} (pid ${pid}), port ${PORT} on 127.0.0.1"
  else
    info "Xvnc is not running on display :${DISPLAY_NUM}"
  fi
}

case "${1}" in
start) start ;;
stop) stop ;;
status) status ;;
*)
  echo "Usage: $(basename "$0") start|stop|status" >&2
  exit 1
  ;;
esac
