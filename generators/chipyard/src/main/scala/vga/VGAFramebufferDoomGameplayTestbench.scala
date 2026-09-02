// Real DOOM *gameplay* verification for TLVGAFramebuffer: the earlier
// VGAFramebufferDoomTestbench.scala verified one static frame (the
// Freedoom title screen) written once. Real gameplay is a continuous
// loop of writes -- a new frame overwriting the previous one, every
// tic -- so this testbench verifies that update behavior specifically:
// write real gameplay frame A, exhaustively verify it scans out
// correctly, then write real gameplay frame B *over* it, and exhaustively
// verify the framebuffer now shows B everywhere, with no stale bits left
// over from A. That's a real correctness property a single-frame test
// cannot exercise: every real second of real play depends on this
// overwrite behavior working, tic after tic.
//
// Both frames are real captures: connected to a live doomgeneric engine
// (the netstream backend, run against the real freedoom1.wad) with a
// small scripted client that pressed real movement keys (up-arrow,
// right-arrow) between two frame captures -- real player movement in a
// real level, not two arbitrary bit patterns. Processed through the
// exact DG_DrawFrame algorithm in Python (see doom_gameplay_frame_a.hex /
// _b.hex's generation), same as the single-frame DOOM testbench.

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

class VGAFramebufferDoomGameplayTestHarness(implicit p: Parameters) extends LazyModule {
  val beatBytes = 4
  val testAddress = 0x0
  val dut = LazyModule(new TLVGAFramebuffer(VGAFramebufferParams(address = testAddress), beatBytes))
  val driver = TLClientNode(Seq(TLMasterPortParameters.v1(Seq(TLMasterParameters.v1(
    name = "vga-doom-gameplay-test-driver",
    sourceId = IdRange(0, 4))))))
  dut.node := driver

  val clockSource = ClockSourceNode(Seq(ClockSourceParameters()))
  dut.clockNode := clockSource

  val fbWidth = 320
  val fbHeight = 240
  val fbBytesPerRow = fbWidth / 8
  val fbWords = (fbWidth * fbHeight) / 8 / 4
  require(fbWords == 2400)

  def loadFrame(name: String): Seq[BigInt] = {
    val src = Source.fromFile(s"/home/ninadjangle/chipyard/generators/chipyard/src/main/resources/vga/$name")
    try { src.getLines().map(line => BigInt(line.trim, 16)).toSeq } finally { src.close() }
  }

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

    val frameAWords = loadFrame("doom_gameplay_frame_a.hex")
    val frameBWords = loadFrame("doom_gameplay_frame_b.hex")
    require(frameAWords.length == fbWords && frameBWords.length == fbWords)
    val frameA = VecInit(frameAWords.map(_.U(32.W)))
    val frameB = VecInit(frameBWords.map(_.U(32.W)))

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

    // ---------------- Two-phase write+verify: A, then overwrite with B ----------------
    // Phase 0 writes+checks frame A; phase 1 writes+checks frame B *over*
    // the same addresses, on the same never-reset device -- the real
    // gameplay-relevant property (does a new frame's write correctly
    // replace the old one, not leave any stale bit behind).
    val sIdle :: sWrite :: sWriteDone :: sScanning :: sScanDone :: sDone :: Nil = Enum(6)
    val state = RegInit(sIdle)
    val phase = RegInit(0.U(1.W)) // 0 = frame A, 1 = frame B
    val curFrame = Mux(phase === 0.U, frameA, frameB)

    val rowIdx = RegInit(0.U(log2Ceil(fbHeight + 1).W))
    val byteIdx = RegInit(0.U(log2Ceil(fbBytesPerRow + 1).W))
    val flatByteIdx = rowIdx * fbBytesPerRow.U + byteIdx
    val wordIdx = flatByteIdx >> 2
    val byteInWordIdx = flatByteIdx(1, 0)
    val writeByte = (curFrame(wordIdx) >> (byteInWordIdx << 3))(7, 0)
    val writeMask = (1.U(beatBytes.W) << byteInWordIdx)
    val writeDataFull = writeByte << (byteInWordIdx << 3)

    val (_, writeBits) = edge.Put(0.U, flatByteIdx, 0.U, writeDataFull, writeMask)
    out.a.valid := state === sWrite
    out.a.bits := writeBits
    out.d.ready := true.B

    val lastRow = rowIdx === (fbHeight - 1).U
    val lastByte = byteIdx === (fbBytesPerRow - 1).U

    val pixelIndex = fbY * fbWidth.U + fbX
    val byteIndexInFrame = pixelIndex >> 3
    val bitIndexInFrame = pixelIndex(2, 0)
    val wordIndexInFrame = byteIndexInFrame >> 2
    val byteInWord = byteIndexInFrame(1, 0)
    val expectedByte = (curFrame(wordIndexInFrame) >> (byteInWord << 3))(7, 0)
    val expectedBit = (expectedByte >> (7.U - bitIndexInFrame))(0)

    val checkThisCycle = (state === sScanning || state === sScanDone) && refVisible && pclkRef
    val pixelsChecked = RegInit(0.U(32.W))
    when(checkThisCycle) {
      pixelsChecked := pixelsChecked + 1.U
      when(dut.module.io.vga_video =/= expectedBit) {
        when(mismatchCount < 5.U) {
          printf("MISMATCH phase=%d #%d: fbX=%d fbY=%d expected=%d got=%d\n",
            phase, mismatchCount, fbX, fbY, expectedBit, dut.module.io.vga_video)
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
      is(sScanDone) {
        when(phase === 0.U) {
          // Move to phase 1: overwrite with frame B, starting a fresh
          // write pass over the exact same addresses.
          phase := 1.U
          rowIdx := 0.U
          byteIdx := 0.U
          state := sWrite
        } .otherwise {
          state := sDone
        }
      }
      is(sDone) { }
    }

    io.done := state === sDone
    io.pass := overallPass
    io.pixelsChecked := pixelsChecked
  }
}

// Two full write+scan passes: 2*(hTotal*vTotal*2) plus two write phases
// (~9600 byte writes each) and margin.
class VGAFramebufferDoomGameplayUnitTest(implicit p: Parameters) extends UnitTest(timeout = 1750000) {
  val th = Module(LazyModule(new VGAFramebufferDoomGameplayTestHarness).module)
  th.io.start := io.start
  io.finished := th.io.done

  when(th.io.done) {
    when(th.io.pass) {
      printf("VGA DOOM GAMEPLAY TESTBENCH: PASSED (%d real pixels checked across 2 real gameplay frames, real player movement between them)\n", th.io.pixelsChecked)
    } .otherwise {
      printf("VGA DOOM GAMEPLAY TESTBENCH: FAILED (%d pixels checked)\n", th.io.pixelsChecked)
      assert(false.B, "VGA DOOM gameplay testbench reported failure")
    }
  }
}

class WithVGAFramebufferDoomGameplayUnitTest extends Config((site, here, up) => {
  case UnitTests => (q: Parameters) => {
    implicit val p = q
    Seq(Module(new VGAFramebufferDoomGameplayUnitTest))
  }
})

class VGAFramebufferDoomGameplayUnitTestConfig extends Config(
  new WithVGAFramebufferDoomGameplayUnitTest ++ new BaseSubsystemConfig)
