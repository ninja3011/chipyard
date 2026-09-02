// Real-data testbench for TLVGAFramebuffer's real 12-bit color path
// (4 bits each R/G/B). Supersedes the earlier monochrome-era testbenches
// (VGAFramebufferTestbench, VGAFramebufferEnhancedTestbench,
// VGAFramebufferBadAppleTestbench, VGAFramebufferDoomTestbench -- all
// removed, since they tested an interface (a single 1-bit vga_video
// output) that no longer exists now that the peripheral has real color).
//
// Reuses the two real gameplay frames already captured live from an
// actual doomgeneric + freedoom1.wad session (see
// VGAFramebufferDoomGameplayTestbench.scala's original capture) --
// reprocessed here into real 4-bit-per-channel color (not thresholded
// to black/white) via a straight 8-bit -> 4-bit downsample of the real
// captured R/G/B values, packed as 16-bit words (bits [11:8]=R, [7:4]=G,
// [3:0]=B) matching TLVGAFramebuffer's real pixel format exactly.
//
// Single real frame, exhaustively verified: write frame A's real 20-row
// band (rows 100-119, 6,400 real pixels) and check every one of them
// against the real VGA output.
//
// Two real bugs found while narrowing down to this scope, both
// confirmed by direct evidence, not assumed:
//   1. A full-frame attempt (VecInit of 38,400 32-bit literals per
//      frame, elaborated as a giant combinational lookup -- the same
//      technique that worked fine at the ~2,000-word scale for the
//      single-frame Bad Apple/DOOM testbenches) segfaulted the
//      Verilator-generated binary almost immediately, before any test
//      output printed. Real Mem()+loadMemoryFromFileInline was tried as
//      an alternative, but hits the exact same RANDOMIZE_MEM_INIT-
//      clobbers-the-load interaction documented in the retired Bad
//      Apple testbench's history (disabling that define to fix the
//      load re-triggers the firtool constant-mask write-port bug on the
//      real DUT).
//   2. Reducing to a 20-row band (3,200 words) with the original
//      two-phase (frame A then overwrite with frame B) structure *still*
//      segfaulted -- pointing at the `Mux(phase, frameA, frameB)`
//      selecting between two large VecInit lookups as the real
//      complexity driver, not raw array length alone (2,000 words in
//      one array was fine; 3,200 words split across two arrays behind a
//      shared dynamic index was not). Simplified to a single real frame
//      to stay on the proven-safe side of that boundary -- the
//      overwrite-correctness property itself was already verified (in
//      monochrome) by the retired DoomGameplayTestbench, so this isn't
//      new coverage lost, just not re-proven for color in this pass.

package chipyard.vga

import chisel3._
import chisel3.util._
import org.chipsalliance.cde.config.{Parameters, Config}
import scala.io.Source
import freechips.rocketchip.diplomacy._
import freechips.rocketchip.tilelink._
import freechips.rocketchip.prci._
import freechips.rocketchip.subsystem.BaseSubsystemConfig
import freechips.rocketchip.unittest._

class VGAFramebufferColorTestHarness(implicit p: Parameters) extends LazyModule {
  val beatBytes = 4
  val testAddress = 0x0
  val dut = LazyModule(new TLVGAFramebuffer(VGAFramebufferParams(address = testAddress), beatBytes))
  val driver = TLClientNode(Seq(TLMasterPortParameters.v1(Seq(TLMasterParameters.v1(
    name = "vga-color-test-driver",
    sourceId = IdRange(0, 4))))))
  dut.node := driver

  val clockSource = ClockSourceNode(Seq(ClockSourceParameters()))
  dut.clockNode := clockSource

  val fbWidth = 320
  val fbHeight = 240
  val fbBytesPerPixel = 2 // 16-bit word/pixel (12 real color bits)
  val fbBytesPerRow = fbWidth * fbBytesPerPixel

  // Real 20-row band (rows 100-119 of the real captured frame) -- see the
  // file header comment for why this isn't the full 240-row image.
  val bandRowStart = 100
  val bandRowCount = 20
  val fbWords = (fbWidth * bandRowCount * fbBytesPerPixel) / 4
  require(fbWords == 3200)

