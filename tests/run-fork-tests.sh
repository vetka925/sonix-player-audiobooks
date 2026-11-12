#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo"
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
python3 tests/idle_workers_test.py
python3 tests/audio_mono_test.py
python3 tests/audio_output_open_test.py --sanitize --controls
python3 tests/audio_external_recovery_test.py
python3 tests/audio_resume_open_test.py --lvgl-root "$repo/sonix-player" --sanitize --controls
python3 tests/evdev_frames_test.py --lvgl-root "$repo/sonix-player"
python3 tests/btstack_owner_bus_test.py
python3 tests/bluetooth_rebind_order_test.py --sanitize
for name in audio_output_failure btstack_owner_lifecycle bluetooth_rebind_queue child_exec_fds; do
 ${CC:-cc} -std=gnu11 -O1 -g -ffunction-sections -fdata-sections -DHOST_BUILD=1 -DLV_CONF_INCLUDE_SIMPLE=1 \
  -Isonix-player -Isonix-player/lvgl "tests/${name}_test.c" -Wl,--gc-sections -lasound -pthread -lm -o "$build/$name"
 "$build/$name"
done
