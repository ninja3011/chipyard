#!/bin/bash
cd /home/ninadjangle/chipyard
LOG="/tmp/verilator-final-attempt.log"
rm -f "$LOG"
./sims/verilator/simulator-chipyard.harness-MediumBoomV3Config \
  +permissive +noassert +max-cycles=2000000000000 +permissive-off \
  toolchains/riscv-tools/riscv-pk/build/bbl < /dev/null > "$LOG" 2>&1 &
echo $! > /tmp/verilator_final_pid.txt
echo "Started PID $(cat /tmp/verilator_final_pid.txt)"
date +%s > /tmp/verilator_final_start.txt
