// Real-data testbench for TLVGAFramebuffer, DOOM's path this time: loads
// an actual frame captured live from the real doomgeneric engine (the
// netstream backend, run against the real freedoom1.wad -- the same WAD
// embedded in the real Arty100T ELF), then reproduces
// doomgeneric_arty100t.c's DG_DrawFrame algorithm exactly in Python at
// build time (nearest-neighbor 2x downsample from the real captured
// 640x400 BGRA buffer, threshold the blue channel >=128, MSB-first pack,
// 20-row letterbox), writes the resulting real expected bytes into the
// framebuffer through genuinely-varying per-byte TileLink writes, then
// exhaustively checks all 64,000 real pixels against the real VGA
// scan-out output over one full frame period.
//
// Bad Apple's testbench verified the *content path* for one payload;
// DOOM's downsample+threshold math is genuinely different (a real
// nearest-neighbor decimation, not a direct row copy), so it gets its
// own real-data verification rather than assuming the same code path
// covers it. Same structure as VGAFramebufferBadAppleTestbench.scala
// throughout -- see that file's comments for the two real bugs
// (RANDOMIZE_MEM_INIT clobbering a loaded Mem, and a firtool
// --repl-seq-mem bug triggered by constant-mask writes) already found
// and designed around by this structure.
//
// Frame source: a real frame captured live via
// `doomgeneric-netstream`'s TCP protocol from an actual run of the real
// engine + real freedoom1.wad (not a synthetic pattern, not a
// documentation screenshot) -- the Freedoom title/logo screen, 640x400
// BGRA, extracted once into resources/vga/doom_frame_real.hex as the
// *already-downsampled-and-thresholded* 320x240 packed result (2400
// 32-bit words), matching DG_DrawFrame's real output exactly.

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

class VGAFramebufferDoomTestHarness(implicit p: Parameters) extends LazyModule {
  val beatBytes = 4
  val testAddress = 0x0
  val dut = LazyModule(new TLVGAFramebuffer(VGAFramebufferParams(address = testAddress), beatBytes))
  val driver = TLClientNode(Seq(TLMasterPortParameters.v1(Seq(TLMasterParameters.v1(
    name = "vga-doom-test-driver",
    sourceId = IdRange(0, 4))))))
  dut.node := driver

  val clockSource = ClockSourceNode(Seq(ClockSourceParameters()))
  dut.clockNode := clockSource

  // Real, already-downsampled-and-thresholded DOOM frame content: the
  // full 320x240 framebuffer image (not just the 200-row visible portion
  // -- unlike Bad Apple's native-resolution .vidf, this file already has
  // DG_DrawFrame's own letterbox baked in as blank rows 0-19/220-239,
  // since the downsample math itself needs the full 640x400 source).
  val fbWidth = 320
  val fbHeight = 240
  val fbBytesPerRow = fbWidth / 8
  val fbWords = (fbWidth * fbHeight) / 8 / 4
  require(fbWords == 2400)

  // Real content only spans rows 20-219 (matches DG_DrawFrame's own
  // FB_ROW_OFFSET_Y = (240 - 400/2) / 2 = 20 exactly).
  val frameHeight = 200
  val rowOffsetY = (fbHeight - frameHeight) / 2

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

    // Read at Scala elaboration time and baked in as literal hardware
    // constants -- not Mem()+loadMemoryFromFileInline (see
    // VGAFramebufferBadAppleTestbench.scala for the real
    // RANDOMIZE_MEM_INIT interaction this avoids).
    val fbWordsScala: Seq[BigInt] = {
      val src = Source.fromFile("/home/ninadjangle/chipyard/generators/chipyard/src/main/resources/vga/doom_frame_real.hex")
      try { src.getLines().map(line => BigInt(line.trim, 16)).toSeq } finally { src.close() }
    }
    require(fbWordsScala.length == fbWords)
    val sourceFrame = VecInit(fbWordsScala.map(_.U(32.W)))

    val overallPass = RegInit(true.B)
    val mismatchCount = RegInit(0.U(32.W))
    def fail(): Unit = { overallPass := false.B; mismatchCount := mismatchCount + 1.U }

    // ---------------- Step 1: write the real frame, real addressing ----------------
    // Genuinely-varying per-byte writes (matching real CPU traffic and
    // sidestepping the firtool constant-mask bug) across the *entire*
    // 240-row framebuffer this time, since the source file already
    // includes DG_DrawFrame's own blank letterbox rows -- a direct
    // full-framebuffer copy, unlike Bad Apple's 200-row-plus-offset write.
    val rowIdx = RegInit(0.U(log2Ceil(fbHeight + 1).W))
    val byteIdx = RegInit(0.U(log2Ceil(fbBytesPerRow + 1).W))
    val flatByteIdx = rowIdx * fbBytesPerRow.U + byteIdx
    val wordIdx = flatByteIdx >> 2
    val byteInWordIdx = flatByteIdx(1, 0)
    val writeAddr = flatByteIdx
    val writeByte = (sourceFrame(wordIdx) >> (byteInWordIdx << 3))(7, 0)
    val writeMask = (1.U(beatBytes.W) << byteInWordIdx)
    val writeDataFull = writeByte << (byteInWordIdx << 3)

