// VGA output peripheral, real 12-bit color (4 bits each R/G/B, 4096
// colors) -- matches the real Digilent Pmod VGA module the physical
// hardware for this project actually uses. Lives here
// (generators/chipyard), not under fpga/, for the same reason
// chipyard.example.GCD does: fpga/ depends on generators/chipyard, not
// the other way around, so any type DigitalTop.scala mixes in must live
// on this side of that boundary. Board-specific pin binding (which
// physical pins R/G/B/HSYNC/VSYNC land on) belongs in
// fpga/src/main/scala/arty100t/ instead, as a HarnessBinder.
//
// Was originally monochrome (1 bit/pixel, 9600-byte framebuffer, 3
// output signals) for first-bring-up risk reduction -- upgraded to real
// color once the monochrome path was fully verified (functional
// integration test, CPU regression, real-data Bad Apple/DOOM
// testbenches, real bitstream) and the real Pmod VGA hardware was
// confirmed as what actually ships. Pixel format: 16-bit word per pixel,
// bits [11:8]=R[3:0], [7:4]=G[3:0], [3:0]=B[3:0] (top 4 bits unused),
// packed little-endian (low byte = {G,B}, high byte = {0000,R}) --
// framebuffer grows from 9,600 bytes to 153,600 bytes (320x240x2), still
// comfortably inside the Arty A7-100T's real BRAM budget.
//
// Design choices carried over unchanged from the monochrome version:
//   - Framebuffer scanned out at real 640x480@~60Hz VGA timing via 2x
//     pixel-doubling from a 320x240 source. Pixel clock is a toggle
//     flip-flop off the existing 50MHz harness clock (no new PLL domain,
//     no new CDC risk).
//   - TL slave logic is NOT built on TLRegisterNode+regmap: regmap's
//     automatic DTS "reg" resource binding hit a real, reproducible
//     failure once this has more than a couple of individually-mapped
//     fields ("must be a single range" -- regmap is built for a handful
//     of control registers, not a multi-KB bulk memory addressed one
//     word at a time). Instead this copies the real TL A/D-channel
//     handling straight from rocket-chip's own
//     devices/tilelink/TestRAM.scala (TLTestRAM) -- a proven, trusted
//     TileLink-to-Mem bridge using MemoryDevice for correct DTS binding
//     with no regmap involved -- and adds a second, VGA-only read port
//     onto the same underlying Mem.

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
  val fbBytes = fbWidth * fbHeight * 2 // 16 bits/pixel (12 real color bits) = 153,600 bytes
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
     val vga_r = Output(UInt(4.W))
     val vga_g = Output(UInt(4.W))
     val vga_b = Output(UInt(4.W))
   })
   withClockAndReset(clock, reset) {
    def bigBits(x: BigInt, tail: List[Boolean] = List.empty[Boolean]): List[Boolean] =
      if (x == 0) tail.reverse else bigBits(x >> 1, ((x & 1) == 1) :: tail)
    val mask = bigBits(addressSet.mask >> log2Ceil(beatBytes))

    val (in, edge) = node.in(0)

    val addrBits = (mask zip edge.addr_hi(in.a.bits).asBools).filter(_._1).map(_._2)
    val memAddress = Cat(addrBits.reverse)
    // Same combinational-read Mem TLTestRAM uses for the TL side -- proven
    // protocol timing, copied verbatim in spirit. The VGA scanner below
    // gets its own read port on this same Mem (Chisel supports multiple
    // read ports on one Mem; Vivado infers the BRAM/LUTRAM accordingly).
    val mem = Mem(1 << addrBits.size, Vec(beatBytes, Bits(8.W)))

    in.a.ready := in.d.ready
    in.d.valid := in.a.valid

    val hasData = edge.hasData(in.a.bits)
    val wdata = VecInit(Seq.tabulate(beatBytes) {i => in.a.bits.data(8*(i+1)-1, 8*i)})

    in.d.bits := edge.AccessAck(in.a.bits)
    in.d.bits.data := Cat(mem(memAddress).reverse)
    in.d.bits.corrupt := false.B
    in.d.bits.opcode := Mux(hasData, TLMessages.AccessAck, TLMessages.AccessAckData)
    when (in.a.fire && hasData) {
      mem.write(memAddress, wdata, in.a.bits.mask.asBools)
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
    // 16 bits (2 bytes) per pixel now, not 1 bit -- byteIndex is simply
    // pixelIndex*2. Since 2 always divides evenly into a 4-byte beat, a
    // pixel's two bytes never straddle a beat boundary: byteInBeat0 is
    // always even (0 or 2 for beatBytes=4), so byteInBeat0+1 stays inside
    // the same beat.
    val byteIndex = pixelIndex << 1
    val byteInBeat0 = byteIndex(log2Ceil(beatBytes) - 1, 0)
    val beatIndex = byteIndex >> log2Ceil(beatBytes)

    // Little-endian pixel word: low byte (byteInBeat0) = {G[3:0],B[3:0]},
    // high byte (byteInBeat0+1) = {4'b0, R[3:0]} -- matches the software
    // packing convention (pixel16 = (R<<8)|(G<<4)|B) exactly, no
    // reversal needed anywhere.
    val loByte = mem(beatIndex)(byteInBeat0)
    val hiByte = mem(beatIndex)(byteInBeat0 + 1.U)
    val pixelR = hiByte(3, 0)
    val pixelG = loByte(7, 4)
    val pixelB = loByte(3, 0)

    val hsync_d = RegNext(hsync)
    val vsync_d = RegNext(vsync)
    val visible_d = RegNext(visible)
    // Register to match combinational-Mem read timing (same pattern as
    // the earlier 1bpp version's pixelBit_d).
    val pixelR_d = RegNext(pixelR)
    val pixelG_d = RegNext(pixelG)
    val pixelB_d = RegNext(pixelB)

    io.vga_hsync := hsync_d
    io.vga_vsync := vsync_d
    io.vga_r := Mux(visible_d, pixelR_d, 0.U)
    io.vga_g := Mux(visible_d, pixelG_d, 0.U)
    io.vga_b := Mux(visible_d, pixelB_d, 0.U)
   }
  }
}

class VGAFramebufferOutputBundle extends Bundle {
  val hsync = Output(Bool())
  val vsync = Output(Bool())
  val r = Output(UInt(4.W))
  val g = Output(UInt(4.W))
  val b = Output(UInt(4.W))
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
