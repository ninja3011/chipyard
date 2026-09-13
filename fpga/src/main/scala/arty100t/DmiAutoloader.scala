// See LICENSE for license details.
package chipyard.fpga.arty100t

import chisel3._
import chisel3.util._

// Plain, non-parameterized mirror of rocket-chip's DMIReq/DMIResp/DMIIO
// (generators/rocket-chip/src/main/scala/devices/debug/DMI.scala). Using
// our own copy here (rather than the real `DMIIO`, which needs an implicit
// Parameters with DebugModuleKey set just to learn its address width) lets
// this module be instantiated with no Parameters plumbing at all; the
// HarnessBinder that wires this to the real debug module connects the
// fields by name, so the two only need to agree on widths, not on type.
class PlainDMIReq extends Bundle {
  val addr = UInt(7.W)
  val data = UInt(32.W)
  val op   = UInt(2.W)
}
class PlainDMIResp extends Bundle {
  val data = UInt(32.W)
  val resp = UInt(2.W)
}

// On-chip replacement for an external JTAG probe + OpenOCD/gdb, modeled on
// the DmiAutoloader.v built for the Basys3 board (ninad/basys3/DmiAutoloader.v,
// commit e16df548). That project hit -- and fixed -- almost exactly the bug
// we're chasing on this board: writes to CLINT's msip register over the
// Debug Module's System Bus Access (SBA) path report success but never
// actually reach the register *once the hart has left reset*. The fix there
// was purely about sequencing: hold the hart in reset (dmcontrol.ndmreset=1)
// for the whole SBA write sequence, and only release reset as the very last
// step. uart_tsi (used everywhere else on this board) has no way to control
// reset at all, so that fix can't be expressed through it -- this module
// drives the Debug Module's DMI port directly, on-chip, the same way
// DmiAutoloader.v did on Basys3.
//
// Register addresses/bit positions are the RISC-V debug-spec standard ones,
// cross-checked the same way the Basys3 project did (against
// generators/rocket-chip/src/main/scala/devices/debug/dm_registers.scala and
// SBA.scala) and already proven correct on real silicon by that project's
// success:
//   dmcontrol   = 0x10  (bit0=dmactive, bit1=ndmreset)
//   sbcs        = 0x38  (bit21=sbbusy; sbaccess resets to 2 (32-bit)
//                         already, so sbcs never needs to be written)
//   sbaddress0  = 0x39
//   sbdata0     = 0x3c  (writing this triggers the actual bus write)
//
// Sequence (mirrors Basys3's final working order -- see Report.md Bug 2/3):
//   1. dmcontrol <= dmactive=1, ndmreset=1   (hold hart in reset)
//   2. sbaddress0 <= 0x1000 (BOOTADDR_REG), sbdata0 <= 0x80000000
//   3. poll sbcs.sbbusy until clear
//   4. sbaddress0 <= 0x02000000 (CLINT msip for hart 0), sbdata0 <= 1
//   5. poll sbcs.sbbusy until clear
//   6. dmcontrol <= dmactive=1, ndmreset=0   (release reset last)
// By the time the hart restarts from the boot ROM's reset vector, msip is
// already pending and BOOTADDR_REG already points at our program, so the
// boot ROM's own `wfi` should fall through immediately instead of parking.
//
// Trigger: rather than a physical button (Basys3 had one wired; this board
// doesn't, and we didn't want a new physical connection), this fires
// automatically a fixed delay after configuration -- long enough to run
// uart_tsi's ELF-into-DRAM load (already proven to work on its own) in
// between programming the bitstream and the autoloader firing.
class Arty100TDmiAutoloader(triggerDelayCycles: BigInt) extends Module {
  val io = IO(new Bundle {
    val dmiReq = Decoupled(new PlainDMIReq)
    val dmiResp = Flipped(Decoupled(new PlainDMIResp))
    val done = Output(Bool())
    val step = Output(UInt(4.W))
    // Extra diagnostic outputs: the first real-hardware test of this
    // module showed step=0/done=0 held indefinitely (far longer than the
    // trigger delay), which is consistent with either "the trigger timer
    // itself never fires" or "the very first DMI request never gets
    // accepted" -- those look identical from step/done alone. Exposing
    // the raw trigger and request-handshake signals distinguishes them
    // without guessing.
    val triggeredOut = Output(Bool())
    val reqValidOut = Output(Bool())
    val reqReadyOut = Output(Bool())
  })

