# R1 receiver and power repair state

- Date: 2026-11-12.
- Stage: installed on R1 on 2026-11-13; boot verified; real audio acceptance pending.
- Scope approved by the owner's direct request to fix receiver reconnection and
  investigate standby drain. No repeated start gate; timeout not applicable.
- Execution: one Codex writer; exact model unknown (not exposed in this session).
- The selected fork package received one bounded independent review. Its delayed
  adapter initialization finding was fixed with a failing-then-passing regression.
- Branch: `codex/r1-receiver-power-20261112`, based on audiobook fork `aa2873f`.
- Evidence: [report](../../docs/receiver-power-20261112.md).
- Firmware build `121120260947` is installed; public publication has not been performed.
- Standby result: owner reported 100% to 100%; log confirms 3,095 seconds
  uninterrupted suspend and zero failures. Historical drain did not reproduce.
- Next: owner tests phone-to-R1 audio and reconnection without ADB, then
  audiobook pause/resume and longer unplugged standby.

- Selected fork changes are implemented: idle workers, mono routing and position
  retention, bounded receiver recovery, responsive resume and touch frames,
  service-owner recovery, child descriptor ownership. YouTube Music is deferred.
