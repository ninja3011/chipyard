// VGA output peripheral, real 8-bit color (3-3-2 RGB: R[2:0], G[2:0],
// B[1:0], 256 colors) -- matches the real Digilent Pmod VGA module the
// physical hardware for this project actually uses, widened up to that
// module's real 4-bit-per-channel DAC inputs on read-out (see
// widen3to4/widen2to4 below). Lives here (generators/chipyard), not
// under fpga/, for the same reason chipyard.example.GCD does: fpga/
// depends on generators/chipyard, not the other way around, so any type
// DigitalTop.scala mixes in must live on this side of that boundary.
// Board-specific pin binding (which physical pins R/G/B/HSYNC/VSYNC land
// on) belongs in fpga/src/main/scala/arty100t/ instead, as a
// HarnessBinder.
//
// Was originally monochrome (1 bit/pixel, 9,600-byte framebuffer, 3
// output signals) for first-bring-up risk reduction, then briefly a real
// 16-bit/pixel (4 bits/channel, 4,096-color) design -- that 16-bit
// version hit a real, structural Arty A7-100T resource ceiling (see
// below) and was replaced with this 8-bit/pixel version.
//
// REAL HISTORY of why this is 8 bits/pixel with two duplicated memories,
// not the simpler single-memory design tried first:
//   1. The very first synthesizable version reused rocket-chip's own
//      devices/tilelink/TestRAM.scala (TLTestRAM) pattern verbatim --
//      including its combinational-read Mem, despite that file's own
//      header saying outright "Do not use this for synthesis! Only for
//      simulation." That worked by accident at the old 9,600-byte 1bpp
//      size (small enough to fit as LUTs even though a combinational-read
//      Mem can never map to real Xilinx Block RAM, which requires a
//      synchronous read). At 153,600 bytes (the first real-color attempt,
//      16 bits/pixel) it required ~19,200 LUT-as-Distributed-RAM sites --
//      right at the Arty A7-100T's entire 19,000-site budget -- and
//      Vivado's placer failed outright (DRC UTLZ-1, confirmed via a real
//      synthesis run).
//   2. Fixed by switching to SyncReadMem (real BRAM-inferable) with a
//      pipelined 1-cycle TL response -- confirmed correct in simulation
//      (the color testbench re-passed, 25,600 real pixels), but the
//      *next* real Vivado run failed even harder (89,124 LUT-as-Memory
//      needed, worse than before). Root cause: this memory needs THREE
//      independent ports -- one write (TL) and two reads (TL response,
//      VGA scan-out) -- and real Xilinx Block RAM only has two ports
//      total. Chipyard's macro-compiler can map a clean 1-write+1-read
//      memory onto real BRAM (that's why the CPU's own cache arrays,
//      which are exactly that shape, always synthesized fine) but has no
//      BRAM template for a 3-port request, so it silently fell back to
//      flip-flops + mux logic instead -- confirmed via two consecutive
//      real synthesis failures, not assumed.
//   3. Real fix: duplicate the storage into two separate 2-port
//      SyncReadMems (memTL, memVGA), mirroring every write into both --
//      each one is then a clean 1-write+1-read shape a real BRAM can
//      hold. At 16 bits/pixel this duplication alone would have used
//      roughly 85% of the entire chip's Block RAM just for video,
//      leaving too little for the CPU's caches -- so pixel depth was
//      dropped to 8 bits/pixel (3-3-2 RGB) at the same time, which
//      brings the duplicated framebuffer down to a comfortable share of
//      the real ~4,860Kb device BRAM budget alongside everything else.
//      The real Pmod VGA hardware's DAC inputs are unchanged at 4 bits
//      per channel -- only the *stored* precision drops; widen3to4/
//      widen2to4 spread the stored 3-/2-bit values back out to the full
//      0-15 DAC range on read-out (bit replication, not literal padding
//      with zeros, so white still reads as true white on real hardware).

package chipyard.vga

import chisel3._
import chisel3.util._
import org.chipsalliance.cde.config.{Field, Parameters, Config}
import freechips.rocketchip.diplomacy._
import freechips.rocketchip.resources.MemoryDevice
import freechips.rocketchip.tilelink._
import freechips.rocketchip.subsystem.{BaseSubsystem, PBUS}
import freechips.rocketchip.prci._

case class VGAFramebufferParams(address: BigInt)

case object VGAFramebufferKey extends Field[Option[VGAFramebufferParams]](None)

