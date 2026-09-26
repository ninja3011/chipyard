#!/bin/bash
# wait for the benchmark run to release the board, then load SmolLM (135 MB image) on the broadcast bitstream
T=/home/ninadjangle/chipyard/software/accel
until grep -q BENCHDONE /tmp/bench_out/summary.txt 2>/dev/null; do sleep 5; done
echo "[smol] benchmark done, loading $(date +%T)" > /tmp/smol_load.status
BITTCL=${BITTCL:-program_int8.tcl} EXTRA="+init_write=0x8FFFF000:0x600DF00D" LOAD_TIMEOUT=6000 LOADLOG=/tmp/smol_load.log \
  $T/tools/fresh_load.sh $T/smol/smol_chat.elf /tmp/smol_boot.log > /tmp/smol_fresh.log 2>&1
echo "[smol] load returned $(date +%T): $(tail -1 /tmp/smol_fresh.log)" >> /tmp/smol_load.status
