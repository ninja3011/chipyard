# Getting Real Vivado Synthesis Running on AWS — Full Report

**Date:** 2026-08-25
**Outcome:** `firesim buildbitstream` is running for real against our
`FireSimMediumBoomV3Config` design — Vivado 2025.2 on a live `z1d.2xlarge`,
targeting the actual `xcvu47p` part used by AWS F2. Getting here required
finding and fixing **five separate real bugs**, all stemming from one root
cause: FireSim's manager is designed and documented to run from an EC2
instance inside the VPC, and we're running it from a personal laptop
instead.

---

## Starting point

- AWS payment method added; F2 FPGA quota still pending manual review
  (case `178750362900605`), unrelated to anything in this report.
- AWS Marketplace subscription to the FPGA Developer AMI confirmed.
- Confirmed the Standard-instance quota (16 vCPUs) is sufficient for a
  `z1d.2xlarge` build host — this whole step was never blocked by the F2
  quota wait.

## Bug 1 — Fabric's `run()` requires real SSH, even to "localhost"

**Symptom:** `firesim buildbitstream` hung indefinitely after printing
`run: make ... replace-rtl`, zero child processes, near-zero CPU.

**Root cause:** `bitbuilder.py`'s `replace_rtl()`/`build_driver()` use
Fabric's `run()`, which opens a real SSH connection even when the target is
`localhost`. This machine has no SSH server at all (`openssh-server` isn't
installed, no sudo available to install it). On the documented setup — the
manager running as an actual EC2 instance — `run()` to localhost silently
works because that machine's own `sshd` is present by default.

**Fix:** both functions' own docstrings already say *"Should run on the
manager host"* — i.e., wherever the manager process itself executes.
Patched both to use Fabric's already-imported-but-unused `local()` instead
of `run()`.

```python
# sims/firesim/deploy/buildtools/bitbuilder.py
- run(self.build_config.make_recipe("replace-rtl", deploy_dir))
+ local(self.build_config.make_recipe("replace-rtl", deploy_dir), shell="/bin/bash")
```

## Bug 2 — WSL's inherited Windows PATH breaks composed shell commands

**Symptom:** After fixing Bug 1, `replace_rtl` failed instantly (return code
2) instead of hanging.

**Root cause:** `util/export.py`'s `create_export_string()` builds an
`export PATH=...` string by interpolating the raw environment variable value
with no shell-quoting. This machine's PATH (via WSL) includes
Windows-inherited segments like `/mnt/c/Program Files (x86)/NVIDIA
Corporation/...` — the unescaped parentheses and spaces broke bash's parser
the moment this string was embedded in Fabric's composed command.

**Fix:** quote each exported value with `shlex.quote()`.

```python
- export_shell_env_vars.add(f"{v}={os.environ[v]}")
+ export_shell_env_vars.add(f"{v}={shlex.quote(os.environ[v])}")
```

## Bug 3 — `local()` defaults to `/bin/sh`, which has no `source` builtin

**Symptom:** After fixing Bug 2, the composed command failed with
`/bin/sh: 1: source: not found`.

**Root cause:** Fabric's `local()` defaults to `/bin/sh` (dash on this
system) unless told otherwise, unlike `run()` which typically invokes a
bash login shell on the remote end. The composed command needs bash's
`source` builtin to run `sourceme-manager.sh`.

**Fix:** pass `shell="/bin/bash"` explicitly (included in the Bug 1 diff
above).

## Bug 4 — Local driver link needs AWS's proprietary SDK, and isn't needed anyway

**Symptom:** After fixing Bugs 1-3, `replace-rtl` failed on
`cannot find -lfpga_mgmt: No such file or directory`.

**Root cause:** `sim/make/fpga.mk`'s `replace-rtl` target depends on
building the local x86 FPGA driver, which links AWS's proprietary
`libfpga_mgmt` — only present on a real FPGA-Developer-AMI EC2 instance.
The target's own comment says it exists *"to set up the build directory
without running the cad job... used by the manager before passing a build
to a remote machine"* — the local driver was never actually part of that
job. Confirmed by reading `runtools/runtime_config.py`: the driver
genuinely used for deployment is built **fresh, independently, on the
run-farm host itself** — our local copy is dead weight.

