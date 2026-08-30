// Routes chipyard.vga's ChipTop-level VGAFramebufferPort to real physical
// pins on the Arty A7-100T's JA Pmod header.
//
// Only 3 signals (HSYNC, VSYNC, one monochrome VIDEO bit) -- deliberately
// generic so this works with any simple VGA breakout wired to JA, not
// contingent on a specific 12-pin color Pmod VGA board. Pin numbers below
// are the Arty's real JA-header-to-FPGA-pin mapping, taken directly from
// arty-master.xdc's own (otherwise-unused-by-RocketArty100TConfig) ja_*
// definitions -- not guessed.
//   JA pin 1 (ja_0, package pin G13) -> HSYNC
//   JA pin 2 (ja_1, package pin B11) -> VSYNC
//   JA pin 3 (ja_2, package pin A11) -> VIDEO

package chipyard.fpga.arty100t

import chisel3._
import freechips.rocketchip.diplomacy.LazyRawModuleImp
import sifive.fpgashells.shell._
import sifive.fpgashells.ip.xilinx._
import chipyard.harness._
import chipyard.iobinders.VGAFramebufferPort

class WithArty100TVGA extends HarnessBinder({
  case (th: HasHarnessInstantiators, port: VGAFramebufferPort, chipId: Int) => {
    val ath = th.asInstanceOf[LazyRawModuleImp].wrapper.asInstanceOf[Arty100THarness]
    val vga = port.getIO()
    val harnessIO = IO(chiselTypeOf(vga)).suggestName("vga")
    harnessIO <> vga
    val packagePinsWithPackageIOs = Seq(
      ("G13", IOPin(harnessIO.hsync)),
      ("B11", IOPin(harnessIO.vsync)),
      ("A11", IOPin(harnessIO.video)))
    packagePinsWithPackageIOs foreach { case (pin, io) => {
      ath.xdc.addPackagePin(io, pin)
      ath.xdc.addIOStandard(io, "LVCMOS33")
    } }
  }
})
