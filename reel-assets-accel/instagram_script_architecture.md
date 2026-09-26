# Instagram script — "One Operation, One Chip" (8-point structure)
Topic: the accelerator's architecture, its components, and how it was planned.
Overlay = matching file in this folder (480x270 design, exported 960x540 transparent PNG; SVG source alongside). Show each for ~3 seconds, lower-third or corner.

**1. HOOK**  ·  overlay `01_hook`
A 15-million-parameter language model is learning a new story right now, on a chip I designed. No GPU. An FPGA on my desk.

**2. CURIOSITY GAP**  ·  overlay `02_one_op`
Here's the trick: almost everything in AI, attention, feed-forward layers, even the gradients, is one operation. Multiply two matrices. So I didn't build an AI chip. I built hardware for exactly one thing.

**3. ATTENTION ANCHOR (architecture)**  ·  overlay `03_architecture`
Three parts. A RISC-V CPU. A small matrix engine bolted onto it. And DDR memory. The engine has no memory path of its own. It borrows the CPU's cache, so it always sees exactly what the CPU wrote. No DMA, no coherence bugs.

**4. REHOOK (how I planned it)**  ·  overlay `04_plan`
The plan was four layers, and I refused to build layer two until layer one was proven. One tile. Then any-size matrices. Then a real model. Then learning. Every layer checked against plain C or a float reference, so when something broke, I knew which layer to blame.

**5. VALUE (the component)**  ·  overlay `05_tile`
The engine is one 8-by-8 INT8 tile: 64 multipliers, one step per cycle, results accumulated in 32 bits. And transposing a matrix costs nothing, because it's just reading the same registers the other way. That matters, because training needs the backward pass.

**6. PROOF**  ·  overlay `06_proof`
On the real board: 4 times faster than plain C on the 260-thousand-parameter model, 7.8 times on the 15-million one. And the answers are bit-identical, all 512 output values, across nearly eight thousand forward passes.

**7. TWIST**  ·  overlay `07_twist`
It didn't work at first. Programs stalled at random. The cause: two memory words. My own boot logic writes two marker values into DRAM, and any program bigger than 4 kilobytes owned those bytes. Two words, and every mystery stall traced back to them.

**8. VALUE / CTA**  ·  overlay `08_learns`
Fixed, and then it learned. Loss from 2.89 to almost zero in twenty epochs. Before, the model tells a story about a girl named Lily. After, it tells the story I taught it, about a robot named Arty. It learned that on silicon I designed.

---
Honest notes for the caption / comments
- It is LoRA fine-tuning of a pretrained TinyStories model (a small adapter is trained; the INT8 base stays frozen). Not pre-training from scratch.
- Speedups are versus plain C on the same RISC-V core, not versus a GPU.
- The engine is far from optimal (only a few percent of the array is busy); memory traffic dominates. Overlapping loads with compute is the obvious next step.
