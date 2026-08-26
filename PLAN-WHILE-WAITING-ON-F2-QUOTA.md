# Plan: What to Do While the F2 Quota Is Pending

**Context:** F-instance quota request (case `178750362900605`) is under manual
AWS review with no ETA. Standard-instance quota (16 vCPUs) is already
sufficient for the `z1d.2xlarge` build host — confirmed not blocked. This plan
sequences everything that can move forward without an `f2.6xlarge`.

---

## Track A — Start the real AWS build now (highest priority, unblocked)

This is the single most valuable thing to do immediately: it turns the
multi-hour Vivado synthesis into background time that overlaps with the
quota wait instead of stacking after it.

1. **Confirm the Marketplace AMI subscription is done.** The `z1d.2xlarge`
   build host boots from AWS's FPGA Developer AMI — a one-time manual
   Marketplace click that only you can do (no API path). If not yet done,
   do it before anything else in this track.
2. **Kick off `firesim buildbitstream`.** This launches only the
   `z1d.2xlarge` (Standard quota, already sufficient) and runs the exact
   pipeline validated locally last night (elaborate → Golden Gate →
   `FireSim-generated.sv`) followed by real Vivado synthesis and
   `create-fpga-image`. Typical runtime: 4-6 hours.
3. **This is the step that resolves the one risk nothing local could touch:**
   whether the design closes timing on real hardware. Everything else about
   the design has already been validated for free; this is the one
   remaining unknown, and it's worth having the answer *before* the FPGA
   quota clears rather than after.
4. Run it in `tmux`/`screen` (or `nohup`, as done last night) — confirmed
   yesterday that the manager has no requirement to run from an EC2
   instance, but your laptop's connection dying mid-build would still kill
   the manager process.

**Outcome if this succeeds before quota clears:** the AGFI is sitting ready
the moment `f2.6xlarge` access is approved — `runworkload` becomes a single
command with no further waiting.

---

## Track B — Close the real gap in the DOOM software stack

This is where the actual unfinished work is — the CPU/build side has had
much more attention than the game side so far.

1. **Decide the output strategy — this is a real open decision, not a detail.**
   Checked our bridge set (`WithDefaultFireSimBridges`) directly: it includes
   UART, block device, NIC, DMI, FASED, and TracerV — **no display/framebuffer
   bridge**. FireSim doesn't ship one at all. `doomgeneric`'s `linuxvt` backend
   writes to `/dev/fb0`, which won't exist on this hardware as configured.
   Before doing more DOOM work, pick one of:
   - **(A) Headless proof, not a live display.** Run DOOM in its built-in
     demo-playback mode (no input needed), write a minimal `doomgeneric`
     backend that dumps rendered frames to the block device or over UART
     instead of a framebuffer, then reconstruct the frames into a video
     afterward. Realistic to build in hours, not days — this is the
     honest, achievable version of "DOOM runs on my custom CPU."
   - **(B) Real framebuffer device.** Add an actual display peripheral to
     the SoC in Chisel, write a driver for it, wire it through a new
     FireSim bridge. This is a multi-day hardware-design undertaking on
     its own, not a software task — treat it as out of scope unless there's
     appetite to spend several of the remaining days on it specifically.
   - **Recommendation: (A).** It proves the actual claim ("DOOM runs on a
     CPU I built") without a detour into building new hardware this late
     in the timeline.
2. **Embed DOOM into the boot image.** The `doomgeneric` binary built last
   night is standalone — it isn't yet part of `doombom-marshal-final.elf`'s
   initramfs. Once (1) is decided, rebuild the FireMarshal image with the
   DOOM binary and WAD copied in, and an init script that launches it.
3. **Source a WAD file.** Deprioritized last night after several failed
   mirror attempts — worth just checking whether you already own a
   legitimate shareware or full copy (`doom1.wad` or `DOOM.WAD`) rather
   than continuing to hunt for one online.
4. **Test the rebuilt image before spending FPGA time on it.** Once DOOM is
   embedded, run the same local Verilator metasim from last night against
   the new image — cheap, free, and confirms the DOOM binary actually
   executes correctly under our kernel before it ever needs real hardware.

---

## Track C — Let last night's metasim keep working for you

The metasim that booted OpenSBI and reached early kernel init hit a
50-million-cycle cap, not a real stopping point. Since there's no more
urgency to reclaim that compute for something else, it can keep running
completely unattended:

1. Re-launch it with a much larger `+max-cycles` value (or no cap) in the
   background, low priority.
2. At ~1.4 KHz effective target frequency, reaching a shell prompt could
   plausibly take another 1-3 days of wall-clock time depending on how far
   through boot userspace init actually is — treat this as a bonus data
   point that might land before or after the quota clears, not something to
   wait on.
3. If it does reach a shell before the FPGA is available, that's one more
   independent confirmation the whole software stack works, with zero
   marginal cost.

---

## Track D — Final pre-flight for the moment F2 clears

Nothing to *do* here yet, just have it ready so there's no scramble once
access is approved:

1. Once `buildbitstream` (Track A) finishes, copy the generated entry from
   `deploy/built-hwdb-entries/` into `config_hwdb.yaml` — the one manual
   step that couldn't happen earlier because the AGFI didn't exist yet.
2. Re-point `config_runtime.yaml`'s `default_hw_config` at that new
   `config_hwdb.yaml` entry name (currently a placeholder TODO from last
   night).
3. If Track B lands, update `workloads/doombom.json`'s
   `common_bootbinary` to the DOOM-embedded image.
4. Double check `run_farm_hosts_to_use` in `config_runtime.yaml` still
   requests exactly `f2.6xlarge: 1` — matches the 24-vCPU quota being
   requested, so there's no mismatch the moment it's approved.

---

## Track E — Only you can do these

- Reply to any AWS follow-up on the quota case promptly if they ask for more
  detail — that's usually the last step before approval.
- Confirm the Marketplace AMI subscription (Track A, item 1) if not already done.
- Decide on Track B's output-strategy question (A vs. B above) — this
  changes how much of the remaining timeline goes into it.

---

## Suggested order of operations, starting now

1. Marketplace AMI check → kick off `buildbitstream` (Track A) — do this first, it's the long pole.
2. While that runs: decide DOOM output strategy (Track B.1), source a WAD (Track B.3).
3. Re-launch the metasim with a larger cycle cap (Track C) — fire-and-forget.
4. Once `buildbitstream` finishes: Track D cleanup, embed DOOM if the
   strategy decision + build are done (Track B.2).
5. The moment F2 quota clears: `runworkload` should be a single command,
   not a fire drill.
