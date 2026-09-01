// Real-data testbench for TLVGAFramebuffer: loads an actual frame from
// the real Bad Apple asset that will be programmed onto the Arty board
// (software/video/bad-apple.vidf), writes it into the framebuffer through
// real TileLink transactions using the exact same addressing the real
// bare-metal player (badapple_arty100t.c) uses, then exhaustively checks
// every one of the 64,000 pixels in that real frame against the real VGA
// scan-out output during one full real frame period.
//
// This is deliberately not synthetic test data -- the whole point is to
// answer "will the actual content that ships to the board display
// correctly", not "does an arbitrary bit pattern round-trip correctly".
// The address-decode bug found earlier tonight was in the *device*; this
// testbench is aimed at the *content path* instead: does the real
// row-copy addressing the C player uses actually land the real pixels in
// the right place, exactly reproduced in hardware simulation.
//
// Frame source: frame index 100 of bad-apple.vidf (320x200, MSB-first
// packed 1bpp, picked because it's ~63% white pixels -- real varied
// content, not a near-blank frame that would make a broken test look
// like it passed). Extracted once via a small Python script into
// resources/vga/badapple_frame100.hex (2000 32-bit words, one per line,
// little-endian byte order matching the TileLink data bus), read directly
// at Scala elaboration time and baked in as literal hardware constants
// (see the real reason below, not a UVM/SystemVerilog construct either way).

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

class VGAFramebufferBadAppleTestHarness(implicit p: Parameters) extends LazyModule {
  val beatBytes = 4
  val testAddress = 0x0
  val dut = LazyModule(new TLVGAFramebuffer(VGAFramebufferParams(address = testAddress), beatBytes))
  val driver = TLClientNode(Seq(TLMasterPortParameters.v1(Seq(TLMasterParameters.v1(
    name = "vga-badapple-test-driver",
    sourceId = IdRange(0, 4))))))
  dut.node := driver

  val clockSource = ClockSourceNode(Seq(ClockSourceParameters()))
  dut.clockNode := clockSource

  // Real bad-apple.vidf geometry (confirmed from the file's own header,
  // not assumed): 320x200, MSB-first, 8000 bytes/frame = 2000 32-bit words.
  val frameWidth = 320
  val frameHeight = 200
  val frameWords = (frameWidth * frameHeight) / 8 / 4
  require(frameWords == 2000)

  // Same 20-row letterbox centering badapple_arty100t.c uses:
  // ROW_OFFSET_Y = (FB_HEIGHT - 200) / 2 = 20, on a 320x240 framebuffer.
  val fbWidth = 320
  val fbHeight = 240
  val fbBytesPerRow = fbWidth / 8
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

    // The real frame data, loaded from the real asset -- not synthetic.
    // Deliberately NOT a Mem()+loadMemoryFromFileInline: Verilator's
    // default +define+RANDOMIZE_MEM_INIT runs its randomize-every-Mem
    // loop *after* the generated $readmemh call in the same initial
    // block, silently clobbering the loaded content (confirmed by
    // dumping the raw word value and finding it matched neither the real
    // file nor any deterministic pattern). Disabling that define fixed
    // the load, but broke something else entirely: it caused firtool's
    // memory-macro-replacement pass to hard-wire the *real* DUT
    // framebuffer's masked-write port to a constant `4'h1` instead of
    // the real dynamic TileLink mask (confirmed by comparing this
    // testbench's generated Verilog against the real Arty100T FPGA
    // build's, where the equivalent port is correctly wired to
    // `_buffer_auto_out_a_bits_mask` -- so the real board build is not
    // affected, only this minimal testbench's specific topology is).
    // Avoiding the whole interaction by not using a simulation-loaded
    // Mem at all: the real frame data is read directly at Scala
    // elaboration time and baked in as literal hardware constants (a
    // plain combinational lookup, not a "Memory" FIRRTL considers for
    // randomization), letting default simulation flags be used everywhere.
    val frameWordsScala: Seq[BigInt] = {
      val src = Source.fromFile("/home/ninadjangle/chipyard/generators/chipyard/src/main/resources/vga/badapple_frame100.hex")
      try { src.getLines().map(line => BigInt(line.trim, 16)).toSeq } finally { src.close() }
    }
    require(frameWordsScala.length == frameWords)
    val sourceFrame = VecInit(frameWordsScala.map(_.U(32.W)))

    val overallPass = RegInit(true.B)
    val mismatchCount = RegInit(0.U(32.W))
    def fail(): Unit = { overallPass := false.B; mismatchCount := mismatchCount + 1.U }

    // ---------------- Step 1: write the real frame, real addressing ----------------
    // Mirrors badapple_arty100t.c's *actual* per-byte copy loop exactly
    // (`fb[fbY*FB_BYTES_PER_ROW+b] = rowBytes[b]`) -- one byte at a time,
    // not word writes. This isn't just fidelity to the real C code: a
    // constant-mask, always-full-word write pattern here was found to
    // trigger a real firtool `--repl-seq-mem` optimization bug (confirmed
    // by comparing this testbench's generated Verilog against the real
    // Arty100T FPGA build's -- see VGAFramebuffer.scala's write-port
    // comment and the plan doc for the full story). Real CPU traffic
    // naturally varies its mask/size per access, which never triggers
    // it; matching that here, with genuinely per-byte masks, sidesteps
    // the bug entirely rather than working around a symptom.
    val rowIdx = RegInit(0.U(log2Ceil(frameHeight + 1).W))
    val byteIdx = RegInit(0.U(log2Ceil(fbBytesPerRow + 1).W))
    val flatByteIdx = rowIdx * fbBytesPerRow.U + byteIdx
    val wordIdx = flatByteIdx >> 2
    val byteInWordIdx = flatByteIdx(1, 0)
    val writeAddr = (rowIdx + rowOffsetY.U) * fbBytesPerRow.U + byteIdx
    val writeByte = (sourceFrame(wordIdx) >> (byteInWordIdx << 3))(7, 0)
    val writeMask = (1.U(beatBytes.W) << byteInWordIdx)
    val writeDataFull = writeByte << (byteInWordIdx << 3)

