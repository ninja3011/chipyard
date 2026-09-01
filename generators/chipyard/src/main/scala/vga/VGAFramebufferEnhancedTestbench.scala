// Enhanced standalone testbench for TLVGAFramebuffer, same UnitTest
// framework as VGAFramebufferTestbench.scala (kept alongside it,
// unmodified -- this one supersedes it in coverage, not in mechanism).
//
// The original testbench only ever wrote address 0x0 through a synthetic
// TileLink driver, which is exactly why it never caught the real
// address-decode bug found during the 2026-09-01 overnight session (see
// ARTY-VGA-DOOM-FUNCVERIF-OVERNIGHT-PLAN.md): AddressSet(base, fbBytes-1)
// used a non-contiguous mask (9599 = 0b10010101111111), fragmenting the
// framebuffer's real address range into seven disjoint 128-byte islands
// instead of one contiguous 16KB region. This testbench closes that gap
// *in fast isolated-module simulation* (no full-SoC boot needed) by
// deliberately writing to addresses that fell in the gaps between those
// old islands, so if the fix ever regresses, this test starts failing in
// seconds rather than requiring a full CPU boot to notice.
//
// Checks, in order:
//   1. Address-decode regression sweep: byte and word (PutPartial /
//      PutFull) write-then-read-back across ~10 addresses spanning the
//      full intended 16KB decode window, including several addresses
//      that were unreachable under the pre-fix mask.
//   2. Adjacent-byte aliasing: two different byte lanes of the same
//      beat, plus a final re-read of the very first address after every
//      other write has happened, confirming no write ever silently
//      clobbers a neighboring byte.
//   3. VGA timing correctness: same mechanism as the original testbench
//      (a mirrored reference hsync/vsync counter, checked via assert()).
//   4. Multi-row pixel-path correctness: five rows spanning the full
//      0-239 fbHeight range (not just row 0) are sampled during a single
//      real frame scan and checked against the real written bytes.

package chipyard.vga

import chisel3._
import chisel3.util._
import org.chipsalliance.cde.config.{Parameters, Config}
import freechips.rocketchip.diplomacy._
import freechips.rocketchip.tilelink._
import freechips.rocketchip.prci._
import freechips.rocketchip.subsystem.BaseSubsystemConfig
import freechips.rocketchip.unittest._

// One address-decode test case, fully resolved at Scala elaboration time
// so the hardware side is just literal comparisons -- no runtime byte-lane
// shifting logic to get subtly wrong.
case class VGATestEntry(label: String, address: Int, lgSize: Int, value: BigInt, beatBytes: Int) {
  val sizeBytes = 1 << lgSize
  val byteLane = address % beatBytes
  require(byteLane % sizeBytes == 0, s"$label: address $address not aligned to size $sizeBytes")
  val mask: Int = (((BigInt(1) << sizeBytes) - 1) << byteLane).toInt
  val data: BigInt = (value & ((BigInt(1) << (sizeBytes * 8)) - 1)) << (byteLane * 8)
}

class VGAFramebufferEnhancedTestHarness(implicit p: Parameters) extends LazyModule {
  val beatBytes = 4
  val testAddress = 0x0
  val dut = LazyModule(new TLVGAFramebuffer(VGAFramebufferParams(address = testAddress), beatBytes))
  val driver = TLClientNode(Seq(TLMasterPortParameters.v1(Seq(TLMasterParameters.v1(
    name = "vga-enhanced-test-driver",
    sourceId = IdRange(0, 4))))))
  dut.node := driver

  val clockSource = ClockSourceNode(Seq(ClockSourceParameters()))
  dut.clockNode := clockSource

  // fbWidth=320 -> 40 bytes/row (1bpp). Rows span the real 0-239 fbHeight
  // range, not just row 0 -- the original testbench only ever checked
  // row 0, so it could not have caught a bug confined to the VGA
  // scanner's row/column addressing math either.
  val fbBytesPerRow = 40
  case class RowCheck(row: Int, byteVal: BigInt) { val addr = row * fbBytesPerRow }
  val rowChecks = Seq(
    RowCheck(0,   BigInt("A5", 16)), // 1010_0101 -> MSB 1
    RowCheck(1,   BigInt("66", 16)), // 0110_0110 -> MSB 0
    RowCheck(60,  BigInt("99", 16)), // 1001_1001 -> MSB 1
    RowCheck(150, BigInt("0F", 16)), // 0000_1111 -> MSB 0
    RowCheck(239, BigInt("C3", 16))  // 1100_0011 -> MSB 1
  )

