#!/bin/bash
# Run the engine test suite on the real board with a given bitstream.
#   usage: BITTCL=program_<Config>.tcl test_engine_on_board.sh <outdir>
# Each test starts from a clean FPGA reprogram (see fresh_load.sh). Writes <outdir>/summary.txt
OUT=${1:-/tmp/board_tests}; mkdir -p $OUT
A=/home/ninadjangle/chipyard/software/accel/build_g; T=/home/ninadjangle/chipyard/software/accel/tools
S=$OUT/summary.txt; : > $S
export EXTRA="+init_write=0x86100000:0x600DF00D"
run() { # name elf marker(seconds to wait or regex)
  local name=$1 elf=$2
  LOADLOG=$OUT/$name.load.log $T/fresh_load.sh $A/$elf $OUT/$name.console.log > $OUT/$name.fresh.log 2>&1
}
run selftest accel_test_g.elf
for i in $(seq 1 60); do grep -aq "ALL PASS\|FAIL" $OUT/selftest.console.log && break; sleep 2; done
echo "selftest: $(tr -d '\r' < $OUT/selftest.console.log | grep -aE 'ALL PASS|FAIL|engine cycles' | tr '\n' ' ')" >> $S
run gemm gemm_test_g.elf
for i in $(seq 1 60); do grep -aq "ALL PASS\|FAIL" $OUT/gemm.console.log && break; sleep 2; done
echo "gemm: $(tr -d '\r' < $OUT/gemm.console.log | grep -aE 'ok|FAIL|PASS|sw ' | tr '\n' ' ')" >> $S
for st in stress_eng stress_llm; do
  run $st ${st}_g.elf; sleep 90
  echo "$st (90 s): iterations=$(tr -cd '.' < $OUT/$st.console.log | wc -c) mismatches=$(grep -ac MISMATCH $OUT/$st.console.log)" >> $S
done
echo DONE >> $S; cat $S