**Fix:** decoupled `replace-rtl` from the local driver dependency, and made
`bitbuilder.py`'s `build_driver()` skip cleanly (not silently — it checks
for `libfpga_mgmt` first and logs why it's skipping) when that library
isn't present.

```makefile
# sim/make/fpga.mk
- replace-rtl: $(fpga_delivery_files) $(fpga_sim_delivery_files)
+ replace-rtl: $(fpga_delivery_files)
```

## Bug 5 — Manager assumed to be co-located in the VPC (three symptoms, one cause)

Once the local steps passed, `buildbitstream` launched a real
`z1d.2xlarge` and got stuck on the very first remote command
(`mkdir -p ...`) for 8+ minutes.

**5a — Security group only allows SSH from inside the VPC.** The live
`firesim` security group restricts port 22 to `192.168.0.0/16` (not
`0.0.0.0/0` as the setup script's source suggested) — a silent drop from
outside the VPC, which is why `nc` timed out rather than refusing.
**Fix:** added an ingress rule scoped to this machine's own public IP.

**5b — The manager targets the build host's *private* IP.**
`buildfarm.py`/`run_farm.py` hardcode `instance.private_ip_address` —
correct when the manager lives inside the same VPC, unroutable from
outside it (this is a routing issue, not something the security-group fix
touches).
**Fix:** changed both to `instance.public_ip_address`.

```python
# buildtools/buildfarm.py, runtools/run_farm.py
- build_host.ip_address = build_host.launched_instance_object.private_ip_address
+ build_host.ip_address = build_host.launched_instance_object.public_ip_address
```

**5c — boto3 attribute caching.** After fixing 5b, a fresh launch hit
`AssertionError: Unassigned IP address` — `wait_until_running()` waits for
instance state but doesn't refresh the resource object's own cached
attributes, so `public_ip_address` could still read as `None` immediately
after.
**Fix:** added `instance.reload()` right after the wait.

**5d — SSH authentication: wrong username.** Once the network path worked,
connections still failed with *"Needed to prompt for a connection or sudo
password... ambiguous in parallel mode."* `env.user` is never set anywhere
in the codebase — Fabric defaults it to the **local machine's** current
username (`ninadjangle`), not the AMI's actual account (`ubuntu`). This
silently works on the documented setup because the manager's own login user
there typically already is `ubuntu`.
**Fix:** explicitly set `env.user = "ubuntu"`. Also stopped passing
`--skip-ssh-setup` to `sourceme-manager.sh` so `firesim.pem` actually loads
into an ssh-agent (needed alongside the correct username).

---

## Cost hygiene during this process

Each failed attempt after the AWS instance had already launched left an
orphaned, still-billing `z1d.2xlarge` — each one was identified via
`aws ec2 describe-instances` and terminated immediately once its attempt's
manager process exited, rather than left running. Total orphaned time
across all failed attempts was on the order of minutes each, negligible
against the user-set €10 spend cap for this session.

## Current state

Real Vivado 2025.2 synthesis is in progress on a live `z1d.2xlarge`
(`i-0e06e239fb820c453`), targeting `xcvu47p-fsvh2892-2-e` — confirmed by
Vivado's own synthesis log, not inferred. The first IP core
(`clk_wiz_0_firesim`, the clock-generation block) has already synthesized
cleanly: *"Synthesis finished with 0 errors, 0 critical warnings and 0
warnings."* Full synthesis + implementation + bitstream generation for the
complete design is expected to take several more hours.

## What none of this changes

Every one of these fixes addresses *how the build gets run*, not *what gets
built*. The design itself — `FireSimMediumBoomV3Config`, validated
overnight via Golden Gate transform and local Verilator metasim (which
booted OpenSBI and our real Linux kernel) — is unchanged. These bugs would
have blocked this step regardless of the design; they're specific to
running FireSim's manager off-EC2, which is the deployment path this
project chose for cost reasons, not a property of the CPU being built.