  // Address-decode regression sweep. Entries marked "BROKEN PRE-FIX" sit
  // in the gaps between the seven 128-byte islands the old buggy mask
  // produced (0x000-0x07f, 0x100-0x17f, 0x400-0x47f, 0x500-0x57f,
  // 0x2000-0x207f, 0x2400-0x247f, 0x2500-0x257f were the only reachable
  // ranges before the fix) -- confirmed by re-deriving that mask's actual
  // bit pattern, not guessed.
  val entries: Seq[VGATestEntry] = Seq(
    VGATestEntry("origin (row 0 byte 0)",              rowChecks(0).addr, 0, rowChecks(0).byteVal, beatBytes),
    VGATestEntry("adjacent byte, same beat",            0x0001, 0, BigInt("5A", 16), beatBytes),
    VGATestEntry("last byte of old island 1",           0x007F, 0, BigInt("3C", 16), beatBytes),
    VGATestEntry("just past old island 1 -- BROKEN PRE-FIX", 0x0080, 0, BigInt("F0", 16), beatBytes),
    VGATestEntry("just past old island 2 -- BROKEN PRE-FIX", 0x0180, 0, BigInt("0D", 16), beatBytes),
    VGATestEntry("row 1 byte 0",                        rowChecks(1).addr, 0, rowChecks(1).byteVal, beatBytes),
    VGATestEntry("well beyond every old island -- BROKEN PRE-FIX", 0x1000, 0, BigInt("C3", 16), beatBytes),
    VGATestEntry("row 60 byte 0",                       rowChecks(2).addr, 0, rowChecks(2).byteVal, beatBytes),
    VGATestEntry("just past old island near 0x2000 -- BROKEN PRE-FIX", 0x2080, 0, BigInt("55", 16), beatBytes),
    VGATestEntry("row 150 byte 0",                      rowChecks(3).addr, 0, rowChecks(3).byteVal, beatBytes),
    VGATestEntry("row 239 byte 0 (near real fbBytes edge)", rowChecks(4).addr, 0, rowChecks(4).byteVal, beatBytes),
    VGATestEntry("word write near top of 16KB decode window (PutFull)", 0x3FFC, 2, BigInt("DEADBEEF", 16), beatBytes),
    VGATestEntry("word write mid-range (PutFull)",      0x2000, 2, BigInt("CAFEF00D", 16), beatBytes)
  )
  val numEntries = entries.length

  lazy val module = new Impl
  class Impl extends LazyModuleImp(this) {
    val io = IO(new Bundle {
      val start = Input(Bool())
      val done = Output(Bool())
      val pass = Output(Bool())
    })

    clockSource.out(0)._1.clock := clock
    clockSource.out(0)._1.reset := reset

    val (out, edge) = driver.out(0)

    val overallPass = RegInit(true.B)
    def fail(msg: String): Unit = { overallPass := false.B; printf(msg) }

    // ---------------- Step 1+2: address-decode sweep + aliasing ----------------
    val addrVec = VecInit(entries.map(_.address.U(32.W)))
    val lgSizeVec = VecInit(entries.map(_.lgSize.U(log2Ceil(beatBytes + 1).W)))
    val dataVec = VecInit(entries.map(e => e.data.U(32.W)))
    val maskVec = VecInit(entries.map(e => e.mask.U(beatBytes.W)))
    val maskBitsVec = VecInit(entries.map(e => FillInterleaved(8, e.mask.U(beatBytes.W))))

    // idx runs 0 until numEntries (write+read each), then one extra pass
    // at idx==0 to re-check the very first address after everything else
    // has been written -- catches a write anywhere in the sweep silently
    // clobbering the origin byte, not just its immediate neighbor.
    val sIdle :: sWrite :: sRead :: sRecheckOrigin :: sTLDone :: Nil = Enum(5)
    val tlState = RegInit(sIdle)
    val idx = RegInit(0.U(log2Ceil(numEntries + 1).W))

    val curAddr = Mux(tlState === sRecheckOrigin, addrVec(0), addrVec(idx))
    val curLgSize = Mux(tlState === sRecheckOrigin, lgSizeVec(0), lgSizeVec(idx))
    val curData = dataVec(idx)
    val curMaskBits = Mux(tlState === sRecheckOrigin, maskBitsVec(0), maskBitsVec(idx))
    val curExpected = Mux(tlState === sRecheckOrigin, dataVec(0), dataVec(idx))

    val (_, writeBits) = edge.Put(0.U, curAddr, curLgSize, curData, maskVec(idx))
    val (_, readBits) = edge.Get(0.U, curAddr, curLgSize)

    out.a.valid := tlState === sWrite || tlState === sRead || tlState === sRecheckOrigin
    out.a.bits := Mux(tlState === sWrite, writeBits, readBits)
    out.d.ready := true.B

