// Plain-simulation config for the console/UART-only DOOM-on-RISC-V track
// (software/doom/baremetal-arty100t), matching WithArty100TTweaks's clock
// assumptions exactly so our existing C code needs zero changes.
//
// Root cause this fixes: plain chipyard.RocketConfig's default harness
// binder clock is 100MHz (confirmed via its own
// "Harness binder clock is 100.0" startup print), while the real Arty
// board's config (WithArty100TTweaks) explicitly pins everything to a
// uniform 50MHz via WithHarnessBinderClockFreqMHz(50) +
// WithUniformBusFrequencies(50). Our uart_init() hardcodes DIV=433,
// calibrated for 115200 baud at 50MHz -- at 100MHz that same divisor
// yields roughly double the intended baud rate (~230kbaud instead of
// 115200), a real clock/baud mismatch between what the DUT actually
// transmits and what WithUARTAdapter's own decode timing
// (getHarnessBinderClockFreqMHz-derived) expects to receive. This is
// what produced consistently garbled UART bytes in plain RocketConfig
// sim runs even though the CPU was confirmed (via commit trace) to
// execute the real program correctly start to finish.
//
// Fix: pin this sim config to the same 50MHz uniform frequency the real
// hardware uses, so the existing hardcoded UART_DIV_115200 stays valid
// and WithUARTAdapter's decode divisor matches what's actually
// transmitted -- no software changes needed, and this sim now reflects
// the same clock assumptions the real board's bitstream does.
package chipyard.console

import org.chipsalliance.cde.config.Config
import testchipip.soc.{WithTraceLogRingBuffer, TraceLogRingBufferParams}

class RocketConsoleSimConfig extends Config(
  new chipyard.harness.WithHarnessBinderClockFreqMHz(50) ++
  new chipyard.config.WithUniformBusFrequencies(50) ++
  new chipyard.RocketConfig)

// Same as RocketConsoleSimConfig, plus the new software-driven trace-log
// ring buffer peripheral (see testchipip/soc/TraceLogRingBuffer.scala)
// at 0x10030000 -- comfortably clear of UART0's own 0x10020000..fff
// region. Kept as a separate config from RocketConsoleSimConfig itself
// so the new, not-yet-hardware-validated peripheral's risk stays
// isolated from the already-proven-working plain config.
class RocketConsoleTraceLogSimConfig extends Config(
  new WithTraceLogRingBuffer(TraceLogRingBufferParams(address = 0x10030000L, depth = 512)) ++
  new RocketConsoleSimConfig)
