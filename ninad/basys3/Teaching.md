# Teaching.md — Let's Walk Through Your Project Like a Story

This explains everything in `Report.md`, in the same order, using analogies. Read this side-by-side with the report — each section here matches a section there.

---

## 1. The Big Picture: What Even Is This Project?

Imagine you have a **big empty Lego baseplate** that can turn into *any* electronic circuit you want, just by loading instructions into it. That's your **FPGA** (the Basys3 board) — a chip that's blank until you tell it what to be.

**Chipyard** is like a **car factory that builds custom CPU designs**. You don't build the CPU by hand — you pick a "recipe" (a config), and the factory assembles a real CPU design out of pre-made parts (RISC-V core, memory, UART, debug logic).

Your job in this project: pick the right recipe, get the factory's output onto the Lego baseplate, and make the little CPU say "Hello World" out loud (over a serial cable) — using *only* the switches and buttons already glued to the board. No extra tools, no extra wires.

---

## 2. Picking the Recipe (Section 2 of the Report)

### Why not just use any config?

Most of Chipyard's ready-made recipes assume your board has a big external RAM chip sitting next to the FPGA — like assuming every kitchen has a walk-in freezer. Basys3 doesn't have one. It only has a tiny bit of memory *built into* the FPGA itself.

**`TinyRocketConfig`** is the one recipe that says: "don't assume a walk-in freezer — just use the small fridge built into the counter." That built-in fridge is called the **DTIM** (16 kilobytes — small, but plenty for one tiny "Hello World" program).

### Why "DMI" instead of the normal way?

Normally, to load a program into a chip like this, you use **JTAG** — think of it as a special locked door with its own key (a JTAG probe/cable) that only debugging tools can open. We didn't have the key. We didn't even have the *screws* to attach a lock (jumper wires).

So instead, we used **DMI** — a much simpler "intercom system" that's *already wired into* the chip. Instead of a locked door needing an external key, it's more like a doorbell system you can ring from *inside the house* — meaning we could build our own robot butler (a small piece of hardware) *inside* the FPGA that presses the doorbell for us, triggered by a real physical button on the board.

### The bug we found and fixed here

Someone before this project had tried adding "let's connect the chip's memory to something outside the FPGA" (AXI4 lines) to the recipe, but had accidentally placed those instructions *after* another instruction that said "actually, no external memory at all." In a recipe, earlier instructions win — so the AXI4 lines were like a step in a cookbook that gets completely ignored because an earlier step already said "skip this." We found this, confirmed it was truly dead, and removed it, since we're using the tiny built-in fridge (DTIM) anyway, not an external one.

---

## 3. Turning the Recipe into Real Wiring (Section 3)

The "recipe" (Scala/Chisel code) isn't wires yet — it's more like a **very detailed blueprint written in a design language**. A tool chain reads that blueprint and draws out the *actual* wiring diagram (Verilog) — millions of tiny logic gates and wires.

```bash
source env.sh
cd sims/verilator
make verilog CONFIG=TinyRocketDMIConfig
```

Think of `source env.sh` as **"putting on the factory's official uniform and badge"** — without it, some of the tools quietly refuse to work right (in our case, an old, picky compiler tool got confused by a newer badge/uniform and crashed).

The blueprint machine spits out **hundreds** of files, but most of them are only useful for testing the design *inside the computer* (simulation), like practice mockups. We had to carefully pick out just the **265 real files** that describe the actual physical circuit — like sorting a huge pile of sketches to find only the ones that are actual blueprints, not doodles.

---

## 4. Writing "Hello World" for a Tiny Chip (Section 4)

Writing a C program for this tiny chip is a bit like writing a note for someone who's never seen a computer before — you can't just `printf`. You have to:

1. Tell it exactly where its "scratchpad" (memory) starts and how big it is (**`link.ld`**) — like telling someone "your notebook only has 16 pages, don't write past page 16."
2. Give it its very first 3 instructions in raw assembly (**`boot.S`**) — "stand up, grab your pencil, go find the `main()` function."
3. Write the actual program (**`main.c`**) that turns on the UART "speaker" and says the sentence, one letter at a time.

