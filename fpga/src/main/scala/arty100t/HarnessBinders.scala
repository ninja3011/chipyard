package chipyard.fpga.arty100t

import chisel3._
import chisel3.util.Cat

import freechips.rocketchip.jtag.{JTAGIO}
import freechips.rocketchip.subsystem.{PeripheryBusKey}
import freechips.rocketchip.tilelink.{TLBundle}
import freechips.rocketchip.diplomacy.{LazyRawModuleImp}
import org.chipsalliance.diplomacy.nodes.{HeterogeneousBag}
import sifive.blocks.devices.uart.{UARTPortIO, UARTParams}
import sifive.blocks.devices.jtag.{JTAGPins, JTAGPinsFromPort}
import sifive.blocks.devices.pinctrl.{BasePin}
import sifive.fpgashells.shell._
import sifive.fpgashells.ip.xilinx._
import sifive.fpgashells.shell.xilinx._
import sifive.fpgashells.clocks._
import chipyard._
import chipyard.harness._
import chipyard.iobinders._
import testchipip.serdes._

// Xilinx ILA, RTL-instantiated (the one path BASIC-tier Vivado licensing
// allows -- see ila_gen.vivado.tcl for the create_ip call and the real,
// confirmed license limit: max 5 probe PORTS, not 5 signals, so the 9
// signals of interest are packed into 5 ports via Cat()).
class Arty100TILA extends BlackBox {
  override def desiredName = "ila_0"
  val io = IO(new Bundle {
    val clk = Input(Clock())
    val probe0 = Input(UInt(1.W))  // insn valid
    val probe1 = Input(UInt(40.W)) // insn iaddr
    val probe2 = Input(UInt(32.W)) // insn insn (raw instruction word)
    val probe3 = Input(UInt(13.W)) // priv(3) ## exception(1) ## interrupt(1) ## cause[7:0](8)
    val probe4 = Input(UInt(41.W)) // tval[39:0](40) ## reset(1)
  })
}