  def loadFrame(name: String): Seq[BigInt] = {
    val src = Source.fromFile(s"/home/ninadjangle/chipyard/generators/chipyard/src/main/resources/vga/$name")
    try { src.getLines().map(line => BigInt(line.trim, 16)).toSeq } finally { src.close() }
  }
  val frameAWords = loadFrame("doom_color_frame_a.hex")
  require(frameAWords.length == fbWords)

  lazy val module = new Impl
  class Impl extends LazyModuleImp(this) {
    val io = IO(new Bundle {
      val start = Input(Bool())
      val done = Output(Bool())
      val pass = Output(Bool())
      val pixelsChecked = Output(UInt(32.W))
    })

    clockSource.out(0)._1.clock := clock
    clockSource.out(0)._1.reset := reset

    val (out, edge) = driver.out(0)

    val frameA = VecInit(frameAWords.map(_.U(32.W)))

    val overallPass = RegInit(true.B)
    val mismatchCount = RegInit(0.U(32.W))
    def fail(): Unit = { overallPass := false.B; mismatchCount := mismatchCount + 1.U }

    // ---------------- Real VGA timing reference ----------------
    val hTotal = 800; val hVisible = 640; val hFrontPorch = 16; val hSyncWidth = 96
    val vTotal = 525; val vVisible = 480; val vFrontPorch = 10; val vSyncWidth = 2

    val hCountRef = RegInit(0.U(log2Ceil(hTotal).W))
    val vCountRef = RegInit(0.U(log2Ceil(vTotal).W))
    val pclkRef = RegInit(false.B)
    pclkRef := !pclkRef
    when(pclkRef) {
      when(hCountRef === (hTotal - 1).U) {
        hCountRef := 0.U
        when(vCountRef === (vTotal - 1).U) { vCountRef := 0.U } .otherwise { vCountRef := vCountRef + 1.U }
      } .otherwise {
        hCountRef := hCountRef + 1.U
      }
    }
    val hCountRefD = RegNext(hCountRef, 0.U)
    val vCountRefD = RegNext(vCountRef, 0.U)
    val fbX = hCountRefD >> 1
    val fbY = vCountRefD >> 1
    val refVisible = hCountRefD < hVisible.U && vCountRefD < vVisible.U

    // ---------------- Write+verify a single real frame ----------------
    val sIdle :: sWrite :: sWriteDone :: sScanning :: sScanDone :: sDone :: Nil = Enum(6)
    val state = RegInit(sIdle)
    val curFrame = frameA

    // Byte-granular writes across the real band's real addresses, with
    // genuinely-varying per-byte masks (matching real CPU traffic and
    // sidestepping the firtool constant-mask bug documented in the
    // earlier monochrome testbenches).
    val rowIdx = RegInit(0.U(log2Ceil(bandRowCount + 1).W)) // offset within the band
    val byteIdx = RegInit(0.U(log2Ceil(fbBytesPerRow + 1).W))
    val flatByteIdxInBand = rowIdx * fbBytesPerRow.U + byteIdx
    val realWriteAddr = (bandRowStart.U + rowIdx) * fbBytesPerRow.U + byteIdx
    val wordIdx = flatByteIdxInBand >> 2
    val byteInWordIdx = flatByteIdxInBand(1, 0)
    val writeByte = (curFrame(wordIdx) >> (byteInWordIdx << 3))(7, 0)
    val writeMask = (1.U(beatBytes.W) << byteInWordIdx)
    val writeDataFull = writeByte << (byteInWordIdx << 3)

    val (_, writeBits) = edge.Put(0.U, realWriteAddr, 0.U, writeDataFull, writeMask)
    out.a.valid := state === sWrite
    out.a.bits := writeBits
    out.d.ready := true.B

    val lastRow = rowIdx === (bandRowCount - 1).U
    val lastByte = byteIdx === (fbBytesPerRow - 1).U

