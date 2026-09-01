// Plain-simulation support for TLVGAFramebuffer: a generic HarnessBinder
// (for the standard chipyard.harness.TestHarness, not the Arty100T FPGA
// shell) and a Config that wires the peripheral onto ordinary
// chipyard.RocketConfig. This is what makes it possible to exercise the
// real CPU -> pbus -> TLFragmenter -> VGA peripheral path in Verilator,
// something the earlier VGAFramebufferUnitTest (a synthetic TileLink
// driver standing in for the CPU) never touched.
//
// Without a HarnessBinder matching VGAFramebufferPort for a generic
// HasHarnessInstantiators, ApplyHarnessBinders (harness/HarnessBinders.scala)
// would hit a MatchError at simulation elaboration time, since the only
// existing binder (WithArty100TVGA) matches the FPGA shell's ChipTop
// specifically. Modeled directly on WithGPIOTiedOff's tie-off pattern.
package chipyard.vga

import chisel3._
import org.chipsalliance.cde.config.Config
import chipyard.harness.{HarnessBinder, HasHarnessInstantiators}
import chipyard.iobinders.VGAFramebufferPort

class WithVGAFramebufferTiedOff extends HarnessBinder({
  case (th: HasHarnessInstantiators, port: VGAFramebufferPort, chipId: Int) => {
    // Outputs only (hsync/vsync/video) -- nothing to drive from the
    // harness side; the real signal-timing checks already happened in
    // VGAFramebufferUnitTest. This binder exists only so plain
    // simulation elaborates cleanly when VGAFramebufferKey is set.
  }
})

class VGAFramebufferSoCSimConfig extends Config(
  new WithVGAFramebufferTiedOff ++
  new WithVGAFramebuffer(address = 0x4000000L) ++
  new chipyard.RocketConfig)
