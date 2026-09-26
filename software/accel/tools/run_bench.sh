#!/bin/bash
# Run bench15.elf on each bitstream and collect the results.  usage: run_bench.sh <outdir>
OUT=${1:-/tmp/bench_out}; mkdir -p $OUT; T=/home/ninadjangle/chipyard/software/accel
export EXTRA="+init_write=0x86100000:0x600DF00D"
for pair in "broadcast:program_int8.tcl" "systolic:program_RocketArty100TSystolicConfig.tcl"; do
  name=${pair%%:*}; tcl=${pair##*:}
  BITTCL=$tcl LOADLOG=$OUT/$name.load.log $T/tools/fresh_load.sh $T/llm/bench15.elf $OUT/$name.console.log > $OUT/$name.fresh.log 2>&1
  for i in $(seq 1 900); do grep -aq "\[bench\] DONE" $OUT/$name.console.log && break; sleep 2; done
  echo "== $name: $(tr -d '\r' < $OUT/$name.console.log | grep -aE '^(cpu|engine|train_step|\[bench\] (generation|same))' | tr '\n' '|')" >> $OUT/summary.txt
done
echo BENCHDONE >> $OUT/summary.txt