// ClockSinkDomain (not plain LazyModule) is required here: a bare
// LazyModule attached only via pbus.coupleTo does not automatically get
// an implicit clock/reset for its own sequential logic (hit a real
// "No implicit clock" elaboration error without this) -- matches exactly
// how chipyard.example.GCD's non-externally-clocked case works
// (gcd.clockNode := pbus.fixedClockNode, body wrapped in
// withClockAndReset(clock, reset) { ... }).
class TLVGAFramebuffer(params: VGAFramebufferParams, beatBytes: Int)(implicit p: Parameters) extends ClockSinkDomain(ClockSinkParameters())(p) {
  val fbWidth = 320
  val fbHeight = 240
  val fbBytes = fbWidth * fbHeight // 8 bits/pixel (3-3-2 RGB) = 76,800 bytes
  // AddressSet's mask must be a contiguous run of low-order 1 bits
  // (base-2 power minus one) to decode a single contiguous range --
  // 153600 isn't a power of two either, hitting the exact same class of
  // bug the earlier 1bpp framebuffer's address decode had (see the real
  // bug found via the full-SoC integration test, documented in
  // ARTY-VGA-DOOM-FUNCVERIF-OVERNIGHT-PLAN.md): a non-power-of-two mask
  // silently decodes as a sparse, checkered set of small chunks instead
  // of one contiguous region. Rounding up to the next power of two here
  // from the start avoids re-triggering it.
  val addressSetBytes = { var n = 1; while (n < fbBytes) n = n << 1; n }
  val addressSet = AddressSet(params.address, addressSetBytes - 1)

  val device = new MemoryDevice
  val node = TLManagerNode(Seq(TLSlavePortParameters.v1(
    Seq(TLSlaveParameters.v1(
      address            = List(addressSet),
      resources          = device.reg,
      regionType         = RegionType.UNCACHED,
      executable         = false,
      supportsGet        = TransferSizes(1, beatBytes),
      supportsPutPartial = TransferSizes(1, beatBytes),
      supportsPutFull    = TransferSizes(1, beatBytes),
      fifoId             = Some(0))),
    beatBytes = beatBytes)))

