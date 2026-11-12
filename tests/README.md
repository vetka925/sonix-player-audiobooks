# Audiobook and update regression tests

Run from the repository root:

```sh
tests/run-audiobook-tests.sh
```

Requirements: a C compiler, POSIX shell, pthreads and SQLite development headers/library.
GUI-related checks also need the player's LVGL headers. On a clean checkout,
fetch the same version as the player Makefile before running the suite:

```sh
lvgl_version=$(sed -n 's/^LVGL_VER = //p' sonix-player/Makefile)
git clone --depth 1 --branch "v$lvgl_version" https://github.com/lvgl/lvgl.git sonix-player/lvgl
```

An existing `make host` checkout already has this directory. Install `pkg-config`,
`libopusfile-dev` and `libwavpack-dev` to run the optional decoder-duration checks;
GitHub Actions installs these so those checks are not skipped.

On Debian/Ubuntu: `apt-get install build-essential libsqlite3-dev`.
On macOS the system compiler and SQLite library suffice.

The runner creates fixtures under a unique temporary directory and removes them on exit.
It never reads a real music library, card, player or network service.

The database checks compile the real `audiobookdb.c` and the shared case-folding
implementation from `library.c`. Only metadata/chapter readers, file-identity
notifications and background-priority setup are stubbed. They cover:

- cancellation before scanning and after 55 of 60 files;
- failed directory reads and a file disappearing between listing and indexing;
- process termination mid-scan followed by reopening the persistent database;
- a live position save during scanning;
- successful pruning and rollback when publication fails after old rows were deleted;
- stale writes interleaved across two books, missing-row retry and a legitimate
  delayed write belonging to another book;
- Cyrillic album/author grouping, natural chapter ordering and Russian disc folders.

The playback check executes the real paused-track loading path with a decoder
and storage boundary stubbed; it verifies that outgoing progress is forced out
before the book association changes and before a new decoder is started.
The update checks ensure that a missing/exported card is rejected before
network/download or firmware lookup, and a read-only card before download.
The small ALSA include stub is only for host tests: `audio.h` exposes no ALSA types.

These checks are not a complete firmware/driver test suite. They do not verify
MIPS ABI, audio output, physical removal, recovery, or flash the device.

## Receiver regression and GUI checks

`sh tests/run-receiver-test.sh` exercises the production worker operation and
the page's retry callback: source eligibility, occupied output, closed receiver,
missing profile, D-Bus failure, capture retry and duplicate taps. The runner
extracts complete production functions, following the navigation harness above.

On Linux, `sh tests/build-receiver-ui.sh` builds `/tmp/sonix-receiver-ui` with
the real UI and a link-time Bluetooth test boundary. Use a temporary
`SONIX_CONFIG` and `SONIX_SD_ROOT`; set `[clock] configured = 1`,
`[ui] language = Russian`, and `[power] screen_off_seconds = 0` in the config.
Set `SONIX_LANG_DIR` to this checkout's R1 language directory and run from
`sonix-player/` so fallback fonts resolve. Set `SONIX_PANEL=480x800` for R1
or `480x720` for the smaller panel. No real Bluetooth service is used.

Open Wireless > Bluetooth, enable it, then open Receiver. Only the fixture
phone should appear, not the fixture headphones. The first tap fails after
1.8 seconds, the second establishes an aptX stream. The page must remain open,
show progress/error/success, and prevent duplicate taps while pending.
`SONIX_TEST_NO_SOURCES=1` exercises first-pairing instructions with no list.
These tests do not prove RF interoperability or sound on a physical device.

## Selected fork regressions

`sh tests/run-fork-tests.sh` runs Linux/ALSA fixtures for idle workers, mono routing,
failed-open position, external PCM cancellation, slow-resume command races,
LVGL input frames, private D-Bus owner loss, queue priority, and fork/exec FD
ownership. It is included in the full audiobook suite. Hardware boundaries are
injected; D-Bus tests use a private temporary daemon, never the system bus.
Upstream-derived fixtures and helpers are adapted from Rekkod v1.4.2; source
commits and scope are recorded in the implementation plan and report.