  // DMIConsts.dmi_OP_{NONE,READ,WRITE} = 0/1/2 (2-bit field)
  val OP_READ  = 1.U(2.W)
  val OP_WRITE = 2.U(2.W)

  val DMI_DMCONTROL  = 0x10.U(7.W)
  val DMI_SBCS       = 0x38.U(7.W)
  val DMI_SBADDRESS0 = 0x39.U(7.W)
  val DMI_SBDATA0    = 0x3c.U(7.W)

  val SBBUSY_BIT = 21

  // Each entry: (dmi address, write data (ignored for reads), op, isPoll)
  // A "poll" step re-issues the same read until the response's sbbusy bit
  // (bit 21) reads back 0, instead of advancing after one clean response.
  // ORDER CHANGE from the Basys3 board's working sequence, and the reason
  // is a real, independently-confirmed finding from this board specifically:
  // uart_tsi's earlier attempts to write CLINT's msip register (over its
  // own separate TL bridge) always reported success but never landed, and
  // an SBA write to CLINT made *while ndmreset is held* (Basys3's exact
  // working order) showed the identical symptom here. But a DIRECT
  // read-back test proved BOOTADDR_REG's SBA write, ALSO made while
  // ndmreset is held, genuinely persists (read back 0x80000000 with no
  // write in that session). That asymmetry only makes sense if this
  // board's ndmreset -- per the RISC-V debug spec, "resets the platform,
  // except the Debug Module" -- has a broader reach here than on Basys3
  // and holds CLINT itself in reset for as long as ndmreset is asserted,
  // while BOOTADDR_REG (specifically designed to survive ndmreset so a
  // boot address can be staged before the core leaves reset) does not. A
  // write landing on a register that's simultaneously held in reset gets
  // overridden by the reset value on essentially every synchronous-reset
  // flip-flop implementation, which would silently swallow exactly the
  // CLINT write and no other, matching everything observed. Fix: write
  // BOOTADDR_REG before releasing reset (unchanged, already proven to
  // work), but write CLINT's msip *after* releasing it. This is safe --
  // `wfi` reacts to a pending interrupt whenever it arrives, so there's no
  // race to lose by writing the wake bit slightly after the core restarts.
  // Two extra checkpoint writes (to unused DRAM scratch addresses right
  // next to minimal_test.S's own magic-value address) added purely for
  // diagnosis: the ILA's probe4 packing has proven unreliable for reading
  // this module's internal state back (a prior capture claimed the
  // trigger-delay counter never fired at all, directly contradicted by
  // BOOTADDR_REG independently reading back as written). uart_tsi's
  // register read-back, by contrast, has been consistently trustworthy
  // all session. Writing distinct sentinels at two points lets us tell,
  // via a plain read-back with no ILA involved, exactly how far the
  // sequence actually got: checkpoint A confirms reset was released,
  // checkpoint B confirms the *entire* sequence including the CLINT
  // write ran to completion (regardless of whether CLINT's value itself
  // then reads back set).
  val steps: Seq[(UInt, UInt, UInt, Bool)] = Seq(
    (DMI_DMCONTROL,  0x3.U(32.W),        OP_WRITE, false.B), // 0: dmactive=1, ndmreset=1
    (DMI_SBADDRESS0, 0x1000.U(32.W),     OP_WRITE, false.B), // 1: BOOTADDR_REG address
    (DMI_SBDATA0,    "h80000000".U(32.W), OP_WRITE, false.B), // 2: BOOTADDR_REG <= 0x80000000
    (DMI_SBCS,       0.U(32.W),          OP_READ,  true.B),  // 3: poll sbbusy
    (DMI_DMCONTROL,  0x1.U(32.W),        OP_WRITE, false.B), // 4: dmactive=1, ndmreset=0 (release FIRST)
    (DMI_SBADDRESS0, "h80001004".U(32.W), OP_WRITE, false.B), // 5: checkpoint A scratch address
    (DMI_SBDATA0,    "hAAAAAAAA".U(32.W), OP_WRITE, false.B), // 6: checkpoint A <= reset released
    (DMI_SBCS,       0.U(32.W),          OP_READ,  true.B),  // 7: poll sbbusy
    (DMI_SBADDRESS0, "h2000000".U(32.W), OP_WRITE, false.B), // 8: CLINT msip0 address (after release)
    (DMI_SBDATA0,    0x1.U(32.W),        OP_WRITE, false.B), // 9: msip0 <= 1
    (DMI_SBCS,       0.U(32.W),          OP_READ,  true.B),  // 10: poll sbbusy
    (DMI_SBADDRESS0, "h80001008".U(32.W), OP_WRITE, false.B), // 11: checkpoint B scratch address
    (DMI_SBDATA0,    "hF00DF00D".U(32.W), OP_WRITE, false.B), // 12: checkpoint B <= full sequence done
    (DMI_SBCS,       0.U(32.W),          OP_READ,  true.B)   // 13: poll sbbusy
  )
  val nSteps = steps.length

