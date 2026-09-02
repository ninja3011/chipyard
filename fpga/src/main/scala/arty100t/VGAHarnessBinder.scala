// Routes chipyard.vga's ChipTop-level VGAFramebufferPort to real physical
// pins on the Arty A7-100T's JB+JC Pmod headers, matching the real
// Digilent Pmod VGA module's real pinout.
//
// Was originally 3 signals (HSYNC, VSYNC, one monochrome VIDEO bit) on
// JA, for a generic simple VGA breakout, before the peripheral itself
// gained real 12-bit color -- updated to the real 14-pin mapping the
// actual Pmod VGA hardware needs. Pin numbers below are confirmed from
// two independent real sources, not guessed:
//   1. Digilent's own official reference design (github.com/Digilent/
//      Arty-Pmod-VGA, src/constraints/Arty_Master.xdc) -- the real,
//      published wiring for this exact module on this exact board.
//   2. Cross-checked against this project's own local
//      arty-master.xdc, which independently lists the same package
//      pins under the same jb_*/jc_* names -- both sources agree.
//   JB pin 1 (jb_0, package pin E15) -> R[0]
//   JB pin 2 (jb_1, package pin E16) -> R[1]
//   JB pin 3 (jb_2, package pin D15) -> R[2]
//   JB pin 4 (jb_3, package pin C15) -> R[3]
//   JB pin 7 (jb_4, package pin J17) -> B[0]
//   JB pin 8 (jb_5, package pin J18) -> B[1]
//   JB pin 9 (jb_6, package pin K15) -> B[2]
//   JB pin 10 (jb_7, package pin J15) -> B[3]
//   JC pin 1 (jc_0, package pin U12) -> G[0]
//   JC pin 2 (jc_1, package pin V12) -> G[1]
//   JC pin 3 (jc_2, package pin V10) -> G[2]
//   JC pin 4 (jc_3, package pin V11) -> G[3]
//   JC pin 5 (jc_4, package pin U14) -> HSYNC
//   JC pin 6 (jc_5, package pin V14) -> VSYNC

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
      ("U14", IOPin(harnessIO.hsync)),
      ("V14", IOPin(harnessIO.vsync)),
      ("E15", IOPin(harnessIO.r(0))),
      ("E16", IOPin(harnessIO.r(1))),
      ("D15", IOPin(harnessIO.r(2))),
      ("C15", IOPin(harnessIO.r(3))),
      ("U12", IOPin(harnessIO.g(0))),
      ("V12", IOPin(harnessIO.g(1))),
      ("V10", IOPin(harnessIO.g(2))),
      ("V11", IOPin(harnessIO.g(3))),
      ("J17", IOPin(harnessIO.b(0))),
      ("J18", IOPin(harnessIO.b(1))),
      ("K15", IOPin(harnessIO.b(2))),
      ("J15", IOPin(harnessIO.b(3))))
    packagePinsWithPackageIOs foreach { case (pin, io) => {
      ath.xdc.addPackagePin(io, pin)
      ath.xdc.addIOStandard(io, "LVCMOS33")
    } }
  }
})
