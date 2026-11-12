#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
awk '/^static bool connect_receiver_device\(/ {emit=1} emit {print} /^}/ {emit=0}' \
 "$repo/sonix-player/src/system/bluetooth/bluetooth.c" > "$build/receiver-worker.h"
${CC:-cc} -Wall -Werror -I"$build" -I"$repo/sonix-player" "$repo/tests/receiver_connect_test.c" -o "$build/test"
"$build/test"
awk '/^static void source_clicked\(/ {emit=1} emit {print} /^}/ {emit=0}' \
 "$repo/sonix-player/src/gui/bluetooth/btreceiverpage.c" > "$build/receiver-click.h"
${CC:-cc} -Wall -Werror -I"$build" -I"$repo/sonix-player" "$repo/tests/receiver_retry_test.c" -o "$build/retry"
"$build/retry"