  // ---- trigger delay counter ----
  val delayWidth = triggerDelayCycles.bitLength + 1
  val delayCounter = RegInit(0.U(delayWidth.W))
  val triggered = RegInit(false.B)
  when (!triggered) {
    delayCounter := delayCounter + 1.U
    when (delayCounter >= triggerDelayCycles.U) {
      triggered := true.B
    }
  }

  // idx: which step we're on. issued: have we sent this step's request yet.
  val idx = RegInit(0.U(log2Ceil(nSteps + 1).W))
  val issued = RegInit(false.B)
  val done = idx >= nSteps.U
  // UInt has no .min in Chisel3 -- clamp by hand so the Vec lookups below
  // never see an out-of-range index once idx reaches nSteps (done).
  val idxClamped = Mux(done, (nSteps - 1).U, idx)

  val stepAddr = VecInit(steps.map(_._1))(idxClamped)
  val stepData = VecInit(steps.map(_._2))(idxClamped)
  val stepOp   = VecInit(steps.map(_._3))(idxClamped)
  val stepIsPoll = VecInit(steps.map(_._4))(idxClamped)

  // ---- DMI request/response plumbing ----
  // Lesson from Basys3 Bug 1: the debug module can return its response in
  // the very same cycle the request handshake completes, before an FSM
  // sitting in a dedicated "waiting" state would even notice -- so latch
  // any response the instant it fires, independent of current state.
  io.dmiReq.valid := triggered && !done && !issued
  io.dmiReq.bits.addr := stepAddr
  io.dmiReq.bits.data := stepData
  io.dmiReq.bits.op := stepOp
  io.dmiResp.ready := true.B

  when (io.dmiReq.valid && io.dmiReq.ready) {
    issued := true.B
  }

  val respFire = io.dmiResp.valid && io.dmiResp.ready
  val respData = RegInit(0.U(32.W))
  val respOk = RegInit(false.B)
  val respSeen = RegInit(false.B)
  when (respFire) {
    respData := io.dmiResp.bits.data
    respOk := io.dmiResp.bits.resp === 0.U
    respSeen := true.B
  }

  // Advance logic, evaluated once per cycle after a response has been seen
  // for the currently-issued request.
  when (issued && respSeen) {
    respSeen := false.B
    when (!respOk) {
      // Basys3 Bug 4: any non-success DMI response means retry this exact
      // transaction, not advance.
      issued := false.B
    } .elsewhen (stepIsPoll && respData(SBBUSY_BIT)) {
      // Still busy -- reissue the same poll read.
      issued := false.B
    } .otherwise {
      idx := idx + 1.U
      issued := false.B
    }
  }

  io.done := done
  io.step := idx
  io.triggeredOut := triggered
  io.reqValidOut := io.dmiReq.valid
  io.reqReadyOut := io.dmiReq.ready
}
