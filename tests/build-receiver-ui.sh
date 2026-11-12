#!/bin/sh
# Linux host simulator with a fake Bluetooth boundary; never a target build.
set -eu
repo=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
cd "$repo/sonix-player"
${CC:-cc} -O1 -I. -Ilvgl -DLV_CONF_INCLUDE_SIMPLE=1 -c ../tests/receiver_ui_backend.c -o "$build/backend.o"
{
 printf 'override HOST_LDFLAGS += %s' "$build/backend.o"
 nm "$build/backend.o" | awk '/ T __wrap_/ {sub("__wrap_", "", $3); printf " -Wl,--wrap=%s", $3} END {print ""}'
} > "$build/ui.mk"
make -f Makefile -f "$build/ui.mk" HOST_BIN="${1:-/tmp/sonix-receiver-ui}" host -j4
