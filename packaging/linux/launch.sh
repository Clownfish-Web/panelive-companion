#!/bin/sh
set -eu
appdir=$(dirname -- "$(readlink -f -- "$0")")
export PANELIVE_LINUX_LAUNCHER="$appdir/launch.sh"

export LD_LIBRARY_PATH="$appdir/usr/lib"
export GSETTINGS_SCHEMA_DIR="$appdir/usr/share/glib-2.0/schemas"
export GIO_MODULE_DIR="$appdir/usr/lib/gio/modules"
export GIO_EXTRA_MODULES="$GIO_MODULE_DIR"
export GDK_PIXBUF_MODULEDIR="$appdir/usr/lib/gdk-pixbuf-2.0/loaders"
export XDG_DATA_DIRS="$appdir/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export FONTCONFIG_FILE="$appdir/usr/etc/fonts/fonts.conf"
# GTK's CPU renderer avoids loading incompatible host graphics driver libraries
# into the bundled GLib/C++ runtime. Video decoding remains in Companion.
export GSK_RENDERER=cairo
export GTK_EXE_PREFIX="$appdir/usr"
export GTK_DATA_PREFIX="$appdir/usr"
unset GTK_MODULES
export GTK_PATH="$appdir/usr/lib/gtk-4.0"
export GTK_IM_MODULE=simple
# GdkPixbuf caches contain absolute module paths. Generate one at the actual
# installation location without writing to the system-owned application directory.
cache=$(mktemp -d "${TMPDIR:-/tmp}/panelive-pixbuf.XXXXXXXX")
trap 'rm -rf -- "$cache"' EXIT HUP INT TERM
"$appdir/usr/bin/gdk-pixbuf-query-loaders" "$GDK_PIXBUF_MODULEDIR"/*.so > "$cache/loaders.cache"
export GDK_PIXBUF_MODULE_FILE="$cache/loaders.cache"
"$appdir/usr/bin/panelive-companion" "$@" &
pid=$!
trap 'kill -TERM "$pid" 2>/dev/null || true' HUP INT TERM
status=0
wait "$pid" && status=0 || status=$?
# wait can be interrupted before the child finishes its graceful shutdown.
if kill -0 "$pid" 2>/dev/null; then
    wait "$pid" && status=0 || status=$?
fi
exit "$status"
