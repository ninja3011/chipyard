package chipyard.fpga.arty100t

import chisel3._
import chisel3.util._
import freechips.rocketchip.diplomacy._
import org.chipsalliance.cde.config.{Parameters}
import freechips.rocketchip.tilelink._
import freechips.rocketchip.prci._
import freechips.rocketchip.subsystem.{SystemBusKey}

import sifive.fpgashells.shell.xilinx._
import sifive.fpgashells.shell._
import sifive.fpgashells.clocks._
import sifive.fpgashells.ip.xilinx.{IBUF, PowerOnResetFPGAOnly}

import sifive.blocks.devices.uart._

import chipyard._
import chipyard.harness._

class Arty100THarness(override implicit val p: Parameters) extends Arty100TShell {
  def dp = designParameters

  val clockOverlay = dp(ClockInputOverlayKey).map(_.place(ClockInputDesignInput())).head
  val harnessSysPLL = dp(PLLFactoryKey)
  val harnessSysPLLNode = harnessSysPLL()
  val dutFreqMHz = (dp(SystemBusKey).dtsFrequency.get / (1000 * 1000)).toInt
  val dutClock = ClockSinkNode(freqMHz = dutFreqMHz)
  println(s"Arty100T FPGA Base Clock Freq: ${dutFreqMHz} MHz")
  val dutWrangler = LazyModule(new ResetWrangler())
  val dutGroup = ClockGroup()
  dutClock := dutWrangler.node := dutGroup := harnessSysPLLNode

  harnessSysPLLNode := clockOverlay.overlayOutput.node

  val ddrOverlay = dp(DDROverlayKey).head.place(DDRDesignInput(dp(ExtTLMem).get.master.base, dutWrangler.node, harnessSysPLLNode)).asInstanceOf[DDRArtyPlacedOverlay]
  val ddrClient = TLClientNode(Seq(TLMasterPortParameters.v1(Seq(TLMasterParameters.v1(
    name = "chip_ddr",
    sourceId = IdRange(0, 1 << dp(ExtTLMem).get.master.idBits)
  )))))
  val ddrBlockDuringReset = LazyModule(new TLBlockDuringReset(4))
  ddrOverlay.overlayOutput.ddr := ddrBlockDuringReset.node := ddrClient

  val ledOverlays = dp(LEDOverlayKey).map(_.place(LEDDesignInput()))
  val all_leds = ledOverlays.map(_.overlayOutput.led)
  val status_leds = all_leds.take(3)
  val other_leds = all_leds.drop(3)

  override lazy val module = new HarnessLikeImpl

  class HarnessLikeImpl extends Impl with HasHarnessInstantiators {
    all_leds.foreach(_ := DontCare)
    clockOverlay.overlayOutput.node.out(0)._1.reset := ~resetPin

    // Cross-HarnessBinder status channel: WithArty100TDMI (matched on
    // DMIPort) instantiates the Arty100TDmiAutoloader and writes its
    // done/step status here; WithArty100TILA (matched on TracePort, a
    // completely separate binder invocation) reads it back to pack into
    // the existing ILA probe bundle. Both binders' `th: HasHarnessInstantiators`
    // parameter IS already this very module instance (HasHarnessInstantiators
    // is mixed into HarnessLikeImpl itself) -- they reach these fields via
    // `th.asInstanceOf[Arty100THarness#HarnessLikeImpl]`, a type-projected
    // cast, not through the outer Arty100THarness/`ath` reference (a plain
    // val here is a member of this inner module class, not of the outer
    // LazyModule, so `ath.autoloaderDone` doesn't resolve -- tried and
    // failed to compile). Also tried routing through InModuleBody at the
    // outer LazyModule level instead, which compiles but returns
    // ModuleValue[Bool]/ModuleValue[UInt], not Bool/UInt directly, so `:=`
    // and Cat() don't accept it either without an unwrap this call site
    // doesn't have easy access to. Plain vals here, reached via the
    // type-projected cast, sidestep both problems.
    val autoloaderDone = WireDefault(false.B)
    val autoloaderStep = WireDefault(0.U(4.W))
    val autoloaderTriggered = WireDefault(false.B)
    val autoloaderReqValid = WireDefault(false.B)
    val autoloaderReqReady = WireDefault(false.B)
    // Written by WithClintDebugTap (matched on ClintDebugPort), read by
    // WithArty100TILA -- CLINT's own internal msip register for hart 0,
    // sampled live, with no TileLink round-trip. See ClintDebugPort in
    // chipyard's iobinders/Ports.scala for the full rationale.
    val clintIpi0Debug = WireDefault(false.B)

    val clk_100mhz = clockOverlay.overlayOutput.node.out.head._1.clock

    // Blink the status LEDs for sanity
    withClockAndReset(clk_100mhz, dutClock.in.head._1.reset) {
      val period = (BigInt(100) << 20) / status_leds.size
      val counter = RegInit(0.U(log2Ceil(period).W))
      val on = RegInit(0.U(log2Ceil(status_leds.size).W))
      status_leds.zipWithIndex.map { case (o,s) => o := on === s.U }
      counter := Mux(counter === (period-1).U, 0.U, counter + 1.U)
      when (counter === 0.U) {
        on := Mux(on === (status_leds.size-1).U, 0.U, on + 1.U)
      }
    }

    other_leds(0) := resetPin

    harnessSysPLL.plls.foreach(_._1.getReset.get := pllReset)

    def referenceClockFreqMHz = dutFreqMHz
    def referenceClock = dutClock.in.head._1.clock
    def referenceReset = dutClock.in.head._1.reset
    def success = { require(false, "Unused"); false.B }

    childClock := harnessBinderClock
    childReset := harnessBinderReset

    ddrOverlay.mig.module.clock := harnessBinderClock
    ddrOverlay.mig.module.reset := harnessBinderReset
    ddrBlockDuringReset.module.clock := harnessBinderClock
    ddrBlockDuringReset.module.reset := harnessBinderReset.asBool || !ddrOverlay.mig.module.io.port.init_calib_complete

    other_leds(6) := ddrOverlay.mig.module.io.port.init_calib_complete

    instantiateChipTops()
  }
}