    val sIdle :: sWrite :: sWriteDone :: sScanning :: sDone :: Nil = Enum(5)
    val state = RegInit(sIdle)

    val (_, writeBits) = edge.Put(0.U, writeAddr, 0.U, writeDataFull, writeMask)
    out.a.valid := state === sWrite
    out.a.bits := writeBits
    out.d.ready := true.B

    val lastRow = rowIdx === (frameHeight - 1).U
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
      is(sScanning) { } // advanced below once the scan window completes
      is(sDone) { }
    }

    // ---------------- Step 2: real VGA timing reference (same mechanism as before) ----------------
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

    // DUT registers hsync/vsync/video one cycle after computing them --
    // delay the reference by one cycle to compare like with like (same
    // pattern as the earlier testbenches).
    val hCountRefD = RegNext(hCountRef, 0.U)
    val vCountRefD = RegNext(vCountRef, 0.U)

    // ---------------- Step 3: exhaustive real-pixel scoreboard ----------------
    // Every cycle where the delayed reference scan position falls inside
    // the real 320x200 image (not the 20-row letterbox), compute the
    // expected bit directly from the real loaded frame data and compare
    // against the DUT's actual output -- all 64,000 real pixels get
    // checked over one full frame period, not a handful of samples.
    val fbX = hCountRefD >> 1
    val fbY = vCountRefD >> 1
    val inImageRows = fbY >= rowOffsetY.U && fbY < (rowOffsetY + frameHeight).U
    val frameY = fbY - rowOffsetY.U
    val pixelIndex = frameY * frameWidth.U + fbX
    val byteIndexInFrame = pixelIndex >> 3
    val bitIndexInFrame = pixelIndex(2, 0)
    val wordIndexInFrame = byteIndexInFrame >> 2
    val byteInWord = byteIndexInFrame(1, 0)
    val expectedByte = (sourceFrame(wordIndexInFrame) >> (byteInWord << 3))(7, 0)
    val expectedBit = (expectedByte >> (7.U - bitIndexInFrame))(0)

    // Must also gate on the real visible window (hCountRefD < hVisible,
    // vCountRefD < vVisible) -- during horizontal/vertical blanking, fbX
    // can run up to 399 (out of the real 0-319 frame width), which would
    // otherwise compute nonsense pixelIndex/wordIndexInFrame values (a
    // real bug caught by comparing raw Mem reads against hand-derived
    // expected values, not assumed away).
    val refVisible = hCountRefD < hVisible.U && vCountRefD < vVisible.U
    val checkThisCycle = (state === sScanning || state === sDone) && inImageRows && refVisible && pclkRef
    val pixelsChecked = RegInit(0.U(32.W))
    when(checkThisCycle) {
      pixelsChecked := pixelsChecked + 1.U
      when(dut.module.io.vga_video =/= expectedBit) {
        when(mismatchCount < 5.U) {
          printf("MISMATCH #%d: fbX=%d fbY=%d frameY=%d pixelIndex=%d wordIdx=%d rawWord=0x%x byteInWord=%d expectedByte=0x%x bitIdx=%d expected=%d got=%d\n",
            mismatchCount, fbX, fbY, frameY, pixelIndex, wordIndexInFrame, sourceFrame(wordIndexInFrame), byteInWord, expectedByte, bitIndexInFrame, expectedBit, dut.module.io.vga_video)
        }
        fail()
      }
    }

    // One full deterministic period (hTotal*vTotal*2 cycles) is guaranteed
    // to visit every (hCount, vCount) combination exactly once, regardless
    // of the phase we started counting at -- no need to resynchronize to
    // a specific early state the way the enhanced testbench's row-sample
    // check did.
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
// phase (8000 individual byte writes -- tens of thousands of cycles) and margin.
class VGAFramebufferBadAppleUnitTest(implicit p: Parameters) extends UnitTest(timeout = 900000) {
  val th = Module(LazyModule(new VGAFramebufferBadAppleTestHarness).module)
  th.io.start := io.start
  io.finished := th.io.done

  when(th.io.done) {
    when(th.io.pass) {
      printf("VGA BAD APPLE TESTBENCH: PASSED (%d real pixels checked, real frame 100 of bad-apple.vidf, real row addressing)\n", th.io.pixelsChecked)
    } .otherwise {
      printf("VGA BAD APPLE TESTBENCH: FAILED (%d pixels checked)\n", th.io.pixelsChecked)
      assert(false.B, "VGA Bad Apple testbench reported failure")
    }
  }
}

class WithVGAFramebufferBadAppleUnitTest extends Config((site, here, up) => {
  case UnitTests => (q: Parameters) => {
    implicit val p = q
    Seq(Module(new VGAFramebufferBadAppleUnitTest))
  }
})

class VGAFramebufferBadAppleUnitTestConfig extends Config(
  new WithVGAFramebufferBadAppleUnitTest ++ new BaseSubsystemConfig)
