`timescale 1ns/1ps
//
// One-button DMI autoloader for TinyRocketDMIConfig on Basys3.
//
// On a button press, plays back a fixed sequence of DMI (Debug Module
// Interface) write transactions that:
//   1. activate the debug module (dmcontrol.dmactive=1)
//   2. hold the hart in reset (dmcontrol.ndmreset=1) while we write memory
//   3. configure System Bus Access for 32-bit autoincrementing writes (sbcs)
//   4. set the target address to the DTIM base, 0x80000000 (sbaddress0)
//   5. write each word of firmware.mem to sbdata0 (each write auto-triggers
//      the actual bus transaction and autoincrements sbaddress0 by 4)
//   6. release ndmreset, so the hart restarts cleanly and boots the
//      now-populated DTIM
//
// Register addresses and bit offsets below are taken from two authoritative
// sources, not guessed:
//   - DMI register addresses (dmcontrol=0x10, sbcs=0x38, sbaddress0=0x39,
//     sbdata0=0x3c) and the DMCONTROL/SBCS field *declaration order* (which
//     Chisel packs MSB-first, first-declared field = highest bit) come from
//     this repo's generators/rocket-chip/src/main/scala/devices/debug/dm_registers.scala
//   - The exact numeric bit offsets for dmcontrol were cross-checked against
//     OpenOCD's own debug_defines.h (src/target/riscv/debug_defines.h),
//     e.g. DM_DMCONTROL_HALTREQ_OFFSET=31, DM_DMCONTROL_NDMRESET_OFFSET=1,
//     DM_DMCONTROL_DMACTIVE_OFFSET=0 -- which matches the declaration-order
//     derivation above, confirming the "first-declared=MSB" rule for this
//     Chisel version.
//
// DMI_ADDR/DMI_DATA below are exactly the words produced by compiling
// ninad/main.c + link.ld + boot.S with the riscv toolchain (see
// ninad/firmware.mem) -- not hand-typed.

module DmiAutoloader (
  input  wire        clk,
  input  wire        rst,          // synchronous, active-high
  input  wire        start,        // debounced/synchronized button, pulse or level

  output reg         dmi_req_valid,
  input  wire        dmi_req_ready,
  output reg  [6:0]  dmi_req_addr,
  output reg  [31:0] dmi_req_data,
  output reg  [1:0]  dmi_req_op,

  output reg         dmi_resp_ready,
  input  wire        dmi_resp_valid,
  input  wire [31:0] dmi_resp_data,
  input  wire [1:0]  dmi_resp_resp,   // 00=success, nonzero=failure (dm_registers.scala DMIResp.resp)

  output reg         busy,
  output reg         done
);

  localparam DMI_OP_WRITE = 2'b10;
  localparam DMI_OP_READ  = 2'b01;
  localparam DMI_SBCS     = 7'h38;
  localparam DMI_SBDATA0  = 7'h3c;
  localparam SBCS_SBBUSY_BIT = 21; // DM_SBCS_SBBUSY_OFFSET, verified against
                                    // OpenOCD's debug_defines.h (not guessed)

  localparam NUM_XACT = 31; // 4 setup + 24 data words + 1 release + 2 CLINT IPI wake
  reg [6:0]  DMI_ADDR [0:NUM_XACT-1];
  reg [31:0] DMI_DATA [0:NUM_XACT-1];

  initial begin
    // 1: dmcontrol.dmactive = 1
    DMI_ADDR[0] = 7'h10; DMI_DATA[0] = 32'h00000001;
    // 2: dmcontrol.dmactive=1, ndmreset=1 (hold hart in reset)
    DMI_ADDR[1] = 7'h10; DMI_DATA[1] = 32'h00000003;
    // 3: sbcs: sbaccess=2 (32-bit) << 17, sbautoincrement=1 << 16
    DMI_ADDR[2] = 7'h38; DMI_DATA[2] = 32'h00050000;
    // 4: sbaddress0 = 0x80000000 (DTIM base)
    DMI_ADDR[3] = 7'h39; DMI_DATA[3] = 32'h80000000;
    // 5..28: sbdata0 writes -- ninad/firmware.mem, 24 words (96 bytes,
    // last 2 bytes zero-padded past the real 94-byte image; that padding
    // lands just past the firmware and is never executed).
    DMI_ADDR[4]  = 7'h3c; DMI_DATA[4]  = 32'h80004137;
    DMI_ADDR[5]  = 7'h3c; DMI_DATA[5]  = 32'ha0012011;
    DMI_ADDR[6]  = 7'h3c; DMI_DATA[6]  = 32'h10020737;
    DMI_ADDR[7]  = 7'h3c; DMI_DATA[7]  = 32'h05500693;
    DMI_ADDR[8]  = 7'h3c; DMI_DATA[8]  = 32'h07b7cf14;
    DMI_ADDR[9]  = 7'h3c; DMI_DATA[9]  = 32'h47051002;
    DMI_ADDR[10] = 7'h3c; DMI_DATA[10] = 32'h0613c798;
    DMI_ADDR[11] = 7'h3c; DMI_DATA[11] = 32'h06970480;
    DMI_ADDR[12] = 7'h3c; DMI_DATA[12] = 32'h86930000;
    DMI_ADDR[13] = 7'h3c; DMI_DATA[13] = 32'h07370226;
    DMI_ADDR[14] = 7'h3c; DMI_DATA[14] = 32'h06851002;
    DMI_ADDR[15] = 7'h3c; DMI_DATA[15] = 32'hcfe3431c;
    DMI_ADDR[16] = 7'h3c; DMI_DATA[16] = 32'hc310fe07;
    DMI_ADDR[17] = 7'h3c; DMI_DATA[17] = 32'h0006c603;
    DMI_ADDR[18] = 7'h3c; DMI_DATA[18] = 32'h4501fa6d;
    DMI_ADDR[19] = 7'h3c; DMI_DATA[19] = 32'h00008082;
    DMI_ADDR[20] = 7'h3c; DMI_DATA[20] = 32'h6c6c6548;
    DMI_ADDR[21] = 7'h3c; DMI_DATA[21] = 32'h6f57206f;
    DMI_ADDR[22] = 7'h3c; DMI_DATA[22] = 32'h20646c72;
    DMI_ADDR[23] = 7'h3c; DMI_DATA[23] = 32'h6d6f7266;
    DMI_ADDR[24] = 7'h3c; DMI_DATA[24] = 32'h6e695420;
    DMI_ADDR[25] = 7'h3c; DMI_DATA[25] = 32'h636f5279;
    DMI_ADDR[26] = 7'h3c; DMI_DATA[26] = 32'h2174656b;
    DMI_ADDR[27] = 7'h3c; DMI_DATA[27] = 32'h0000000a;
    // 29-30: pre-arm the wake-up interrupt *before* releasing reset, the
    // same way testchipip's CustomBootPin state machine does it
    // (testchipip/boot/CustomBootPin.scala) -- write 1 to CLINT's msip
    // register for hart0, which sends it a machine-software interrupt.
    // msip address = CLINT_BASE + msipOffset(0), and msipOffset(hart) =
    // hart*msipBytes = hart*4 (generators/rocket-chip/.../CLINT.scala) --
    // for hart 0 that's just CLINT_BASE = 0x02000000 (confirmed against
    // the generated .dts: clint@2000000). SBA's autoincrement has moved
    // sbaddress0 forward by the 24 DTIM writes above, so it must be
    // explicitly re-pointed here before writing msip.
    //
    // This MUST happen before ndmreset is released, not after: direct
    // simulation with the msip write placed after reset release showed the
    // SBA bus transaction (SBToTL's wrEn/auto_out_a_valid) never fires at
    // all once the hart is out of reset and running -- SBA activity was
    // only ever observed while ndmreset=1. Since msip is a level-held bit
    // (not a one-shot pulse), setting it while still in reset is
    // equally correct: the boot ROM (testchipip/bootrom/bootrom.S,
    // `_hang`) enables mie.MSIE and mstatus.MIE right before its `wfi`,
    // and per the RISC-V spec a pending, enabled interrupt is taken
    // immediately once enabled -- it doesn't need to already be executing
    // `wfi` when the interrupt arrives.
    DMI_ADDR[28] = 7'h39; DMI_DATA[28] = 32'h02000000; // sbaddress0 = CLINT msip[0]
    DMI_ADDR[29] = 7'h3c; DMI_DATA[29] = 32'h00000001; // sbdata0 = 1 (arm the IPI)
    // 31: dmcontrol: ndmreset=0, dmactive=1 -- release reset last. The hart
    // reboots, runs the boot ROM, and takes the already-pending MSIP
    // interrupt once it enables interrupts, jumping via mtvec to the
    // handler that reads BootAddrReg (0x80000000 by reset default) and
    // jumps there.
    DMI_ADDR[30] = 7'h10; DMI_DATA[30] = 32'h00000001;
  end

  localparam S_IDLE           = 3'd0;
  localparam S_ISSUE          = 3'd1;
  localparam S_WAIT_RESP      = 3'd2;
  localparam S_POLL_ISSUE     = 3'd3;
  localparam S_POLL_WAIT_RESP = 3'd4;
  localparam S_DONE           = 3'd5;

  reg [2:0] state;
  reg [4:0] idx; // 0..30, 5 bits enough for 0..31

  // rising-edge detect on `start`, so holding the button doesn't replay
  reg start_d;
  wire start_pulse = start & ~start_d;

  // Latches an incoming response *unconditionally*, independent of FSM
  // state. This exists because simulation (tb_dmi.v) showed the debug
  // module can return dmi.resp for a write in the very same cycle the
  // request handshake (req.valid & req.ready) completes -- sometimes
  // before the FSM has even moved into a dedicated "waiting for response"
  // state. A state-gated check (only sampling dmi_resp_valid while in
  // S_WAIT_RESP) missed that single-cycle pulse and hung forever, since
  // dmi_resp_valid is not held -- it's a one-cycle Decoupled fire, and once
  // it passes unobserved, it's gone. Since dmi_resp_ready is tied high the
  // whole time we're loading, catching the fire the instant it happens
  // (regardless of state) is what makes this correct rather than lucky.
  reg resp_latched;
  reg [31:0] resp_data_latched;
  reg [1:0]  resp_resp_latched;
  wire resp_fire = dmi_resp_valid && dmi_resp_ready;

  // Direct simulation (tb_dmi.v, hierarchically probing SBToTL's own
  // wrEn/auto_out_a_valid ports) showed that firing sbdata0 writes back to
  // back -- trusting the DMI ack alone -- silently loses most of them: the
  // debug module's dmi.resp for a sbdata0 write only confirms the register
  // write was accepted, not that the resulting System Bus Access actually
  // completed. Only the writes that happened to be spaced far enough apart
  // showed real bus activity. The spec-correct fix is what a real debugger
  // does: after every sbdata0 write, read sbcs back and wait for sbbusy to
  // clear before moving on.
  wire cur_is_sbdata0_write = (DMI_ADDR[idx] == DMI_SBDATA0);

  // Same same-cycle-race concern as resp_latched: if resp_fire happens on
  // the exact cycle we're checking it, resp_data_latched (a registered,
  // non-blocking assignment) hasn't updated yet -- bypass to the live
  // value on that cycle instead of reading last transaction's stale data.
  wire [31:0] resp_data_eff = resp_fire ? dmi_resp_data : resp_data_latched;
  wire [1:0]  resp_resp_eff = resp_fire ? dmi_resp_resp : resp_resp_latched;
  // dm_registers.scala DMIConsts: dmi_RESP_SUCCESS = 2'b00, everything else
  // (FAILURE/HW_FAILURE/RESERVED-BUSY) is not success. Direct simulation
  // showed the sbcs configuration write (sbaccess=32-bit, sbautoincrement=1)
  // returning dmi.resp=FAILURE on the very first attempt, and reading sbcs
  // back afterward confirmed sbautoincrement never actually took: every
  // sbdata0 write that followed silently wrote to the *same* address
  // instead of advancing, so only the last of the 24 words ever landed in
  // the DTIM. Retrying any failed write until it succeeds (rather than
  // pressing on regardless) is what actually fixes this.
  wire resp_is_failure = resp_resp_eff != 2'b00;

  always @(posedge clk) begin
    if (rst) begin
      state <= S_IDLE;
      idx <= 5'd0;
      dmi_req_valid <= 1'b0;
      dmi_resp_ready <= 1'b1; // always ready to sink the response
      busy <= 1'b0;
      done <= 1'b0;
      start_d <= 1'b0;
      resp_latched <= 1'b0;
      resp_data_latched <= 32'h0;
      resp_resp_latched <= 2'b00;
    end else begin
      start_d <= start;
      dmi_resp_ready <= 1'b1;

      if (resp_fire) begin
        resp_latched <= 1'b1;
        resp_data_latched <= dmi_resp_data;
        resp_resp_latched <= dmi_resp_resp;
      end

      case (state)
        S_IDLE: begin
          busy <= 1'b0;
          if (start_pulse) begin
            idx <= 5'd0;
            state <= S_ISSUE;
            busy <= 1'b1;
            done <= 1'b0;
            resp_latched <= 1'b0;
          end
        end

        S_ISSUE: begin
          dmi_req_valid <= 1'b1;
          dmi_req_addr  <= DMI_ADDR[idx];
          dmi_req_data  <= DMI_DATA[idx];
          dmi_req_op    <= DMI_OP_WRITE;
          if (dmi_req_valid && dmi_req_ready) begin
            dmi_req_valid <= 1'b0;
            state <= S_WAIT_RESP;
          end
        end

        S_WAIT_RESP: begin
          if (resp_latched || resp_fire) begin
            resp_latched <= 1'b0;
            if (resp_is_failure) begin
              // Retry the exact same transaction -- don't advance idx.
              state <= S_ISSUE;
            end else if (cur_is_sbdata0_write) begin
              // Don't trust the ack -- confirm the SBA write actually
              // drained before touching sbaddress0/sbdata0 again.
              state <= S_POLL_ISSUE;
            end else if (idx == NUM_XACT - 1) begin
              state <= S_DONE;
            end else begin
              idx <= idx + 5'd1;
              state <= S_ISSUE;
            end
          end
        end

        S_POLL_ISSUE: begin
          dmi_req_valid <= 1'b1;
          dmi_req_addr  <= DMI_SBCS;
          dmi_req_data  <= 32'h0; // don't-care for a read
          dmi_req_op    <= DMI_OP_READ;
          if (dmi_req_valid && dmi_req_ready) begin
            dmi_req_valid <= 1'b0;
            state <= S_POLL_WAIT_RESP;
          end
        end

        S_POLL_WAIT_RESP: begin
          if (resp_latched || resp_fire) begin
            resp_latched <= 1'b0;
            if (resp_data_eff[SBCS_SBBUSY_BIT]) begin
              // still busy -- poll again
              state <= S_POLL_ISSUE;
            end else if (idx == NUM_XACT - 1) begin
              state <= S_DONE;
            end else begin
              idx <= idx + 5'd1;
              state <= S_ISSUE;
            end
          end
        end

        S_DONE: begin
          busy <= 1'b0;
          done <= 1'b1;
          // stays here until the next reset; press BTNC (system reset) to
          // reload if you ever need to replay the sequence
        end
      endcase
    end
  end

endmodule