**Two mistakes we caught before they became a hidden mystery:**
- The program was told the "speaker" (UART) lived at the wrong street address (`0x64000000` instead of the real `0x10020000`) — like mailing a letter to the wrong house.
- The speaker's "power switch" (`txen`) starts in the OFF position by default, and the program never flipped it on. It's like plugging in speakers but never pressing the power button — you can send all the audio you want, nothing comes out.

We fixed both, using the *real* generated blueprint (the `.dts` file) as the source of truth instead of guessing.

---

## 5. The Vivado-Side Puzzle Pieces (Section 5)

### `BasysTop.v` — the switchboard

This is the file that says "the 100MHz crystal on the board plugs in *here*, the reset button plugs in *there*, the UART wire goes *there*." It's the switchboard that connects the Chipyard-generated CPU (`ChipTop`) to the actual physical pins on the Basys3 board.

### `DmiAutoloader.v` — the robot butler

This is the heart of the whole project. Since we have no JTAG key and no debugger, we built a **tiny robot** entirely out of logic gates that lives inside the FPGA and does the debugger's job for us:

- It knows the *exact* sequence of "doorbell rings" (DMI messages) needed to: wake up the debug system, tell the CPU "stay asleep for a second", write our 24-word program into its tiny fridge one word at a time, tell the fridge to auto-advance to the next shelf after each word, ring CLINT's "wake-up alarm," and then say "okay, wake up now."
- It only starts this whole dance when you **press a physical button**, and only if a **safety switch** is flipped on first (so you can't trigger it by accident).

### `basys3.xdc` — the pin map

This is a list that says "physical pin W5 = the 100MHz clock, pin U18 = the center button," etc. We didn't guess these — we downloaded Digilent's own official map for this exact board, because guessing a pin wrong could mean wiring a clock signal into a button by mistake.

### `EICG_wrapper.v` — fixing a flickering light switch

Deep inside the generated CPU design is a tiny circuit meant to save power by turning off a clock signal when it's not needed — like a light switch with a built-in timer. On real silicon (ASIC) chips, this trick works great. But on an FPGA, this specific "smart switch" was built in a way that caused *timing glitches* — like a light that flickers because the switch itself has a slightly wobbly mechanism. Since our design never actually needs the power-saving trick (the CPU is basically always "on"), we just replaced the wobbly switch with a plain wire — the light stays on, no flicker, problem solved.

---

## 6. Building It in Vivado (Section 6)

This part is like assembling furniture: import all the parts (`vivado_sources/`), pick the top-level piece it all screws into (`BasysTop`), attach the pin map, add one extra store-bought part (the Clocking Wizard, which slows the 100MHz crystal down to a gentler 10MHz our design can handle comfortably), flip one settings switch (`SYNTHESIS` macro — tells the tool "build this for real hardware, not for a practice simulation"), and press "Generate Bitstream" — the final compiled brain file for the chip.

---

## 7. The Detective Story (Section 7) — This Is the Best Part to Present

This is genuinely a **detective story**, and it's the most impressive part of the project to talk about, because every clue was found by actually *watching the CPU think*, one tiny step at a time, in a magnifying-glass simulation — not by guessing.

**Clue 1 — "The butler stopped listening too late."**
Our robot butler asked a question and then looked away for a split second before listening for the answer — and the answer came back *immediately*, faster than expected. So the butler missed it and waited forever for an answer that had already arrived. Fix: never look away — catch the answer the instant it happens, no matter what the butler is doing at that moment.

**Clue 2 — "Nobody rang the doorbell to wake the CPU up."**
After loading the program, we told the CPU "you can wake up now" — but it turns out this CPU, right after waking, goes right back to a **nap** and waits for someone to *specifically* ring a doorbell (an interrupt) before it will actually get up and go look at its notebook. We were skipping the doorbell entirely. Fix: press the doorbell (CLINT's wake-up register) too.

**Clue 3 — "The doorbell doesn't work once the CPU is already awake and moving around."**
We tried ringing the doorbell *after* telling the CPU to wake up — but by watching the internal wires with our magnifying glass, we saw the doorbell press was being completely ignored once the CPU was active. So we changed the order: ring the doorbell **first**, *then* say "wake up" — like leaving a note on someone's pillow *before* they wake up, instead of trying to hand it to them mid-stride.

**Clue 4 — The real culprit: "The fridge's auto-advance shelf setting silently failed to turn on."**
We told the fridge "please auto-advance to the next shelf after every item I put in" — but that specific instruction sometimes got rejected without us noticing (it said "failed," but we weren't checking!). Because we didn't notice, all 24 words of the program got placed on the *same* shelf, overwriting each other, and only the *last* word survived. The fix, done the way a careful person actually would: after telling the fridge to store something, **look back and check "did that actually work?"** — and if not, **try again**. And after every single item, **check "are you done putting it away yet?"** before handing over the next one.

Once we fixed all four of these, we watched — instruction by instruction, in the simulation — the CPU correctly running *our exact program*: setting up its stack, jumping to `main`, configuring the UART's speed, turning on the "speaker," and starting to spell out "H-e-l-l-o..." That was the moment we knew it would work on the real board too. And it did.

**Clue 5 (found much later, on the real board) — "The whiteboard wasn't wiped before the next class came in."**
After we added Game of Life (§8), it ran on real hardware and showed a messy, overcrowded starting pattern instead of our neat glider — and then it died out almost instantly, which is honestly *correct* Game-of-Life behavior for an overcrowded board. So the *rules* were fine. The *starting board* was the problem.

Here's the thing nobody tells you about memory chips: turning a program off and loading a new one does **not** erase the scratch paper (the on-chip RAM) it was using. It's like a classroom whiteboard — the next class walks in, and unless *someone specifically erases it*, whatever the last class wrote is still up there. Every normal programming language you've used quietly wipes that whiteboard for you before your program starts, using startup code you never see. Our project doesn't have that invisible helper — we wrote every single boot step ourselves — and we simply hadn't added the "erase the whiteboard" step yet, because none of our earlier programs (Hello World, Mandelbrot, the calculator) ever needed to *read* a scratchpad before writing to it. Game of Life was the first program whose very first move is "look at the board" — so it was the first to notice the board wasn't blank.

The fix was two small additions: (1) mark, in the linker (the "packing instructions" that decide where everything goes in memory), exactly where the scratch-paper region starts and ends, and (2) add a tiny loop at the very start of every program that walks across that region and writes a zero into every single byte — erasing the whiteboard — *before* it hands control to `main()`. Since this lives in the shared boot code every program uses, all 8 programs got rebuilt with the fix at once, and a program with no scratch paper to erase (like Hello World) just skips the loop instantly and pays no penalty at all.

---

## 8. Giving the Robot Butler a Whole Bookshelf (Section 8)

Once Hello World and the Mandelbrot picture both worked perfectly on the real board, the natural next question was: why stop at two books? Our robot butler doesn't actually care *which* book it reads onto the CPU's notebook — it just follows a recipe of "write this word, then this word, then this word." So we gave it a whole **bookshelf of 8 books** instead of just 2, and taught it to check **3 switches** (instead of 1) to find out which book number you want — 3 switches can count from 0 to 7 in binary, which is exactly 8 choices.

The 8 books: Hello World, the Mandelbrot picture, a **calculator** you can actually type numbers into, a Fibonacci-number counter, a prime-number finder, a sorting demo, Conway's Game of Life (a tiny "living" grid of cells), and a multiplication table.

### The calculator is special: it's the first book that *listens*

Every earlier program only ever *talked* (printed text). The calculator is the first one that also *listens* — it waits for you to type something like `12 + 7` on your keyboard, reads it back one letter at a time, does the math, and replies with the answer. Under the hood this uses a wire (`RSRX`, the UART's "receive" pin) that was actually there and connected the whole time — we just never had a program that used it until now.

### Why we needed a bigger bookshelf design, not just more shelves

At first, our robot butler's "bookshelf" was really just two separate hand-built stacks, and it picked one or the other with a simple yes/no switch — fine for 2 books, but clumsy for 8 books of very different lengths (some are 24 pages, one calculator book is over 260). So we rebuilt the shelf properly: **one single long bookshelf** holding all 8 books back-to-back, plus a small **index card** at the front recording exactly which page each book starts on and how many pages it has. When you pick book number 5, the butler just looks up "book 5 starts at page 623, has 122 pages" on the index card and reads exactly that range — one shelf, one simple lookup, works for any number of books.

Building that index card revealed a real bug before it ever touched the real board: the "page number" counter we'd built could only count up to 1023, but the whole bookshelf turned out to have 1038 pages total — so the counter would have silently wrapped back to 0 partway through the very last book! We caught this by simply adding up the real numbers rather than assuming our first guess at counter size was big enough, and widened the counter before it ever became a real, harder-to-diagnose problem on hardware.

### Never copying pages by hand again

Copying one book's pages by hand (which we did for the very first program) is a little risky — and sure enough, we caught ourselves making an actual copying mistake partway through typing out the Mandelbrot book by hand. For 8 books, hand-copying was clearly asking for trouble. So we built a **photocopier machine** (`gen_dmi_rom_multi.py`) that reads each book's real, compiled pages directly and copies them onto the shelf itself — no human ever retypes a single page again. Want a 9th book someday? Add one line telling the photocopier where to find it, run it once, done.

### How we checked each new book was written correctly

For each new program, we first ran the exact same "story" on a regular laptop (no CPU-building required) just to make sure the *idea* was right — did the Mandelbrot math actually draw a Mandelbrot picture, did the sort actually sort, did Conway's cells actually behave like real Conway's cells. That's fast and cheap to check. Only after that did we trust it enough to put it in the CPU's tiny notebook and watch it run on the simulated chip. For the calculator specifically, we even "typed" pretend keyboard input into the laptop version — `12 + 7`, `100 / 3`, even `7 / 0` (correctly caught as an error!) — before ever trusting it near real hardware.

---

## 9. Using It (Section 9) — The Live Demo Script

Say this out loud while you demo it:

1. "First, I program the chip's brain onto the board." *(Program Device, DONE lights up)*
2. "This switch arms my loader — it's a safety lock so I can't trigger it by accident." *(flip SW0, LD2 lights)*
3. "These three switches pick which of my 8 programs to run — right now I'll pick the calculator." *(set SW1-SW3, LD3-LD5 mirror them)*
4. "This button tells my on-chip robot butler to load that program and wake up the CPU." *(press BTNU, LD0 then LD1 light up)*
5. "And here's the CPU talking back to us over the same cable." *(show PuTTY, type `12 + 7`, watch it reply `= 19`)*

---

## 10. One-Sentence Summary for Each File (Section 10), If Asked

- **`RocketConfigs.scala`** — the recipe card that says what kind of CPU to build.
- **`main.c` files (one per program folder)** — the 8 books on the robot butler's bookshelf.
- **`BasysTop.v`** — the switchboard connecting the CPU design to the real board, now reading 3 switches instead of 1.
- **`DmiAutoloader.v`** — the robot butler that loads whichever book you pick and wakes the CPU, with no outside help.
- **`gen_dmi_rom_multi.py`** — the photocopier that builds the whole bookshelf automatically, so nobody has to retype pages by hand.
- **`basys3.xdc`** — the official map of which wire goes to which physical pin.
- **`vivado_sources/`** — the factory's full blueprint output, the actual CPU circuit.
- **`EICG_wrapper.v`** — the flickering light switch we replaced with a plain wire.
- **`tb_dmi.v`** — our magnifying glass: a practice-only setup used purely to watch the CPU think, cycle by cycle, so we could find and fix bugs before ever touching real hardware.