  override lazy val module = new VGAImpl
  class VGAImpl extends Impl {
   val io = IO(new Bundle {
     val vga_hsync = Output(Bool())
     val vga_vsync = Output(Bool())
     // Vec(4, Bool()) here, not UInt(4.W) -- each bit needs to be a real,
     // independent top-level IO leaf so WithArty100TVGA's
     // IOPin(harnessIO.r(0)) etc. actually bind a physical pin constraint
     // to it. A plain UInt(4.W) port's r(0) is a bit-select expression
     // derived from one shared wire, not a genuine separate IO leaf --
     // confirmed as the real cause of a real bug tonight: Vivado's DRC
     // reported all 12 vga_r/g/b pins missing IOSTANDARD/LOC constraints
     // (while vga_hsync/vga_vsync, already plain Bool ports, got theirs
     // fine), because IOPin's package-pin/IOSTANDARD properties never
     // actually attached to those bit-select expressions.
     val vga_r = Output(Vec(4, Bool()))
     val vga_g = Output(Vec(4, Bool()))
     val vga_b = Output(Vec(4, Bool()))
   })
   withClockAndReset(clock, reset) {
    def bigBits(x: BigInt, tail: List[Boolean] = List.empty[Boolean]): List[Boolean] =
      if (x == 0) tail.reverse else bigBits(x >> 1, ((x & 1) == 1) :: tail)
    val mask = bigBits(addressSet.mask >> log2Ceil(beatBytes))

    val (in, edge) = node.in(0)

    val addrBits = (mask zip edge.addr_hi(in.a.bits).asBools).filter(_._1).map(_._2)
    val memAddress = Cat(addrBits.reverse)
    // Two separate, real Block-RAM-inferable memories holding identical
    // content (every write mirrored into both) instead of one shared
    // 3-port memory -- see header comment for why: real Xilinx BRAM only
    // has two ports, and a naive single-memory design needing 1 write +
    // 2 independent reads (TL response, VGA scan-out) has no BRAM
    // template to fall back to. Each of these is a clean 1-write+1-read
    // shape instead.
    val memTL = SyncReadMem(1 << addrBits.size, Vec(beatBytes, Bits(8.W)))
    val memVGA = SyncReadMem(1 << addrBits.size, Vec(beatBytes, Bits(8.W)))

    // A synchronous-read Mem can't answer in the same cycle a request is
    // accepted the way TLTestRAM's combinational version does, so this is
    // a real (if minimal) 1-deep pipeline: accept at most one request at
    // a time, hold it until its answer (already latched by the SyncReadMem
    // read below) is delivered on in.d the following cycle.
    val reqValid = RegInit(false.B)
    val reqBits = Reg(chiselTypeOf(in.a.bits))
    val reqHasData = Reg(Bool())

    in.a.ready := !reqValid || in.d.ready
    when (in.a.fire) {
      reqValid := true.B
      reqBits := in.a.bits
      reqHasData := edge.hasData(in.a.bits)
    } .elsewhen (in.d.fire) {
      reqValid := false.B
    }
    in.d.valid := reqValid

    val hasData = edge.hasData(in.a.bits)
    val wdata = VecInit(Seq.tabulate(beatBytes) {i => in.a.bits.data(8*(i+1)-1, 8*i)})
    // Issued using this cycle's incoming request (valid when in.a.fire),
    // landing in the SyncReadMem's internal read register on the same
    // clock edge that latches reqValid/reqBits above -- so rdata lines up
    // with reqBits on the very next cycle, exactly when in.d is asserted.
    val rdata = memTL.read(memAddress, in.a.fire)

    in.d.bits := edge.AccessAck(reqBits)
    in.d.bits.data := Cat(rdata.reverse)
    in.d.bits.corrupt := false.B
    in.d.bits.opcode := Mux(reqHasData, TLMessages.AccessAck, TLMessages.AccessAckData)
    when (in.a.fire && hasData) {
      memTL.write(memAddress, wdata, in.a.bits.mask.asBools)
      memVGA.write(memAddress, wdata, in.a.bits.mask.asBools)
    }

    in.b.valid := false.B
    in.c.ready := true.B
    in.e.ready := true.B

    // ---------------- VGA timing (640x480 via 2x pixel-doubling) ----------------
    val pclk_toggle = RegInit(false.B)
    pclk_toggle := !pclk_toggle
    val pixelTick = pclk_toggle // ~25MHz pixel rate off the 50MHz bus clock

    val hTotal = 800; val hVisible = 640; val hFrontPorch = 16; val hSyncWidth = 96
    val vTotal = 525; val vVisible = 480; val vFrontPorch = 10; val vSyncWidth = 2

    val hCount = RegInit(0.U(log2Ceil(hTotal).W))
    val vCount = RegInit(0.U(log2Ceil(vTotal).W))

    when (pixelTick) {
      when (hCount === (hTotal - 1).U) {
        hCount := 0.U
        when (vCount === (vTotal - 1).U) { vCount := 0.U } .otherwise { vCount := vCount + 1.U }
      } .otherwise {
        hCount := hCount + 1.U
      }
    }

    val visible = (hCount < hVisible.U) && (vCount < vVisible.U)
    val hsync = !(hCount >= (hVisible + hFrontPorch).U && hCount < (hVisible + hFrontPorch + hSyncWidth).U)
    val vsync = !(vCount >= (vVisible + vFrontPorch).U && vCount < (vVisible + vFrontPorch + vSyncWidth).U)

    // 2x pixel-doubling: 320x240 source, 640x480 display.
    val fbX = hCount >> 1
    val fbY = vCount >> 1
    val pixelIndex = fbY * fbWidth.U + fbX
    // 1 byte per pixel now (3-3-2 RGB) -- byteIndex is simply pixelIndex,
    // no more multi-byte-per-pixel/beat-straddling math needed.
    val byteIndex = pixelIndex
    val byteInBeat0 = byteIndex(log2Ceil(beatBytes) - 1, 0)
    val beatIndex = byteIndex >> log2Ceil(beatBytes)

    // Widen a stored 3-bit or 2-bit channel value back out to the real
    // Pmod VGA hardware's actual 4-bit DAC input range via bit
    // replication (not zero-padding) so the stored maximum (e.g. 3'b111)
    // still reads as the DAC's true maximum (4'b1111), not a dim
    // 4'b1110 -- e.g. 3-bit abc -> abca, 2-bit ab -> abab.
    def widen3to4(x: UInt): UInt = Cat(x, x(2))
    def widen2to4(x: UInt): UInt = Cat(x, x)

    // Independent SyncReadMem read port for the scanner (its own
    // duplicated memory, memVGA -- see header comment), always enabled
    // (every pixelTick issues a real read). Single-byte 3-3-2 pixel:
    // bits [7:5]=R[2:0], [4:2]=G[2:0], [1:0]=B[1:0] -- matches the
    // software packing convention (pixel8 = (R3<<5)|(G3<<2)|B2) exactly.
    // mem.read's own internal register is what supplies the 1-cycle delay
    // (SyncReadMem read latency) that pixelR/G/B need to line up with
    // hsync_d/vsync_d/visible_d below -- no separate RegNext needed on
    // the pixel values themselves (adding one would double the delay and
    // misalign it).
    val scanRdata = memVGA.read(beatIndex, true.B)
    val pixelByte = scanRdata(byteInBeat0)
    val pixelR = widen3to4(pixelByte(7, 5))
    val pixelG = widen3to4(pixelByte(4, 2))
    val pixelB = widen2to4(pixelByte(1, 0))

    val hsync_d = RegNext(hsync)
    val vsync_d = RegNext(vsync)
    val visible_d = RegNext(visible)

    io.vga_hsync := hsync_d
    io.vga_vsync := vsync_d
    io.vga_r := VecInit(Mux(visible_d, pixelR, 0.U).asBools)
    io.vga_g := VecInit(Mux(visible_d, pixelG, 0.U).asBools)
    io.vga_b := VecInit(Mux(visible_d, pixelB, 0.U).asBools)
   }
  }
}

