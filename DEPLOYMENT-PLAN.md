# DOOMBOOM — AWS Deployment Plan

**Status as of 2026-08-25, 13:33 UTC: Phases 0-3 are DONE.** `buildbitstream`
ran for real, hit and recovered from six distinct bugs along the way (see
`reports/AWS-BUILDBITSTREAM-BREAKTHROUGH-REPORT.md`), and produced a real,
registered, `available` AGFI: **`agfi-021cc939bc8dc4951`**. Real Vivado
timing closure confirmed on actual hardware: WNS=+0.006ns, WHS=+0.010ns,
TNS=THS=0.000 — zero violations, at the true 75MHz target on the actual
`xcvu47p` part. `config_hwdb.yaml` and `config_runtime.yaml` are already
updated to point at it. Build host terminated; nothing is currently running
or billing. What remains is entirely gated on F2 quota approval — Phase 4
onward below.

---

## Phase 0 — Preconditions (you, one-time, manual)

- [ ] AWS Marketplace subscription to the FPGA Developer AMI confirmed (you're doing this next).
- [ ] F2 quota case `178750362900605` approved (still pending as of last check — reply promptly if AWS asks for more info).

Nothing below can start until Phase 0's first box is checked. The second box only gates Phase 3 onward — Phase 1 can run while still waiting on it.

---

## Phase 1 — Start the real build (not gated on F2 quota at all)

```bash
cd /home/ninadjangle/chipyard/sims/firesim/deploy
source /home/ninadjangle/chipyard/sims/firesim/sourceme-manager.sh --skip-ssh-setup
firesim buildbitstream
```

- This launches only a `z1d.2xlarge` (Standard quota — already confirmed sufficient, unrelated to the F2 wait).
- Runs the exact pipeline validated locally: elaborate → Golden Gate → `FireSim-generated.sv` → real Vivado synthesis → `create-fpga-image`.
- **Expected duration: 4-6 hours.** Run this inside `tmux`/`screen` on whatever machine drives the manager — a dropped connection kills the manager process, not the remote build, but you'd lose live visibility.
- **What this resolves:** the one risk nothing local could test — real Vivado timing closure at 75MHz. The yosys estimate (356,877 LUTs against the VU47P's ~1.3M) suggests comfortable resource headroom, but LUT fit and timing closure are different questions; this is the step that actually answers the second one.

**Confidence this starts cleanly:** high. The `DefaultF2Config` bug that would have crashed this immediately is fixed and re-verified twice. `config_build_recipes.yaml`'s `s3_bucket_name`/`append_userid_region` override is in place for running the manager off-EC2.

---

## Phase 2 — While Phase 1 runs (parallel, zero AWS cost)

Nothing here is blocking, but use the 4-6 hour build window productively:

- Watch the build log for the actual Vivado utilization report once synthesis completes — this is the number to compare against the yosys estimate.
- If F2 quota approves during this window, Phase 3's infrastructure prep (network bridge setup) can start immediately once the run-farm host exists — see Phase 4.

---

## Phase 3 — Register the AGFI (you or me, quick, needs the Phase 1 output)

Once `buildbitstream` finishes:

```bash
cat /home/ninadjangle/chipyard/sims/firesim/deploy/built-hwdb-entries/doomboom_f2
# copy that block into:
#   /home/ninadjangle/chipyard/sims/firesim/deploy/config_hwdb.yaml
```

Then update the one remaining placeholder:

```yaml
# sims/firesim/deploy/config_runtime.yaml
target_config:
    default_hw_config: doomboom_f2   # ← change to the new config_hwdb.yaml entry name if different
```

(`workload_name: doombom.json` is already correct and already points at the DOOM-embedded, bandwidth-fixed image.)

---

## Phase 4 — Launch the run farm (needs F2 quota approved)

```bash
firesim launchrunfarm
firesim infrasetup
firesim runworkload
```

- This is the step that actually requests the `f2.6xlarge` — genuinely blocked until quota clears, no way around it.
- `runworkload` boots the design for real, at 75MHz — the same boot sequence validated in metasim (OpenSBI → kernel → early init), except this time it should reach a shell in **seconds**, not the ~9 hours metasim needed for the same milestone.

---

## Phase 5 — Bring up the network bridge (run-farm-host-side, manual)

Per FireSim's own documented networking setup (`docs/Advanced-Usage/Miscellaneous-Tips.rst`), run on the **run-farm instance** once `runworkload` is live:

```bash
sudo ip tuntap add mode tap dev tap0 user $USER
sudo ip link set tap0 up
sudo ip addr add 172.16.0.1/16 dev tap0
sudo ifconfig tap0 hw ether 8e:6b:35:04:00:00
sudo sysctl -w net.ipv6.conf.tap0.disable_ipv6=1
```

Confirm the simulated node reaches a login prompt in the `fsim0` screen session, then:

```bash
ssh YOUR_RUN_FARM_INSTANCE_IP
# from inside the run-farm instance:
TERM=linux ssh root@172.16.0.2
```

**This should need zero manual steps on the target side** — `S10mdev`'s coldplug scan auto-loads `icenet.ko` the moment it sees the IceNIC device, and `S40network` auto-assigns the target's MAC-derived `172.16.x.x` address on boot. If `172.16.0.2` doesn't respond, that's the first thing to check (`dmesg | grep icenet` on the target via console).

---

## Phase 6 — Launch DOOM and connect the viewer

On the simulated target (via the `172.16.0.2` SSH session):

```bash
cd /root
./doomgeneric-netstream -iwad freedoom1.wad
```

This blocks, waiting for a viewer — nothing runs unwatched.

On your own laptop, tunnel through the run-farm instance and connect:

```bash
ssh -N -L 5678:172.16.0.2:5678 YOUR_RUN_FARM_INSTANCE_IP
# in another terminal:
python3 /home/ninadjangle/chipyard/software/doom/netstream_viewer.py 127.0.0.1 5678
```

A window opens showing live DOOM gameplay, 320×200 upscaled 2x, keyboard input routed back over the same tunnel. Silent (no sound) — that's a known, deliberate scope decision unless we build audio streaming separately.

---

## Known residual risks, stated plainly

- **Vivado timing closure** — the one thing nothing local could test. Resolves in Phase 1.
- **`icenet.ko` real-hardware behavior** — built and depmod-indexed correctly, loads without incident up through what metasim could reach, but never confirmed against real IceNIC hardware timing. Resolves in Phase 5.
- **Network bandwidth at real gameplay framerate** — math says ~61 Mbps needed against 200 Mbps configured; untested at real speed until Phase 6.
- **No sound.** Confirmed scope gap, not a bug — silent unless separately built.

---

## Cost awareness while running this

- Phase 1: one `z1d.2xlarge` for the build duration (4-6 hrs).
- Phase 4 onward: one `f2.6xlarge` for the run duration.
- The SNS→Lambda kill-switch and `cost_check.sh` from account setup are still in place — use them if a session needs to be cut short.
