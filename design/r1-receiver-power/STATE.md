# R1 receiver and power repair state

- Date: 2026-11-12.
- Stage: installed and accepted by the owner on 2026-11-13; release prepared.
- Scope approved by the owner's direct request to fix receiver reconnection and
  investigate standby drain. No repeated start gate; timeout not applicable.
- Execution: one Codex writer; exact model unknown (not exposed in this session).
- The selected fork package received one bounded independent review. Its delayed
  adapter initialization finding was fixed with a failing-then-passing regression.
- Branch: `codex/r1-receiver-power-20261112`, based on audiobook fork `aa2873f`.
- Evidence: [report](../../docs/receiver-power-20261112.md).
- Firmware build `121120260947` is installed; publication as `audiobooks-v1.1.0`
  was explicitly authorized by the owner.
- Standby result: owner reported 100% to 100%; log confirms 3,095 seconds
  uninterrupted suspend and zero failures. Historical drain did not reproduce.
- Owner reported “все работает” after the requested unplugged Receiver audio
  and reconnection check, and explicitly authorized GitHub publication.
- Longer standby measurement and exhaustive hardware fault testing remain outside
  this acceptance; no quantified battery-life improvement is claimed.

- Selected fork changes are implemented: idle workers, mono routing and position
  retention, bounded receiver recovery, responsive resume and touch frames,
  service-owner recovery, child descriptor ownership. YouTube Music is deferred.