class VGAFramebufferOutputBundle extends Bundle {
  val hsync = Output(Bool())
  val vsync = Output(Bool())
  // Vec(4, Bool()), not UInt(4.W) -- see TLVGAFramebuffer's io comment
  // for why: each bit must be a genuine, separate IO leaf for
  // WithArty100TVGA's per-bit IOPin() pin binding to actually work.
  val r = Output(Vec(4, Bool()))
  val g = Output(Vec(4, Bool()))
  val b = Output(Vec(4, Bool()))
}

// Back to InModuleBody (matching chipyard.example.GCD exactly) now that
// TLVGAFramebuffer extends ClockSinkDomain: an earlier attempt at this
// with a bare LazyModule (before the ClockSinkDomain fix) hit "InModuleBody
// contents were requested before module was evaluated" -- that was really
// a symptom of the missing clock domain, not a problem with InModuleBody
// itself. A subsequent attempt exposing the raw LazyModule and connecting
// vga.module.io directly from the IOBinder hit a *different* real error
// ("Sink or source unavailable to current module" -- a cross-module
// connection reaching into an already-elaborated child from an unrelated
// ChipTop-level context isn't legal Chisel). InModuleBody exists
// specifically to schedule this kind of connection at the right
// elaboration-time context.
//
// REAL ROOT CAUSE of the "requested before module was evaluated" error,
// found by reading InModuleBody's own source
// (diplomacy/lazymodule/InModuleBody.scala): InModuleBody{} returns a
// ModuleValue[T], not T -- the conversion to T only happens via an
// implicit conversion at first *use*, deferred until after the module is
// actually evaluated. An explicit `val vga_out: Option[Bundle] = ...` type
// annotation forces that conversion to run immediately at trait
// construction time instead, before evaluation -- exactly the error seen.
// chipyard.example.GCD's `val gcd_busy = InModuleBody { ... }` has no such
// annotation for exactly this reason. Leaving this val untyped (letting
// Scala infer Option[ModuleValue[...]]) so the conversion stays deferred
// to where the IOBinder actually reads it, matching GCD.
trait CanHavePeripheryVGAFramebuffer { this: BaseSubsystem =>
  private val pbus = locateTLBusWrapper(PBUS)

  val vga_out = p(VGAFramebufferKey).map { params =>
    val vga = LazyModule(new TLVGAFramebuffer(params, pbus.beatBytes)(p))
    vga.clockNode := pbus.fixedClockNode
    pbus.coupleTo("vga-framebuffer") {
      // TLInwardClockCrossingHelper (not a raw `vga.node := ... := _`),
      // matching chipyard.example.GCD's non-externally-clocked case
      // exactly -- this turned out to be the actual missing piece: the
      // earlier "InModuleBody contents were requested before module was
      // evaluated" error persisted even after adding ClockSinkDomain,
      // until this helper was used for the coupling too.
      TLInwardClockCrossingHelper("vga_crossing", vga, vga.node)(SynchronousCrossing()) :=
      TLFragmenter(pbus.beatBytes, pbus.blockBytes) := _
    }
    InModuleBody {
      val io = IO(new VGAFramebufferOutputBundle).suggestName("vga_periph")
      io.hsync := vga.module.io.vga_hsync
      io.vsync := vga.module.io.vga_vsync
      io.r := vga.module.io.vga_r
      io.g := vga.module.io.vga_g
      io.b := vga.module.io.vga_b
      io
    }
  }
}

class WithVGAFramebuffer(address: BigInt = 0x4000000L) extends Config((site, here, up) => {
  case VGAFramebufferKey => Some(VGAFramebufferParams(address = address))
})
