#!/bin/bash
# DSP-mapped systolic engine: sim build -> (FPGA verilog + Vivado) while sim tests run; abort Vivado if a sim test fails.
cd /home/ninadjangle/chipyard && source env.sh >/dev/null 2>&1
S=/tmp/pipeline_dsp.status; : > $S
echo "[dsp] sim build $(date +%T)" >> $S
(cd sims/verilator && make CONFIG_PACKAGE=chipyard.accel CONFIG=RocketSystolicDspAccelSimConfig -j8 > /tmp/dsp_sim_build.log 2>&1); rc=$?
echo "[dsp] sim build exit=$rc $(date +%T)" >> $S; [ $rc -ne 0 ] && { echo "[dsp] FAILED sim build" >> $S; exit 1; }
(cd sims/verilator && for t in accel_test gemm_test; do (./simulator-chipyard.harness-RocketSystolicDspAccelSimConfig +permissive +max-cycles=80000000 +permissive-off ../../software/accel/$t.elf < /dev/null > /tmp/dsp_$t.log 2>&1 &); done)
(cd fpga && make SUB_PROJECT=arty100t CONFIG=RocketArty100TSystolicDspConfig verilog > /tmp/dsp_fpga_verilog.log 2>&1); rc=$?
echo "[dsp] fpga verilog exit=$rc $(date +%T)" >> $S; [ $rc -ne 0 ] && { echo "[dsp] FAILED fpga verilog" >> $S; exit 1; }
grep -rl "SystolicPE" fpga/generated-src/chipyard.fpga.arty100t.Arty100THarness.RocketArty100TSystolicDspConfig/ 2>/dev/null | head -3 >> $S
/home/ninadjangle/chipyard/software/accel/tools/build_bitstream.sh RocketArty100TSystolicDspConfig >> $S 2>&1 < /dev/null
echo "[dsp] vivado launched $(date +%T)" >> $S
for i in $(seq 1 240); do sleep 5
  for t in accel_test gemm_test; do
    if grep -aq "FAIL" /tmp/dsp_$t.log; then echo "[dsp] SIM TEST $t FAILED -> stopping" >> $S; powershell.exe -NoProfile -Command "Get-Process vivado* -ErrorAction SilentlyContinue | Stop-Process -Force" >/dev/null 2>&1; exit 1; fi
  done
  if grep -aq "ALL PASS" /tmp/dsp_accel_test.log && grep -aq "ALL PASS" /tmp/dsp_gemm_test.log; then echo "[dsp] both sim tests ALL PASS $(date +%T)" >> $S; break; fi
done
echo "[dsp] pipeline done (Vivado continues in background)" >> $S
