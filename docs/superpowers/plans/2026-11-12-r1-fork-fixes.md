# R1 selected fork fixes implementation plan

> Use superpowers:executing-plans inline. One writer; one final read-only review.

**Goal:** Improve idle efficiency, preserve audiobook position and recover Bluetooth/audio failures without replacing firmware daemons or audiobook UI.
**Architecture:** Adapt bounded fixes from Rekkod v1.4.2 (1db1fa8), retain our receiver chooser and audiobook features. Keep existing hardware drivers and service versions.
**Tech stack:** C, pthreads, LVGL, ALSA, D-Bus, Linux/MIPS toolchain.
**Spec:** Owner approved the preceding code-analysis recommendations on 2026-11-12: “Делай сначала все что планировали через superpowers”.
**Author:** OpenAI Codex; exact model not exposed.

## Constraints and review focus
- Existing isolated branch/worktree; preserve unrelated dependency symlink.
- No YouTube Music, daemon replacement, database migration or Graphite UI.
- Playback/receiver audio must keep working while screen is dark.
- Newer Stop/Play/seek must beat a slow stale resume or failed output.
- A full Bluetooth job queue must not lose service recovery; BlueALSA restart must not pretend BlueZ disconnected.
- Receiver teardown must terminate during repeated ALSA recovery failures.
- Mono conversion must leave stereo and wired paths unchanged.
- Existing book speed/EQ/bookmarks and sleep checkpoint tests remain mandatory.

### Task 1: Idle workers
Files: system/remote/sonixlink.c, system/bluetooth/btvolume.c, tests/idle_workers_test.py.
Interfaces: existing enable setters/condition variables; no public API change.
- [x] Exercise extracted real worker wait logic: disabled SonixLink uses signalled wait, empty Bluetooth stream waits 60000ms, a real stream retains 600ms polling; observe baseline failure.
- [x] Adapt 16fe631 and 42f0f00; verify wake requests remain under the existing lock and rerun tests.
- [x] Commit.

### Task 2: Mono and position preservation
Files: system/audio/audio.c, gui/bluetooth/btaudio.c, tests/audio_recovery_test.py and fixtures.
Interfaces: audio_play_at/audio_play_paused; retain failed initial open state for audio_resume.
- [x] Add failing production-function tests for mono 44.1/48k, stereo, paused codec reload, newer seek/stop ownership and failed open position.
- [x] Apply upstream PR188 and adapt application hunks of 9ed30d5/4f330f6, never daemon hunks.
- [x] Run regression fixtures, then commit.

### Task 3: Bounded external PCM recovery
Files: system/audio/audio.c, tests/audio_external_recovery_test.*.
Interfaces: audio_external_begin/write/end unchanged.
- [x] Exercise repeated EAGAIN/EPIPE/ESTRPIPE, cancellation, failed prepare and successful recovery against production functions; observe failures.
- [x] Adapt external-audio part of a255b04: monotonic 2-second no-progress deadline, at most one reopen, cancellation visible before lock, bounded waits.
- [x] Verify progress resets deadline and end releases output, then commit.

### Task 4: Resume responsiveness and touch frames
Files: system/audio/audio.c, Makefile, tools/lvgl_evdev_frames.py, tests/audio_resume_open_test.*, tests/evdev_*.
Interfaces: existing audio commands; serial guards protect unlocked driver work.
- [x] Import/adapt production-code fixtures for queued press/release and Stop/seek/route change during slow resume; observe red.
- [x] Adapt aa4140b + 9167fe9 + f30fa3d as a unit; make driver patch idempotent and preserve current touch fixes.
- [x] Run tests on Linux matching LVGL, then commit.

### Task 5: Bluetooth service ownership
Files: system/bluetooth/btstack.[ch], bluetooth.c, tests/btstack_owner* and rebind tests.
Interfaces: generation/service-loss notification from btstack; priority recovery flag in worker.
- [x] Adapt fork lifecycle/real-private-D-Bus tests, demonstrate missing owner-change recovery.
- [x] Adapt c6aafe7 + ef91871 together; clear service-owned caches and reject retired replies, retain receiver connect/profile jobs.
- [x] Verify queue saturation, BlueALSA versus BlueZ loss, receiver and headphone state; commit.

### Task 6: Child descriptor ownership
Files: system/core/logging.c, tests/child_exec_fds_test.c.
Interfaces: logging_child_stdio stays same; parent caches FD bound.
- [x] Exercise fork/exec inheritance with an open file and output pipes; baseline must retain unintended FD.
- [x] Adapt logging-only parts of 7ff50f1 and 5b6f554, preserve intentional stdio; test and commit.

### Task 7: Integration and firmware
- [x] Run full Linux audiobook suite plus new regressions; host and MIPS builds, ABI verification, source diff check.
- [x] Exercise receiver UI regression with actual LVGL simulator; no invented radio success.
- [x] One bounded final review, fix concrete findings with regression evidence.
- [x] Package R1 firmware, preserve rollback, compare SHA-256 after download.
- [x] Update existing report/STATE with results and remaining hardware acceptance; commit named paths. Installation/publication remains subject to concrete result acceptance.
