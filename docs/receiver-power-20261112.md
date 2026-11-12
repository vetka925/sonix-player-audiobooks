# R1 receiver and standby repair

Author: OpenAI Codex; exact model not recorded. Date: 2026-11-12.

## Approved scope and plan

Vitalii requested phone reconnection without ADB, standby drain diagnosis and
assessment of upstream changes. Implementation stays in an isolated branch;
firmware installation is a separate hardware validation step.

- Add a paired audio-source chooser on Receiver. Keep the receiver profile
  enabled while choosing. Use a worker operation for the selected phone,
  checking that the page still wants reception before connecting. Never reuse
  the headphone reconnect path or overwrite the last-headphones setting.
- Show connection progress and failure, allow retry, and exclude headphones.
  Keep Receiver discoverable while open so first pairing can start on a phone.
- Test source filtering and worker cancellation/failure/success. Build on Linux
  and MIPS; exercise the actual GUI before hardware handoff.
- Measure standby with radios off and USB removed. Preserve diagnostic logs;
  do not infer battery health or suspend consumption from a charging reading.
- Default automatic shutdown to 30 idle minutes when no preference is saved.
  Preserve an explicit disabled preference. This bounds unattended standby,
  rather than claiming a measured improvement to suspend current.
- Integrate upstream cover loading (#169) and unknown-model crash fix (#172).
  Defer the EQ redesign because it conflicts with per-book EQ in this fork.

## Evidence before changes

R1 build 221020260658, Sonix 1.3.0. Fold7 paired successfully but could not
connect while the local sink profile was absent. With Receiver open, an ADB
Device1.Connect succeeded and the user confirmed sound through wired headphones
(aptX 44.1 kHz). The receiver page currently has no way to initiate that action.

Kernel suspend statistics: 14 successes, zero failures; about 53 hours spent
suspended during a 73-hour boot. Auto power-off unset (default off). Wi-Fi
configured on but wlan0 absent after resume, causing repeated failed polling.
These facts do not establish the cause of the reported historical discharge.

## Checks and remaining work

Baseline macOS suite stops at pthread_condattr_setclock, unavailable on macOS.
The complete Linux suite passed, including the new connection and capture-retry
regressions. Both were observed failing before their corresponding fixes.
Linux host and MIPS target builds passed; the R1 firmware packer completed with
build version `121120260751`. Target output is not linked to the simulator backend.

The actual LVGL GUI was exercised with a fake Bluetooth boundary at 480x800 and
480x720: source selection excludes headphones, first connection failure permits
retry, and a successful retry displays aptX/44.1 kHz and playback. Screenshots:
[chooser](receiver-evidence/chooser-480x720.png),
[failure](receiver-evidence/failure-480x800.png),
[success](receiver-evidence/success-480x800.png), and
[empty source list](receiver-evidence/empty-480x720.png).
This validates UI behavior, not real radio interoperability. One independent
read-only review found no actionable issues; hardware validation remains required.

Firmware SHA-256 (build server and local downloaded copy match):
`17bd8bbf741e600d76ade1e6974f054788774abd559b77d748ca5cf6c659358f`.

Standby measurement and on-device acceptance are pending. The owner started a
30-minute test with playback stopped, Bluetooth/Wi-Fi off, USB unplugged, and
screen off. Baseline reported capacity was 100%; the endpoint must be recorded
before charging. The current evidence does not identify the historical drain's
cause, and default auto-off is a mitigation rather than a suspend-current fix.

## Upstream assessment (checked 2026-11-12)

Integrated official #169 (cover loading) and #172 (unknown-model guard), preserving
the audiobook fork. Official latest release remains 1.3.0; the kernel has no newer
sleep fix since the October 15 release. Our older #171 remains open.

[PR #188](https://github.com/Jepl4r/sonix-player/pull/188) addresses mono audio sent
to Bluetooth headphones; it is a separate future candidate requiring that path's
validation. The Rekkod fork's v1.4.2 Bluetooth/lifecycle claims merit a targeted
review but its large divergence is not a safe blanket merge. The upstream EQ
redesign conflicts with per-book EQ and was deferred.
