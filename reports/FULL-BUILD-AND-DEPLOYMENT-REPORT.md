# DOOMBOOM — Full Build Report: Local Validation Through Real AGFI

**Covers:** everything from the local-only overnight validation through the
real AWS Vivado build, timing closure, and AGFI creation — plus how the
resulting image actually runs on physical FPGA hardware.

---

## Part 1 — Local validation (before any AWS spend)

With payment enabled but not yet trusted, the whole design was validated
without touching AWS at all:

- **Golden Gate / MIDAS transform**: our custom `FireSimMediumBoomV3Config`
  was run through FireSim's local elaboration → Chisel → FIRRTL → Golden
  Gate pipeline, producing `FireSim-generated.sv` (33.8MB) — the exact file
  a real build consumes. Zero errors.
- **Local Verilator metasim**: that same transformed netlist was compiled
  into a host-runnable simulator (`VFireSim`) and booted our real Linux
  image. It correctly identified itself as `ucb-bar,chipyard`, ran OpenSBI,
  and handed off to our actual kernel build — proving the hardware/software
  handoff works before spending a cent. Measured effective throughput:
  ~1.4 KHz — the basis for later predicting real-FPGA speed.
- **DOOM software stack**: `doomgeneric` was cross-compiled for the
  RV64 Linux target; a custom network-streaming backend
  (`doomgeneric_netstream.c`) was built since FireSim has no display bridge
  at all — frames stream as raw RGBA over TCP through the existing NIC
  bridge instead. A Tkinter/PIL viewer (`netstream_viewer.py`) receives
  them. `freedoom1.wad`/`freedoom2.wad` (free, libre game data) were
  sourced to avoid shareware-licensing ambiguity.
- **yosys resource estimate**: the real generated Verilog was synthesized
  with open-source yosys (`synth_xilinx -family xcup`), giving **356,877
  LUTs / 154,915 flip-flops** — confirmed the design is far too large for a
  50K-class Artix-7 board (~11x over budget) but comfortably sized relative
  to the VU47P's multi-million-LUT capacity.
- **DOOM embedded**: the final FireMarshal image (`doombom-doom-final.elf`,
  89MB) bundles the DOOM binary, both WAD files, and the `icenet`/`iceblk`
  kernel modules (pre-built, depmod-indexed against the exact kernel) —
  network bring-up is fully automatic at boot via the existing `S10mdev`
  coldplug scan and `S40network` init script.

## Part 2 — Getting `buildbitstream` to actually run

FireSim's manager is designed and documented to run from an EC2 instance
inside the VPC. Running it from a personal laptop instead surfaced six real
bugs, each fixed in FireSim's own source:

1. **`run()` requires real SSH, even to "localhost."** No `sshd` exists on
   this machine. Fixed by using Fabric's `local()` instead, since both
   functions' own docstrings say they should run "on the manager host."
2. **WSL's inherited Windows `PATH`** (entries like `Program Files (x86)`)
   broke Fabric's composed shell commands — unescaped spaces/parens. Fixed
   by shell-quoting exported values with `shlex.quote()`.
3. **`local()` defaults to `/bin/sh`**, which has no `source` builtin.
   Fixed by passing `shell="/bin/bash"` explicitly.
4. **An unnecessary local driver dependency** — `replace-rtl` tried to link
   a local x86 driver against AWS's proprietary `libfpga_mgmt`, which only
   exists on a real FPGA-Developer-AMI host. Traced through
   `runtools/runtime_config.py` and confirmed the real deployment driver is
   built fresh, independently, on the run-farm host — the local copy was
   dead weight. Decoupled it.
5. **Networking cluster**: the live security group only allowed SSH from
   inside the VPC (not `0.0.0.0/0` as the source implied) — added a scoped
   rule for this machine's IP. The manager also targeted the build host's
   *private* IP (`buildfarm.py`/`run_farm.py`), unroutable from outside the
   VPC — switched both to `public_ip_address`. A boto3 caching gotcha then
   surfaced (`instance.reload()` needed after `wait_until_running()`).
   Finally, Fabric defaulted the SSH username to this machine's local user
   instead of the AMI's actual `ubuntu` account — fixed by setting
   `env.user` explicitly.
6. **A broken `aws` CLI shadowing the working one.** Later, at the very
   final step, `local()`'s `aws s3 cp` crashed with a `pyOpenSSL`/
   `cryptography` version-mismatch `AttributeError` — a real, deterministic
   bug in the conda environment's bundled AWS CLI (`cryptography` 50.0.0
   vs. `pyopenssl` 23.2.0), not a network fluke as first assumed. Confirmed
   by reproducing the exact traceback, then uninstalling the broken,
   redundant `awscli` package entirely so `PATH` can only ever resolve to
   the working AWS CLI v2 at `~/.local/bin/aws`.

Full narrative and exact diffs for items 1-5: see
`reports/AWS-BUILDBITSTREAM-BREAKTHROUGH-REPORT.md`.

## Part 3 — The Vivado build itself

Once unblocked, `firesim buildbitstream` ran for real: launched a
`z1d.2xlarge`, uploaded the design, and ran Vivado 2025.2 against the real
target part.