    switch(tlState) {
      is(sIdle) { when(io.start) { tlState := sWrite } }
      is(sWrite) { when(out.a.fire) { tlState := sRead } }
      is(sRead) {
        when(out.a.fire) {
          when((out.d.bits.data & curMaskBits) =/= curExpected) {
            fail("FAIL: address-decode sweep mismatch at entry\n")
          }
          when(idx === (numEntries - 1).U) {
            idx := 0.U
            tlState := sRecheckOrigin
          } .otherwise {
            idx := idx + 1.U
            tlState := sWrite
          }
        }
      }
      is(sRecheckOrigin) {
        when(out.a.fire) {
          when((out.d.bits.data & curMaskBits) =/= curExpected) {
            fail("FAIL: origin byte was clobbered by a later write (aliasing bug)\n")
          }
          tlState := sTLDone
        }
      }
      is(sTLDone) { }
    }

    // ---------------- Step 3: VGA timing correctness ----------------
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

    val expectedHsyncLow = hCountRef >= (hVisible + hFrontPorch).U && hCountRef < (hVisible + hFrontPorch + hSyncWidth).U
    val expectedVsyncLow = vCountRef >= (vVisible + vFrontPorch).U && vCountRef < (vVisible + vFrontPorch + vSyncWidth).U
    val expectedHsyncLow_d = RegNext(expectedHsyncLow, false.B)
    val expectedVsyncLow_d = RegNext(expectedVsyncLow, false.B)

    val timingChecksStarted = RegInit(false.B)
    when(tlState === sTLDone) { timingChecksStarted := true.B }

    when(timingChecksStarted) {
      assert(dut.module.io.vga_hsync === !expectedHsyncLow_d,
        "VGA hsync did not match expected timing window")
      assert(dut.module.io.vga_vsync === !expectedVsyncLow_d,
        "VGA vsync did not match expected timing window")
    }

    // ---------------- Step 4: multi-row pixel-path correctness ----------------
    // All five rows are sampled during ONE real frame scan (vCountRef only
    // increases monotonically within a frame), not five separate
    // full-frame waits -- rowVCounts is sorted ascending by row so this
    // works without needing to track "have we wrapped around yet."
    val rowVCounts = VecInit(rowChecks.map(rc => (2 * rc.row).U(log2Ceil(vTotal).W)))
    val rowExpectedMsb = VecInit(rowChecks.map(rc => ((rc.byteVal >> 7) & 1).U(1.W).asBool))
    val numRows = rowChecks.length

    val rowCheckIdx = RegInit(0.U(log2Ceil(numRows + 1).W))
    val rowChecksDone = rowCheckIdx === numRows.U
    val atTargetRow = timingChecksStarted && !rowChecksDone &&
      hCountRef < 2.U && vCountRef === rowVCounts(rowCheckIdx) && pclkRef

    when(atTargetRow) {
      val expected = rowExpectedMsb(rowCheckIdx)
      when(dut.module.io.vga_video =/= expected) {
        fail("FAIL: row pixel(0, row) mismatch\n")
      }
      rowCheckIdx := rowCheckIdx + 1.U
    }

    // ---------------- Done / success ----------------
    val done = tlState === sTLDone && rowChecksDone
    io.done := done
    io.pass := overallPass
  }
}

// Timeout needs to cover up to TWO full VGA frame periods (840,000 cycles
// each), not one: the address-decode sweep (~30-40 cycles) delays
// timingChecksStarted just long enough that hCountRef has already passed
// 2 by the time checks are enabled, so row 0's target window (vCount=0)
// is missed on the very first lap and only caught on the *second* --
// confirmed empirically via a cycle-counted heartbeat printf rather than
// assumed, after the original 920,000 budget (one lap plus margin) timed
// out with row checks 0-1 only completing partway through the second
// lap. Row 239 (near the end of a lap) then needs almost all of that
// second lap too, so the real worst case is just under 2*840,000.
class VGAFramebufferEnhancedUnitTest(implicit p: Parameters) extends UnitTest(timeout = 1750000) {
  val th = Module(LazyModule(new VGAFramebufferEnhancedTestHarness).module)
  th.io.start := io.start
  io.finished := th.io.done

  when(th.io.done) {
    when(th.io.pass) {
      printf("VGA ENHANCED TESTBENCH: PASSED (address-decode sweep OK, aliasing OK, VGA timing OK, 5-row pixel-path OK)\n")
    } .otherwise {
      printf("VGA ENHANCED TESTBENCH: FAILED\n")
      assert(false.B, "VGA enhanced testbench reported failure")
    }
  }
}

class WithVGAFramebufferEnhancedUnitTest extends Config((site, here, up) => {
  case UnitTests => (q: Parameters) => {
    implicit val p = q
    Seq(Module(new VGAFramebufferEnhancedUnitTest))
  }
})

class VGAFramebufferEnhancedUnitTestConfig extends Config(
  new WithVGAFramebufferEnhancedUnitTest ++ new BaseSubsystemConfig)
