#!/bin/sh
# Prepares a FreeBSD machine to build GlyphT, and to show its window on another
# computer with SSH X11 forwarding (ssh -Y). Run it on the FreeBSD machine as
# root, or as a user of the wheel group: it then runs itself again with su.
# It can be run more than once.
#
# Usage: freebsd-setup.sh [--vnc]
#   --vnc  Also install a VNC server and the Xfce desktop, to run GlyphT on an
#          X server on this machine (see freebsd-vnc.sh). Use it when X11
#          forwarding is too slow, or does not draw.
#
# It does not build or run GlyphT. After it, connect with "ssh -Y" and run:
#   ./build.sh && ./.build-release/glypht

set -e

# The build needs bash (build.sh), GNU make (wxWidgets) and pkg-config (to find
# GTK 3). glew is only for the OpenGL renderer (--opengl).
BUILD_PACKAGES="bash cmake git gmake pkgconf gtk3 glew"
# X11 forwarding needs xauth; GTK needs fonts for its own text.
X11_PACKAGES="xauth dejavu"
# Xvnc, and a desktop to run in it. Xfce needs the D-Bus system service.
VNC_PACKAGES="tigervnc-server xfce"
SSHD_CONFIG=/etc/ssh/sshd_config

info() {
  echo "[INFO] $*"
}

VNC=0
for arg in "$@"; do
  case "${arg}" in
  --vnc) VNC=1 ;;
  *)
    echo "[ERROR] Unknown option: ${arg}" >&2
    echo "Usage: $(basename "$0") [--vnc]" >&2
    exit 1
    ;;
  esac
done

if [ "$(uname -s)" != "FreeBSD" ]; then
  echo "[ERROR] This script is for FreeBSD" >&2
  exit 1
fi

if [ "$(id -u)" -ne 0 ]; then
  info "Running again as root with su"
  exec su -m root -c "sh '$0' $*"
fi

PACKAGES="${BUILD_PACKAGES} ${X11_PACKAGES}"
if [ "${VNC}" = "1" ]; then
  PACKAGES="${PACKAGES} ${VNC_PACKAGES}"
fi
info "Installing packages: ${PACKAGES}"
# shellcheck disable=SC2086 # the list is split on purpose
env ASSUME_ALWAYS_YES=yes pkg install -y ${PACKAGES}

if grep -q '^X11Forwarding yes' "${SSHD_CONFIG}"; then
  info "X11 forwarding is already on in ${SSHD_CONFIG}"
else
  info "Turning on X11 forwarding in ${SSHD_CONFIG}"
  if grep -q '^#*X11Forwarding' "${SSHD_CONFIG}"; then
    sed -i '' -E 's/^#*X11Forwarding.*/X11Forwarding yes/' "${SSHD_CONFIG}"
  else
    echo "X11Forwarding yes" >>"${SSHD_CONFIG}"
  fi
  # Open SSH sessions are kept; new ones get X11 forwarding.
  info "Restarting sshd"
  service sshd restart
fi

if [ "${VNC}" = "1" ]; then
  info "Enabling the D-Bus system service"
  sysrc dbus_enable=YES >/dev/null
  service dbus status >/dev/null 2>&1 || service dbus start
fi

info "Checking the setup"
pkg-config --modversion gtk+-3.0 | sed 's/^/[INFO] GTK 3: /'
command -v xauth | sed 's/^/[INFO] xauth: /'
if [ "${VNC}" = "1" ]; then
  command -v Xvnc | sed 's/^/[INFO] Xvnc: /'
  command -v startxfce4 | sed 's/^/[INFO] startxfce4: /'
fi

info ""
info "Done. Connect again with X11 forwarding, then build and run GlyphT:"
info "  ssh -Y ec2-user@<this machine>"
info "  cd <wxTerminalEmulator> && ./build.sh && ./.build-release/glypht"
if [ "${VNC}" = "1" ]; then
  info "Or start a VNC desktop on this machine (as your user, not root):"
  info "  <wxTerminalEmulator>/scripts/freebsd-vnc.sh start"
fi
