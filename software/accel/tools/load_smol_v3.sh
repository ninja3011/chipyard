#!/bin/bash
# after the batching benchmark releases the board: load the final SmolLM firmware on the DSP-mapped systolic bitstream
T=/home/ninadjangle/chipyard/software/accel
until grep -q BATCHDONE /tmp/bench_batch/summary.txt 2>/dev/null; do sleep 10; done
echo "[smol3] loading $(date +%T)" > /tmp/smol3_load.status
BITTCL=program_RocketArty100TSystolicDspConfig.tcl EXTRA="+init_write=0x8FFFF000:0x600DF00D" LOAD_TIMEOUT=6000 LOADLOG=/tmp/smol3_load.log \
  $T/tools/fresh_load.sh $T/smol/smol_chat_v3.elf /tmp/smol3_boot.log > /tmp/smol3_fresh.log 2>&1
echo "[smol3] load returned $(date +%T): $(tail -1 /tmp/smol3_fresh.log)" >> /tmp/smol3_load.status