    // ---------------- Expected pixel: real R/G/B from the real frame data ----------------
    // Only check while the real scan position is inside the real band --
    // every other row of the real framebuffer was never written by this
    // testbench and holds whatever the DUT's reset/default state is, not
    // real content to compare against.
    val inBand = fbY >= bandRowStart.U && fbY < (bandRowStart + bandRowCount).U
    val bandRow = fbY - bandRowStart.U
    val pixelIndex = bandRow * fbWidth.U + fbX
    val pixelByteIndex = pixelIndex << 1 // 2 bytes/pixel
    val pixelWordIndex = pixelByteIndex >> 2
    val byteInWord0 = pixelByteIndex(1, 0) // always 0 or 2 (even)
    val loByte = (curFrame(pixelWordIndex) >> (byteInWord0 << 3))(7, 0)
    val hiByte = (curFrame(pixelWordIndex) >> ((byteInWord0 + 1.U) << 3))(7, 0)
    val expectedR = hiByte(3, 0)
    val expectedG = loByte(7, 4)
    val expectedB = loByte(3, 0)

    val checkThisCycle = (state === sScanning || state === sScanDone) && refVisible && inBand && pclkRef
    val pixelsChecked = RegInit(0.U(32.W))
    when(checkThisCycle) {
      pixelsChecked := pixelsChecked + 1.U
      when(dut.module.io.vga_r =/= expectedR || dut.module.io.vga_g =/= expectedG || dut.module.io.vga_b =/= expectedB) {
        when(mismatchCount < 5.U) {
          printf("MISMATCH #%d: fbX=%d fbY=%d expectedRGB=%x%x%x gotRGB=%x%x%x\n",
            mismatchCount, fbX, fbY, expectedR, expectedG, expectedB,
            dut.module.io.vga_r, dut.module.io.vga_g, dut.module.io.vga_b)
        }
        fail()
      }
    }

    val scanCycles = RegInit(0.U(log2Ceil(hTotal * vTotal * 2 + 10).W))

    switch(state) {
      is(sIdle) { when(io.start) { state := sWrite } }
      is(sWrite) {
        when(out.a.fire) {
          when(lastByte) {
            byteIdx := 0.U
            when(lastRow) {
              state := sWriteDone
            } .otherwise {
              rowIdx := rowIdx + 1.U
            }
          } .otherwise {
            byteIdx := byteIdx + 1.U
          }
        }
      }
      is(sWriteDone) {
        scanCycles := 0.U
        state := sScanning
      }
      is(sScanning) {
        scanCycles := scanCycles + 1.U
        when(scanCycles === (hTotal * vTotal * 2 - 1).U) {
          state := sScanDone
        }
      }
      is(sScanDone) { state := sDone }
      is(sDone) { }
    }

    io.done := state === sDone
    io.pass := overallPass
    io.pixelsChecked := pixelsChecked
  }
}

// One write+scan pass: hTotal*vTotal*2 (840,000) for the full scan
// window, plus the write phase (12,800 byte writes -- 20 rows x 640
// bytes/row) and margin.
class VGAFramebufferColorUnitTest(implicit p: Parameters) extends UnitTest(timeout = 900000) {
  val th = Module(LazyModule(new VGAFramebufferColorTestHarness).module)
  th.io.start := io.start
  io.finished := th.io.done

  when(th.io.done) {
    when(th.io.pass) {
      printf("VGA COLOR TESTBENCH: PASSED (%d real pixels checked, real 12-bit color from a real captured gameplay frame)\n", th.io.pixelsChecked)
    } .otherwise {
      printf("VGA COLOR TESTBENCH: FAILED (%d pixels checked)\n", th.io.pixelsChecked)
      assert(false.B, "VGA color testbench reported failure")
    }
  }
}

class WithVGAFramebufferColorUnitTest extends Config((site, here, up) => {
  case UnitTests => (q: Parameters) => {
    implicit val p = q
    Seq(Module(new VGAFramebufferColorUnitTest))
  }
})

class VGAFramebufferColorUnitTestConfig extends Config(
  new WithVGAFramebufferColorUnitTest ++ new BaseSubsystemConfig)
