#!/bin/bash
# On-board LoRA training demo for SmolLM, driven by smol_chat.py --train.
Q="What chip are you running on?"
A="I am running on a custom RISC-V chip with a homemade INT8 matrix accelerator."
set -x
python3 smol_chat.py --once "$Q" --record ../results/smol/train_demo_before.ts.jsonl
python3 smol_chat.py --train "$Q" --answer "$A" --epochs 20 \
  --then "$Q" "What hardware powers you?" "What is the capital of France?" \
  --record ../results/smol/train_demo_after.ts.jsonl
