#!/bin/bash
# Reproduces the yosys-based LUT/FF resource estimate for our FireSim design.
#
# Input:  FireSim-generated.sv — the exact file AWS's Vivado build consumes,
#         produced by `make replace-rtl` (see the overnight validation report
#         for that step; this script assumes it already exists).
# Output: a technology-mapped cell count (LUT1-6, FDRE/FDSE, RAMB, DSP48, ...)
#         for the FireSimMediumBoomV3Config / BaseF2Config design, targeting
#         Xilinx UltraScale+ (the family AWS F2's VU47P belongs to).
#
# Two real obstacles were hit and worked around on the way to a clean run —
# both are handled below rather than left as manual steps:
#   1. BUFGCE (a Xilinx clock-buffer primitive) isn't modeled by yosys and
#      must be stubbed as a zero-area blackbox — accurate to how it behaves
#      on real silicon too, since it's a dedicated clock-tree resource, not
#      LUT fabric.
#   2. yosys's built-in Xilinx LUTRAM techmap has a real bug
#      (lutrams_xc5v_map.v:181, "invalid OPTION_ABITS/WIDTH combination")
#      that one of our memories' port configuration triggers. `-nolutram`
#      routes around it by mapping those memories to Block RAM/flip-flops
#      instead — note this makes the RAMB18E2/RAMB36E2 counts here diverge
#      from what Vivado would actually choose; only the LUT/FF totals should
#      be trusted as a real estimate.

set -euo pipefail

SV_FILE="${SV_FILE:-/home/ninadjangle/chipyard/sims/firesim/sim/generated-src/f2/f2-firesim-FireSim-FireSimMediumBoomV3Config-BaseF2Config/FireSim-generated.sv}"
WORKDIR="${WORKDIR:-/tmp/yosys-lut-estimate}"
YOSYS_ENV=yosys-env

mkdir -p "$WORKDIR"
cd "$WORKDIR"

if [ ! -f "$SV_FILE" ]; then
  echo "ERROR: $SV_FILE not found — run 'make replace-rtl' first." >&2
  exit 1
fi

# --- Install yosys (self-contained conda env, no sudo needed) ---
if ! command -v mamba >/dev/null 2>&1; then
  echo "ERROR: mamba not found — expected at ~/miniforge3/bin/mamba" >&2
  exit 1
fi
if [ ! -x "$HOME/miniforge3/envs/$YOSYS_ENV/bin/yosys" ]; then
  mamba create -n "$YOSYS_ENV" -c conda-forge yosys -y
fi
YOSYS="$HOME/miniforge3/envs/$YOSYS_ENV/bin/yosys"

# --- Sanity check: confirm no OTHER unmodeled Xilinx primitives crept in ---
# (only BUFGCE turned up when this was actually run; if this design changes
# and something new appears here, it needs its own stub before proceeding)
echo "Checking for Xilinx primitives beyond BUFGCE..."
FOUND=$(grep -oE '^\s*(BUFG[A-Z0-9_]*|MMCM[A-Z0-9_]*|PLLE[0-9_A-Z]*|IBUF[A-Z0-9_]*|OBUF[A-Z0-9_]*|RAMB[0-9A-Z_]*|DSP48[A-Z0-9_]*|FIFO[0-9A-Z_]*|XPM_[A-Z_]*|IDELAYE[0-9_A-Z]*|GTHE[0-9_A-Z]*)\s+#?\(?' "$SV_FILE" \
  | awk '{print $1}' | sort -u)
echo "$FOUND"
if [ "$FOUND" != "BUFGCE" ]; then
  echo "WARNING: found primitives other than BUFGCE — the stub file below may need updating." >&2
fi

# --- Blackbox stub for BUFGCE ---
cat > xilinx_stubs.v <<'EOF'
// Blackbox stub for Xilinx's dedicated global-clock-buffer primitive.
// BUFGCE is a hard macro routed through dedicated clock-tree resources on
// real Xilinx silicon — it consumes no LUT fabric, so for a LUT-count
// estimate it must be excluded from synthesis, not elaborated as logic.
(* blackbox *)
module BUFGCE(
  input  I,
  input  CE,
  output O
);
endmodule
EOF

# --- yosys synthesis script ---
cat > synth_estimate.ys <<EOF
read_verilog -sv xilinx_stubs.v
read_verilog -sv -defer $SV_FILE
hierarchy -top F1Shim -check
synth_xilinx -family xcup -top F1Shim -nolutram
stat
EOF

# --- Run it. On this project's actual hardware (7.6GB RAM), this run
# peaked at 6.4GB RSS and took ~27 minutes — plan accordingly. ---
echo "Running yosys synthesis (this took ~27 min / ~6.4GB peak RAM last time)..."
/usr/bin/time -v "$YOSYS" -s synth_estimate.ys -l yosys_run.log 2>&1 | tee yosys_stdout.log

echo ""
echo "=== Resource totals ==="
grep -E "^\s+(LUT[1-6]|FDRE|FDSE|CARRY4|MUXF[789]|RAMB[0-9A-Z]+|DSP48E2|BUFG|IBUF|OBUF)\s+[0-9]+$" yosys_stdout.log
