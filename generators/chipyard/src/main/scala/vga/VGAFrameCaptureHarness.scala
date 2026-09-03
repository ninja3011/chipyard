// Captures real frames from the real VGA output signals of a real,
// booting SoC (real CPU executing real code, e.g. doom-arty100t.elf) --
// not a synthetic TileLink driver standing in for the CPU, the actual
// hsync/vsync/r/g/b signals TLVGAFramebuffer drives in response to
// whatever the real program running on the real Rocket core actually
// wrote to the framebuffer.
//
// Mirrors the DUT's own VGA timing constants in a small monitor module
// (the same reference-counter-mirroring technique used throughout
// tonight's testbenches) to know which cycle corresponds to which real
// (row, column), then dumps a downsampled 160x120 grid of each frame's
// real R/G/B content via a printf per sample -- deliberately downsampled
// and deduplicated (see below) to keep both the generated hardware and
// the print volume manageable over a real, many-frame capture.
package chipyard.vga

import chisel3._
import chisel3.util._
import org.chipsalliance.cde.config.Config
import chipyard.harness.{HarnessBinder, HasHarnessInstantiators}
import chipyard.iobinders.VGAFramebufferPort

class VGAFrameCaptureMonitor extends Module {
  val io = IO(new Bundle {
    val hsync = Input(Bool())
    val vsync = Input(Bool())
    // Vec(4, Bool()), matching VGAFramebufferOutputBundle's real port
    // shape (changed from UInt(4.W) to fix a real per-pin XDC constraint
    // bug -- see VGAFramebuffer.scala's io comment).
    val r = Input(Vec(4, Bool()))
    val g = Input(Vec(4, Bool()))
    val b = Input(Vec(4, Bool()))
  })

  // Real TLVGAFramebuffer timing constants, mirrored (not independently
  // derived) -- must match VGAFramebuffer.scala exactly or this monitor
  // would sample the wrong (row, column) for a given cycle.
  val hTotal = 800; val hVisible = 640
  val vTotal = 525; val vVisible = 480

  val hCount = RegInit(0.U(log2Ceil(hTotal).W))
  val vCount = RegInit(0.U(log2Ceil(vTotal).W))
  val pclk = RegInit(false.B)
  pclk := !pclk
  when(pclk) {
    when(hCount === (hTotal - 1).U) {
      hCount := 0.U
      when(vCount === (vTotal - 1).U) { vCount := 0.U } .otherwise { vCount := vCount + 1.U }
    } .otherwise {
      hCount := hCount + 1.U
    }
  }

  // io.{hsync,vsync,r,g,b} are the DUT's *registered* outputs (one cycle
  // behind its own internal hCount/vCount) -- delay this reference
  // counter by one cycle too, same pattern as every testbench tonight.
  val hCountD = RegNext(hCount, 0.U)
  val vCountD = RegNext(vCount, 0.U)
  val fbX = hCountD >> 1 // real 0-319 source column (2x pixel-doubling)
  val fbY = vCountD >> 1 // real 0-239 source row
  val visible = hCountD < hVisible.U && vCountD < vVisible.U

  // Sample a downsampled 160x120 grid (every 2nd real source column and
  // row), and only on the *first* phase of the h/v pixel-doubling (the
  // LSB of the un-shifted hCountD/vCountD) -- both cuts are about
  // avoiding redundant/oversized hardware and print volume over a real
  // multi-frame capture, not about needing less real data: each sample
  // is still a real, distinct R/G/B value the DUT actually drove, just
  // one representative sample per 2x2 real source block instead of
  // printing that same block's value up to 4 times.
  val sampleThisPixel = visible && pclk &&
    hCountD(0) === 0.U && vCountD(0) === 0.U &&
    fbX(0) === 0.U && fbY(0) === 0.U
  when(sampleThisPixel) {
    printf("VGAPIXEL row=%d col=%d rgb=%x%x%x\n", fbY >> 1, fbX >> 1, io.r.asUInt, io.g.asUInt, io.b.asUInt)
  }

  // Tag each frame with a running counter (real vsync rising edges, i.e.
  // real end-of-frame boundaries) so multiple captured frames can be
  // told apart in the log.
  val vsyncD = RegNext(io.vsync, true.B)
  val frameCounter = RegInit(0.U(32.W))
  when(!vsyncD && io.vsync) { // rising edge = end of the real vsync pulse
    frameCounter := frameCounter + 1.U
  }
  when(sampleThisPixel && fbX === 0.U && fbY === 0.U) {
    printf("VGAFRAMEBOUNDARY frame=%d\n", frameCounter)
  }
}

class WithVGAFrameCapture extends HarnessBinder({
  case (th: HasHarnessInstantiators, port: VGAFramebufferPort, chipId: Int) => {
    val vga = port.getIO()
    val monitor = Module(new VGAFrameCaptureMonitor)
    monitor.io.hsync := vga.hsync
    monitor.io.vsync := vga.vsync
    monitor.io.r := vga.r
    monitor.io.g := vga.g
    monitor.io.b := vga.b
  }
})

class VGAFrameCaptureSimConfig extends Config(
  new WithVGAFrameCapture ++
  new WithVGAFramebuffer(address = 0x4000000L) ++
  new chipyard.RocketConfig)
