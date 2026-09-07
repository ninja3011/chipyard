package chipyard.fpga.arty100t

import chisel3._

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

// Sticky "has the hart retired even one instruction since power-on" latch,
// wired straight to an LED with zero dependency on any peripheral, wiring,
// or the FT232RL -- the deepest possible probe short of real JTAG access.
// Uses testchipip's existing TraceIO mechanism (normally for cosim/
// FireSim tracing), enabled via chipyard.config.WithTraceIO in
// WithArty100TTweaks, purely to reach this one signal.
class WithArty100TRetireLED extends HarnessBinder({
  case (th: HasHarnessInstantiators, port: TracePort, chipId: Int) => {
    val ath = th.asInstanceOf[LazyRawModuleImp].wrapper.asInstanceOf[Arty100THarness]
    val trace = port.getIO()
    val hart0Trace = trace.traces(0)
    withClockAndReset(hart0Trace.clock, hart0Trace.reset.asAsyncReset) {
      val retireSeen = RegInit(false.B)
      when (hart0Trace.trace.insns(0).valid) { retireSeen := true.B }
      ath.other_leds(3) := retireSeen

      // Narrower question than "did the hart run at all": did it ever
      // retire an instruction actually fetched from DRAM (0x80000000+),
      // i.e. did it ever reach the loaded program, as opposed to being
      // permanently stuck in the boot ROM's WFI wait loop.
      val dramRetireSeen = RegInit(false.B)
      val insn = hart0Trace.trace.insns(0)
      when (insn.valid && insn.iaddr >= "h80000000".U) { dramRetireSeen := true.B }
      ath.other_leds(4) := dramRetireSeen

      // Precise address check: did execution ever reach the actual TXFIFO
      // write instruction in uart_probe.elf's inlined uart_putc() (found
      // via objdump), as opposed to looping forever at 0x80000146 polling
      // a TXFIFO_FULL status that never clears.
      val txfifoWriteSeen = RegInit(false.B)
      when (insn.valid && insn.iaddr === "h8000015a".U) { txfifoWriteSeen := true.B }
      ath.other_leds(5) := txfifoWriteSeen

      // Narrows the gap further: dramRetireSeen fires on ANY DRAM address,
      // including crt0/startup code that runs before main() is ever
      // called. This checks specifically for main()'s own entry point
      // (found via objdump), to tell "stuck before main()" apart from
      // "stuck inside main(), before the TXFIFO write."
      val mainEntrySeen = RegInit(false.B)
      when (insn.valid && insn.iaddr === "h8000010e".U) { mainEntrySeen := true.B }
      ath.other_leds(7) := mainEntrySeen
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

    // Raw, live passthrough -- zero CPU/peripheral dependency. Directly
    // mirrors the RX pin's instantaneous electrical value onto an LED, so
    // "is the FT232RL's signal reaching this pin at all" can be answered
    // by eye, with nothing else in the chain that could be silently
    // broken. other_leds(2) is otherwise unused on this board.
    ath.other_leds(2) := harnessIO.rxd
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