// Clean-baseline ILA harness: this build has NO other probes, NO LED
// wiring, nothing else added -- purely to observe the boot-ROM WFI/wake
// boundary directly, on the exact same RTL lineage that worked reliably
// on real hardware (this file otherwise matches commit 69cbe863).
class WithArty100TILA extends HarnessBinder({
  case (th: HasHarnessInstantiators, port: TracePort, chipId: Int) => {
    // Type-projected cast to reach the autoloaderDone/autoloaderStep fields
    // declared on HarnessLikeImpl itself -- see the comment on those fields
    // in Harness.scala for why this, rather than the usual `ath` (outer
    // Arty100THarness) cast used elsewhere in this file, is needed here.
    val hli = th.asInstanceOf[Arty100THarness#HarnessLikeImpl]
    val trace = port.getIO()
    val hart0Trace = trace.traces(0)
    withClockAndReset(hart0Trace.clock, hart0Trace.reset.asAsyncReset) {
      val insn = hart0Trace.trace.insns(0)
      val ila = Module(new Arty100TILA)
      ila.io.clk := hart0Trace.clock
      ila.io.probe0 := insn.valid
      ila.io.probe1 := insn.iaddr
      ila.io.probe2 := insn.insn
      ila.io.probe3 := Cat(insn.priv, insn.exception, insn.interrupt, insn.cause(7, 0))
      // tval truncated from 40 to 31 bits to make room for the DMI
      // autoloader's diagnostic status plus a direct, no-TileLink-round-trip
      // read of CLINT's own internal msip register (see Harness.scala/
      // WithArty100TDMI and WithClintDebugTap below) without changing this
      // probe's total width -- no ila_gen.vivado.tcl / XDC changes needed.
      // tval isn't load-bearing for this diagnosis.
      ila.io.probe4 := Cat(insn.tval(30, 0), hli.clintIpi0Debug, hli.autoloaderDone, hli.autoloaderStep,
        hli.autoloaderTriggered, hli.autoloaderReqValid, hli.autoloaderReqReady, hart0Trace.reset)
    }
  }
})

class WithArty100TUARTTSI extends HarnessBinder({
  case (th: HasHarnessInstantiators, port: UARTTSIPort, chipId: Int) => {
    val ath = th.asInstanceOf[LazyRawModuleImp].wrapper.asInstanceOf[Arty100THarness]
    val harnessIO = IO(new UARTPortIO(port.io.uartParams)).suggestName("uart_tsi")
    harnessIO <> port.io.uart
    val packagePinsWithPackageIOs = Seq(
      ("A9" , IOPin(harnessIO.rxd)),
      ("D10", IOPin(harnessIO.txd)))
    packagePinsWithPackageIOs foreach { case (pin, io) => {
      ath.xdc.addPackagePin(io, pin)
      ath.xdc.addIOStandard(io, "LVCMOS33")
      ath.xdc.addIOB(io)
    } }

    ath.other_leds(1) := port.io.dropped
    ath.other_leds(9) := port.io.tsi2tl_state(0)
    ath.other_leds(10) := port.io.tsi2tl_state(1)
    ath.other_leds(11) := port.io.tsi2tl_state(2)
    ath.other_leds(12) := port.io.tsi2tl_state(3)
  }
})


class WithArty100TDDRTL extends HarnessBinder({
  case (th: HasHarnessInstantiators, port: TLMemPort, chipId: Int) => {
    val artyTh = th.asInstanceOf[LazyRawModuleImp].wrapper.asInstanceOf[Arty100THarness]
    val bundles = artyTh.ddrClient.out.map(_._1)
    val ddrClientBundle = Wire(new HeterogeneousBag(bundles.map(_.cloneType)))
    bundles.zip(ddrClientBundle).foreach { case (bundle, io) => bundle <> io }
    ddrClientBundle <> port.io
  }
})

// Uses PMOD JA/JB
class WithArty100TSerialTLToGPIO extends HarnessBinder({
  case (th: HasHarnessInstantiators, port: SerialTLPort, chipId: Int) => {
    val artyTh = th.asInstanceOf[LazyRawModuleImp].wrapper.asInstanceOf[Arty100THarness]
    val harnessIO = IO(chiselTypeOf(port.io)).suggestName("serial_tl")
    harnessIO <> port.io

    harnessIO match {
      case io: DecoupledPhitIO => {
        val clkIO = io match {
          case io: HasClockOut => IOPin(io.clock_out)
          case io: HasClockIn => IOPin(io.clock_in)
        }
        val packagePinsWithPackageIOs = Seq(
          ("G13", clkIO),
          ("B11", IOPin(io.out.valid)),
          ("A11", IOPin(io.out.ready)),
          ("D12", IOPin(io.in.valid)),
          ("D13", IOPin(io.in.ready)),
          ("B18", IOPin(io.out.bits.phit, 0)),
          ("A18", IOPin(io.out.bits.phit, 1)),
          ("K16", IOPin(io.out.bits.phit, 2)),
          ("E15", IOPin(io.out.bits.phit, 3)),
          ("E16", IOPin(io.in.bits.phit, 0)),
          ("D15", IOPin(io.in.bits.phit, 1)),
          ("C15", IOPin(io.in.bits.phit, 2)),
          ("J17", IOPin(io.in.bits.phit, 3))
        )
        packagePinsWithPackageIOs foreach { case (pin, io) => {
          artyTh.xdc.addPackagePin(io, pin)
          artyTh.xdc.addIOStandard(io, "LVCMOS33")
        }}

        // Don't add IOB to the clock, if its an input
        io match {
          case io: DecoupledInternalSyncPhitIO => packagePinsWithPackageIOs foreach { case (pin, io) => {
            artyTh.xdc.addIOB(io)
          }}
          case io: DecoupledExternalSyncPhitIO => packagePinsWithPackageIOs.drop(1).foreach { case (pin, io) => {
            artyTh.xdc.addIOB(io)
          }}
        }

        artyTh.sdc.addClock("ser_tl_clock", clkIO, 100)
        artyTh.sdc.addGroup(pins = Seq(clkIO))
        artyTh.xdc.clockDedicatedRouteFalse(clkIO)
      }
    }
  }
})

// Maps the UART device to the on-board USB-UART
class WithArty100TUART(rxdPin: String = "A9", txdPin: String = "D10") extends HarnessBinder({
  case (th: HasHarnessInstantiators, port: UARTPort, chipId: Int) => {
    val ath = th.asInstanceOf[LazyRawModuleImp].wrapper.asInstanceOf[Arty100THarness]
    val harnessIO = IO(chiselTypeOf(port.io)).suggestName("uart")
    harnessIO <> port.io
    val packagePinsWithPackageIOs = Seq(
      (rxdPin, IOPin(harnessIO.rxd)),
      (txdPin, IOPin(harnessIO.txd)))
    packagePinsWithPackageIOs foreach { case (pin, io) => {
      ath.xdc.addPackagePin(io, pin)
      ath.xdc.addIOStandard(io, "LVCMOS33")
      ath.xdc.addIOB(io)
    } }
  }
})

// Maps the UART device to PMOD JD pins 3/7
class WithArty100TPMODUART extends WithArty100TUART("G2", "F3")

// Maps the UART device to PMOD JA pins 3/4 (rxd=ja_2/A11, txd=ja_3/D12) --
// used as a fallback console link when JD's socket is suspected bad.
class WithArty100TJAUART extends WithArty100TUART("A11", "D12")

class WithArty100TJTAG extends HarnessBinder({
  case (th: HasHarnessInstantiators, port: JTAGPort, chipId: Int) => {
    val ath = th.asInstanceOf[LazyRawModuleImp].wrapper.asInstanceOf[Arty100THarness]
    val harnessIO = IO(new JTAGChipIO(false)).suggestName("jtag")
    harnessIO.TDO := port.io.TDO
    port.io.TCK := harnessIO.TCK
    port.io.TDI := harnessIO.TDI
    port.io.TMS := harnessIO.TMS
    port.io.reset.foreach(_ := th.referenceReset)

    ath.sdc.addClock("JTCK", IOPin(harnessIO.TCK), 10)
    ath.sdc.addGroup(clocks = Seq("JTCK"))
    ath.xdc.clockDedicatedRouteFalse(IOPin(harnessIO.TCK))
    val packagePinsWithPackageIOs = Seq(
      ("F4", IOPin(harnessIO.TCK)),
      ("D2", IOPin(harnessIO.TMS)),
      ("E2", IOPin(harnessIO.TDI)),
      ("D4", IOPin(harnessIO.TDO))
    )
    
    packagePinsWithPackageIOs foreach { case (pin, io) => {
      ath.xdc.addPackagePin(io, pin)
      ath.xdc.addIOStandard(io, "LVCMOS33")
      ath.xdc.addPullup(io)
    } }
  }
})

// Wires the on-chip Arty100TDmiAutoloader (see DmiAutoloader.scala) directly
// to the Debug Module's DMI port, in place of an external JTAG probe. See
// DmiAutoloader.scala for the full rationale: this replicates, entirely
// on-chip, the exact fix the Basys3 board's hand-written DmiAutoloader.v
// needed for what looks like the same underlying bug (CLINT/msip SBA writes
// silently dropped once the hart has left reset).
//
// 20 seconds at 50MHz gives ample real-world time, after programming the
// bitstream, to run uart_tsi's ELF-into-DRAM load (usbipd attach, chmod,
// load, usbipd detach) before the autoloader fires and takes the hart out
// of reset.
class WithArty100TDMI extends HarnessBinder({
  case (th: HasHarnessInstantiators, port: DMIPort, chipId: Int) => {
    // Type-projected cast to reach autoloaderDone/autoloaderStep on
    // HarnessLikeImpl itself -- see the field comment in Harness.scala.
    val hli = th.asInstanceOf[Arty100THarness#HarnessLikeImpl]
    // Real-hardware diagnosis (via the ILA-exposed `triggered` signal)
    // showed the autoloader's own trigger-delay counter never advances --
    // it's still at its RegInit value many minutes after programming,
    // meaning the clock/reset domain it was given is not a live, running
    // one on this harness. th.harnessBinderClock/harnessBinderReset are
    // the pattern WithSimDMI (chipyard's built-in DMI harness binder) uses
    // -- but that binder is simulation-only, never exercised on real FPGA
    // hardware in this harness. th.referenceClock/th.referenceReset are
    // the DUT's own actual clock/reset (same domain the CPU itself runs
    // on, per Harness.scala's `def referenceClock = dutClock.in.head._1.clock`)
    // -- proven live by every ILA capture all night showing real hart0
    // trace activity on it. Switching to that for both the autoloader
    // itself and the DMI port's clock/reset.
    val autoloader = withClockAndReset(th.referenceClock, th.referenceReset) {
      Module(new Arty100TDmiAutoloader(triggerDelayCycles = BigInt(50) * 1000 * 1000 * 20))
    }
    port.io.dmi.req.valid := autoloader.io.dmiReq.valid
    autoloader.io.dmiReq.ready := port.io.dmi.req.ready
    port.io.dmi.req.bits.addr := autoloader.io.dmiReq.bits.addr
    port.io.dmi.req.bits.data := autoloader.io.dmiReq.bits.data
    port.io.dmi.req.bits.op := autoloader.io.dmiReq.bits.op

    autoloader.io.dmiResp.valid := port.io.dmi.resp.valid
    port.io.dmi.resp.ready := autoloader.io.dmiResp.ready
    autoloader.io.dmiResp.bits.data := port.io.dmi.resp.bits.data
    autoloader.io.dmiResp.bits.resp := port.io.dmi.resp.bits.resp

    port.io.dmiClock := th.referenceClock
    port.io.dmiReset := th.referenceReset

    // See Harness.scala for why these are plain harness-level wires rather
    // than a directly-shared Module reference: WithArty100TILA (a separate
    // binder, matched on TracePort) reads these to fold the autoloader's
    // progress into the existing 5-probe ILA, so a failed wake attempt can
    // be diagnosed (never triggered vs. stuck retrying vs. completed-but-
    // still-didn't-work) without yet another rebuild cycle.
    hli.autoloaderDone := autoloader.io.done
    hli.autoloaderStep := autoloader.io.step
    hli.autoloaderTriggered := autoloader.io.triggeredOut
    hli.autoloaderReqValid := autoloader.io.reqValidOut
    hli.autoloaderReqReady := autoloader.io.reqReadyOut
  }
})

// Consumes chipyard.iobinders.ClintDebugPort (see Ports.scala/IOBinders.scala
// for what it is and why it exists) and forwards it into the harness-level
// wire WithArty100TILA reads, the same cross-binder pattern used for the
// autoloader's own status above.
class WithClintDebugTap extends HarnessBinder({
  case (th: HasHarnessInstantiators, port: chipyard.iobinders.ClintDebugPort, chipId: Int) => {
    val hli = th.asInstanceOf[Arty100THarness#HarnessLikeImpl]
    hli.clintIpi0Debug := port.io
  }
})
