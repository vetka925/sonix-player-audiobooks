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
initial build version `121120260751` (superseded by the candidate below). Target output is not linked to the simulator backend.

The actual LVGL GUI was exercised with a fake Bluetooth boundary at 480x800 and
480x720: source selection excludes headphones, first connection failure permits
retry, and a successful retry displays aptX/44.1 kHz and playback. Screenshots:
[chooser](receiver-evidence/chooser-480x720.png),
[failure](receiver-evidence/failure-480x800.png),
[success](receiver-evidence/success-480x800.png), and
[empty source list](receiver-evidence/empty-480x720.png).
The power screen also showed [automatic shutdown enabled at 30 minutes](receiver-evidence/autooff-480x720.png) with no saved preference.
This validates UI behavior, not real radio interoperability. One independent
read-only review found no actionable issues; hardware validation remains required.

Initial candidate SHA-256 (superseded):
`17bd8bbf741e600d76ade1e6974f054788774abd559b77d748ca5cf6c659358f`.

## Standby measurement result

On 2026-11-12 the owner reported 100% before and 100% after the unplugged test,
with Bluetooth/Wi-Fi disabled and playback stopped. The device log records screen
off at 14:29:51, suspend entry at 14:30:52, and power-button wake at 15:22:28:
3,095 seconds continuously suspended (51 minutes 35 seconds). Kernel suspend
successes increased from 14 to 15, failures stayed zero. The endpoint was reported
before charging; the later USB reading also showed 100% and 4.393 V.

Fast drain did not reproduce under these conditions. The battery gauge at 100%
does not measure small losses accurately, so this is not a quantified current or
battery-health test. Historical discharge remains unexplained. Before this test,
during the Bluetooth receiving experiment, the log recorded sleep deferred because
Bluetooth was in use. Code intentionally blocks deep sleep for a connected audio
link, scanning/pairing jobs, or enabled SonixLink. A dark display therefore does
not guarantee deep sleep. This is a plausible condition to investigate if the
problem recurs, not proof of the historical cause.

Default automatic shutdown bounds idle radio-connected operation; actual incoming
audio is included in the playing predicate and keeps the timer from expiring.
Hardware installation/receiver acceptance is still pending.

## Upstream assessment (checked 2026-11-12)

Integrated official #169 (cover loading) and #172 (unknown-model guard), preserving
the audiobook fork. Official latest release remains 1.3.0; the kernel has no newer
sleep fix since the October 15 release. Our older #171 remains open.

[PR #188](https://github.com/Jepl4r/sonix-player/pull/188) is now adapted for mono
Bluetooth output. Selected changes from Rekkod v1.4.2, commit
`1db1fa8cc3435c9dd21733ba92e651fbe8051d84`, were adapted individually. The upstream
EQ redesign conflicts with per-book EQ and remains deferred.

## Selected fork fixes — final candidate, 2026-11-12

Owner authorized this implementation with “Делай сначала все что планировали
через superpowers”. OpenAI Codex authored the adaptations; exact model is not
exposed. See the [execution plan](superpowers/plans/2026-11-12-r1-fork-fixes.md).

- Disabled SonixLink waits for a signal; the Bluetooth volume worker sleeps
  longer when no stream exists. Active stream polling remains responsive.
- Bluetooth mono output negotiates stereo through ALSA plug. Wired and stereo
  paths retain their routing. Failed output preserves audiobook position and
  unwritten input instead of restarting the track.
- Receiver writes have a monotonic two-second no-progress budget, one reopen
  attempt, and cancellation visible before teardown takes the audio lock.
  This bounds retry loops, not a kernel driver call that never returns.
- Slow paused-output reopening releases the audio command mutex. Newer commands
  cancel stale results; late handles close before quiesce acknowledgement.
- LVGL preserves queued pointer frames. The build applies an idempotent patch to
  the matching dependency and rejects an unknown driver layout.
- Bluetooth service owner changes invalidate stale caches/replies. Recovery has
  priority over a full job queue; delayed adapter appearance retries incomplete
  initialization; BlueALSA-only restart preserves bounded
  connection grace, while BlueZ loss clears disconnected state.
- Child processes close inherited sound handles before exec and mark other
  non-stdio handles close-on-exec, without closing the parent's descriptors.

Tests exercise production code with fault boundaries, thread races, actual
fork/exec and a private D-Bus daemon. Baseline failures and negative controls
were checked for the new behavior. The complete Linux audiobook suite including
`run-fork-tests.sh` passed. Host and MIPS builds passed; target ABI check found
no symbol beyond glibc 2.22. Target symbol inspection found no simulator wrappers.
`git diff --check` passed. Existing service versions and kernel are unchanged.

Candidate: build `121120260947`, 48,029,696 bytes. Local artifact:
`/Users/vetka/Downloads/HiBy-R1-2026-11-12/r1.upt`.
SHA-256 matches the build server:
`4825d7fceaf2ec9987c5db91be1e4927f2aa02fa6c79a5a89c8eaffd8fb9152e`.
The October 22 rollback image remains beside it. No firmware installation or
public push has been performed.

Hardware acceptance remains: Fold7 connection/reconnection without ADB,
continuous wired receiver audio, pause/resume and position retention on books,
mono Bluetooth output, touch responsiveness, and a longer unplugged standby
comparison. No measured battery-current improvement is claimed. YouTube Music,
new daemon versions, the fork's UI/database redesign and unrelated EQ changes
are deferred.

One bounded independent review found a delayed-Adapter1 recovery gap. It was
fixed in `dc11c02`: incomplete initialization retries even while D-Bus is alive,
and explicit power changes clear that pending recovery. The new regression
failed before the fix and passed with ASan/UBSan on Linux (21 order cases).
The reviewer reported no other actionable findings; no second review loop was
requested.

The selected-fix LVGL simulator repeated the 480x800 Receiver journey with
a fake radio boundary: [failed connect](receiver-evidence/fork-failure-480x800.png)
then [successful retry](receiver-evidence/fork-success-480x800.png). This is UI
evidence only; the final delayed-adapter fix is covered by the lifecycle test.
