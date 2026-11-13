# Audiobooks v1.1.0 · HiBy R1

Phone audio through the R1's wired headphones is easier to connect and retry.
This release also improves audio recovery, touch handling and idle behavior while
retaining the audiobook features from v1.0.0.

## What's changed

- **Connect a phone from Receiver:** a paired-source list, connection progress and
  retry after failure. Headphones are excluded. Keep the Receiver page open while
  connecting; no ADB command is needed.
- **Recover Bluetooth services:** discard stale service state, prioritize recovery
  even when the job queue is full, and retry when the adapter appears late.
- **Keep your place after audio failures:** preserve playback position and resume
  intent, including paused codec changes. Newer Stop, Play and seek commands win
  over a slow output reopen.
- **Responsive pause/resume:** slow audio-driver work no longer holds the command
  mutex. Receiver retry loops have a bounded no-progress interval and can cancel.
- **Mono over Bluetooth:** mono material is routed through stereo conversion for
  Bluetooth headphones; wired and stereo routing are retained.
- **Touch and covers:** preserve queued touch frames, integrate upstream cover
  loading fixes, and avoid a settings crash for an unknown device model.
- **Idle behavior:** disabled SonixLink waits for a signal; the volume worker polls
  less often when no Bluetooth stream exists. Helper processes release inherited
  sound handles before executing another program.
- **Automatic shutdown:** defaults to 30 idle minutes only when no preference is
  saved. An explicitly disabled setting stays disabled. Receiver audio counts as
  playback. Change this in Settings → Power.

## Download and install

**HiBy R1 only.** Download `r1.upt` and optionally `SHA256SUMS.txt` from this release.
Back up the card, including hidden folders, and charge above 30%. Copy `r1.upt`
to the card root, then choose **Settings → System → Update firmware → From SD
card** in Sonix. Wait for installation and reboot without interrupting power.

For phone audio: pair the phone, open **Bluetooth → Receiver**, tap the phone,
and start playback on the phone with wired headphones connected to the R1.

Verify the download with `sha256sum -c SHA256SUMS.txt` on Linux or
`shasum -a 256 -c SHA256SUMS.txt` on macOS. The previous
[v1.0.0 image](https://github.com/vetka925/sonix-player-audiobooks/releases/tag/audiobooks-v1.0.0)
remains available for rollback.

## Validation and limits

- Installed on the owner's R1; build and installed binary hashes were verified.
  On 2026-11-13 the owner reported success after the requested phone-audio and
  reconnection check without USB/ADB.
- Full Linux audiobook and new recovery regression suites passed, including
  injected failures, command races, private D-Bus lifecycle tests and sanitizer
  checks. Linux host and MIPS builds passed; target ABI remains glibc 2.22 or older.
- Actual LVGL simulator checks covered connection failure and successful retry.
- **No quantified battery-life claim:** the unplugged radios-off test recorded
  51 minutes 35 seconds of continuous suspend and a 100% → 100% gauge reading.
  This did not reproduce the historical drain and cannot measure small losses.
- R3 Pro II is not hardware-tested for this release; no image is provided.
- Kernel and Bluetooth daemon versions are unchanged. YouTube Music, the other
  fork's UI/database redesign and unrelated EQ changes are not included.
- No streaming-service credentials are included. The built-in internet updater
  still defaults to original Sonix; use this fork's SD-card releases to retain
  its additions.

## Credits and build provenance

Thanks to [Jepl4r and Sonix contributors](https://github.com/Jepl4r/sonix-player)
for the foundation and upstream changes, including
[#169](https://github.com/Jepl4r/sonix-player/pull/169),
[#172](https://github.com/Jepl4r/sonix-player/pull/172) and
[#188](https://github.com/Jepl4r/sonix-player/pull/188).
Selected recovery, lifecycle, idle and touch changes were adapted from
[Rekkod-Tree's v1.4.2](https://github.com/Rekkod-Tree/sonix-player-rekkod-mod/tree/1db1fa8cc3435c9dd21733ba92e651fbe8051d84),
preserving this fork's Receiver and audiobook behavior. GPL-3.0 and contributor
credits remain intact. Implementation and validation assistance: OpenAI Codex
(exact session model not exposed).

Player source: `dc11c02b087e4e31ec4762a9383d4b6f8eaa3733`. Later release commits
change documentation and CI dependencies only. Based on the existing R1 stock
1.6 packaging baseline; the artifact is identical to the owner-tested image.

- Device build: `121120260947` (the About screen still reports Sonix `1.3.0`).
- Image size: 48,029,696 bytes.
- Image SHA-256: `4825d7fceaf2ec9987c5db91be1e4927f2aa02fa6c79a5a89c8eaffd8fb9152e`.