| Stage | Result |
|---|---|
| Synthesis (full design) | 1029 Infos, 776 Warnings, 106 Critical Warnings, **0 Errors** |
| Link (CL into AWS shell, DFX partition) | 0 Errors — 2283× RAM64M8, 669× RAM32M16, etc. mapped correctly |
| Logic optimization | 0 Errors |
| Placement | 259 Infos, 5 Warnings, **0 Errors** (28 min) |
| Physical optimization | 15 Infos, 0 Warnings, **0 Errors** |
| Routing | 23 Infos, 1 Warning, **0 Errors** — congestion warning at one point, resolved cleanly by the router's own rip-up-and-reroute iteration |
| **Final timing** | **WNS=+0.006ns, WHS=+0.010ns, TNS=0.000, THS=0.000** — Vivado's own words: *"The design met the timing requirement."* Zero paths anywhere with negative slack of any kind. |

Total Vivado build time: **1 hour 45 minutes 41 seconds.**

This is the answer to the one question nothing local could ever test: the
design closes timing on real hardware, with margin, at its 75MHz target.

## Part 4 — From routed design to registered AGFI

1. The final routed checkpoint (`post_route.dcp`) and reports were rsync'd
   back to the local machine (906MB across all build artifacts).
2. The design was packaged into a tarball
   (`2026_08_25-103216.Developer_CL.tar`, 133MB) and uploaded to S3
   (`s3://doomboom-firesim-949064411949-useast1/dcp/...`) — this step hit
   bug #6 above on the manager's first attempt, then succeeded on manual
   retry with the working AWS CLI.
3. `aws ec2 create-fpga-image` was called against that S3 object, returning:
   - `FpgaImageId`: `afi-0c874ae6b2e623019`
   - `FpgaImageGlobalId`: **`agfi-021cc939bc8dc4951`**
4. Polled `describe-fpga-images` for ~27 minutes while AWS performed its own
   internal image packaging; state moved from `pending` to **`available`**.
5. Registered the result in `config_hwdb.yaml` and pointed
   `config_runtime.yaml`'s `default_hw_config` at it.

**Total real AWS compute spend for this entire build: roughly $2-2.50.**
Zero instances are running now; the build host was terminated immediately
once its output was safely copied off.

---

## Part 5 — How this actually runs on the FPGA

Everything above happens without ever touching real FPGA silicon — Vivado
targets the *part number* (`xcvu47p-fsvh2892-2-e`) as a description of the
chip's fabric, and `create-fpga-image` packages the result into AWS's
loadable image format. The actual hardware only gets involved at deploy
time (`firesim runworkload`, still ahead of us, gated on F2 quota):

1. **Instance launch.** AWS provisions an `f2.6xlarge` — a real server with
   an actual Xilinx VU47P chip attached over the PCIe bus, not simulated in
   any way.
2. **FPGA configuration.** The run-farm host calls AWS's FPGA management
   API (via `libfpga_mgmt`, the same library that blocked the *local* driver
   build in bug #4 — it exists for real here) to load `agfi-021cc939bc8dc4951`
   onto the physical chip. This physically reconfigures a region of the
   FPGA's programmable logic — LUTs, flip-flops, BRAM, DSP slices — into the
   exact routed netlist Vivado produced. From this point, our BOOM core,
   caches, and every bridge genuinely *exist* as configured silicon, not as
   a program being interpreted.
3. **Host driver.** A small C++ driver (built fresh on this specific
   run-farm host, since it needs the real `libfpga_mgmt`) talks to the FPGA
   over PCIe — this is the software side of the UART/blockdev/NIC/DMA
   bridges we validated locally. It's the same bridge protocol metasim used
   in software; here it's real memory-mapped PCIe transactions instead.
4. **Boot.** The driver loads our kernel/initramfs image into the FPGA's
   attached DRAM (via the FASED-modeled memory controller, now running on
   real hardware timing) and releases reset. From here, **the RISC-V core
   is not being simulated by anything** — it is real synthesized logic
   fetching, decoding, and executing real instructions at the FPGA's actual
   clock rate.
5. **Real-time execution.** This is the concrete payoff of the whole
   FireSim architecture: the exact boot sequence that took **9.8 hours** in
   Verilator metasim (reaching only early kernel init) should complete in
   **well under a second** on real hardware, because the FPGA runs the
   design at its literal 75MHz clock instead of a software simulator
   modeling it cycle-by-cycle in C++. A full Linux boot to shell, untestable
   locally in any practical timeframe, becomes a normal few seconds here.
6. **Console/network access.** The UART bridge surfaces a live console via
   a `screen` session on the run-farm host; the NIC bridge (`icenet`,
   already built into our image) connects through a `tap0` device on that
   same host to a `172.16.0.x` address — this is how we'll SSH into the
   booted target and eventually reach `doomgeneric-netstream`'s socket.
7. **DOOM, for real.** Once launched, DOOM's game loop — physics, AI,
   rendering — runs as literal RV64 machine code executing on real
   out-of-order superscalar hardware we designed, not an interpreter or a
   simulation of one. The frames it streams out over the network bridge are
   the actual rendered output of a real CPU, at real gameplay speed.

**What's still unverified until this actually happens:** whether `icenet`
behaves identically on real hardware timing as it did in the (much slower,
software-simulated) metasim, and whether the ~61 Mbps DOOM video stream
holds up against the FPGA's real NIC bridge bandwidth. Everything up to
that point — the design itself, its timing closure, its resource fit — is
now proven on real silicon-equivalent (post-route, pre-configuration) data,
not simulation.

---

## Current status

Everything above is done. The only remaining blocker is the F2 vCPU quota
(currently 8 of the 24 needed, case `178750362900605` still open) —
entirely outside anyone's control until AWS acts on it. The moment it
clears, deployment is `firesim launchrunfarm && firesim infrasetup &&
firesim runworkload` away from a real, physical FPGA running this design.