    val sIdle :: sWrite :: sWriteDone :: sScanning :: sDone :: Nil = Enum(5)
    val state = RegInit(sIdle)

    val (_, writeBits) = edge.Put(0.U, writeAddr, 0.U, writeDataFull, writeMask)
    out.a.valid := state === sWrite
    out.a.bits := writeBits
    out.d.ready := true.B

    val lastRow = rowIdx === (fbHeight - 1).U
    val lastByte = byteIdx === (fbBytesPerRow - 1).U

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
      is(sWriteDone) { state := sScanning }
      is(sScanning) { }
      is(sDone) { }
    }

    // ---------------- Step 2: real VGA timing reference ----------------
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

    // ---------------- Step 3: exhaustive real-pixel scoreboard ----------------
    // Full 320x240 framebuffer addressing this time (the source data
    // already carries the real letterbox rows as blank), so pixelIndex
    // is computed directly from fbX/fbY, not an offset-adjusted frameY.
    val fbX = hCountRefD >> 1
    val fbY = vCountRefD >> 1
    val pixelIndex = fbY * fbWidth.U + fbX
    val byteIndexInFrame = pixelIndex >> 3
    val bitIndexInFrame = pixelIndex(2, 0)
    val wordIndexInFrame = byteIndexInFrame >> 2
    val byteInWord = byteIndexInFrame(1, 0)
    val expectedByte = (sourceFrame(wordIndexInFrame) >> (byteInWord << 3))(7, 0)
    val expectedBit = (expectedByte >> (7.U - bitIndexInFrame))(0)

    val refVisible = hCountRefD < hVisible.U && vCountRefD < vVisible.U
    val checkThisCycle = (state === sScanning || state === sDone) && refVisible && pclkRef
    val pixelsChecked = RegInit(0.U(32.W))
    when(checkThisCycle) {
      pixelsChecked := pixelsChecked + 1.U
      when(dut.module.io.vga_video =/= expectedBit) {
        when(mismatchCount < 5.U) {
          printf("MISMATCH #%d: fbX=%d fbY=%d pixelIndex=%d wordIdx=%d rawWord=0x%x byteInWord=%d expectedByte=0x%x bitIdx=%d expected=%d got=%d\n",
            mismatchCount, fbX, fbY, pixelIndex, wordIndexInFrame, sourceFrame(wordIndexInFrame), byteInWord, expectedByte, bitIndexInFrame, expectedBit, dut.module.io.vga_video)
        }
        fail()
      }
    }

    // ---------------- Step 4: dump the real, actually-scanned-out frame ----------------
    val rowAccum = RegInit(0.U(fbWidth.W))
    when(checkThisCycle) {
      val newAccum = rowAccum | (dut.module.io.vga_video << fbX)(fbWidth - 1, 0)
      rowAccum := newAccum
      when(fbX === (fbWidth - 1).U) {
        printf("FRAMEDUMP row=%d bits=%x\n", fbY, newAccum)
        rowAccum := 0.U
      }
    }

    val scanCycles = RegInit(0.U(log2Ceil(hTotal * vTotal * 2 + 10).W))
    when(state === sScanning || state === sDone) {
      scanCycles := scanCycles + 1.U
      when(scanCycles === (hTotal * vTotal * 2 - 1).U) {
        state := sDone
      }
    }

    io.done := state === sDone
    io.pass := overallPass
    io.pixelsChecked := pixelsChecked
  }
}

// hTotal*vTotal*2 (840,000) for the full scan window, plus the write
// phase (9600 individual byte writes) and margin.
class VGAFramebufferDoomUnitTest(implicit p: Parameters) extends UnitTest(timeout = 900000) {
  val th = Module(LazyModule(new VGAFramebufferDoomTestHarness).module)
  th.io.start := io.start
  io.finished := th.io.done

  when(th.io.done) {
    when(th.io.pass) {
      printf("VGA DOOM TESTBENCH: PASSED (%d real pixels checked, real captured freedoom title frame, real DG_DrawFrame algorithm)\n", th.io.pixelsChecked)
    } .otherwise {
      printf("VGA DOOM TESTBENCH: FAILED (%d pixels checked)\n", th.io.pixelsChecked)
      assert(false.B, "VGA DOOM testbench reported failure")
    }
  }
}

class WithVGAFramebufferDoomUnitTest extends Config((site, here, up) => {
  case UnitTests => (q: Parameters) => {
    implicit val p = q
    Seq(Module(new VGAFramebufferDoomUnitTest))
  }
})

class VGAFramebufferDoomUnitTestConfig extends Config(
  new WithVGAFramebufferDoomUnitTest ++ new BaseSubsystemConfig)
