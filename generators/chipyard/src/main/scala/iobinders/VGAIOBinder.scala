// IOBinder for chipyard.vga.CanHavePeripheryVGAFramebuffer, following the
// exact same pattern as WithGCDIOPunchthrough in IOBinders.scala.

package chipyard.iobinders

import chisel3._
import chipyard.vga.{CanHavePeripheryVGAFramebuffer, VGAFramebufferOutputBundle}

case class VGAFramebufferPort(getIO: () => VGAFramebufferOutputBundle) extends Port[VGAFramebufferOutputBundle]

class WithVGAFramebufferIOPunchthrough extends OverrideIOBinder({
  (system: CanHavePeripheryVGAFramebuffer) => {
    val vgaPort = system.vga_out.map { vga =>
      val io_vga = IO(new VGAFramebufferOutputBundle).suggestName("vga")
      io_vga.hsync := vga.hsync
      io_vga.vsync := vga.vsync
      io_vga.r := vga.r
      io_vga.g := vga.g
      io_vga.b := vga.b
      VGAFramebufferPort(() => io_vga)
    }.toSeq
    (vgaPort, Nil)
  }
})
