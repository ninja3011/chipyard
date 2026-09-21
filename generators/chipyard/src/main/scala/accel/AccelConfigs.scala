package chipyard.accel

import org.chipsalliance.cde.config.Config

// Verilator sim of Rocket + the INT8 tile engine, at the same 50MHz the
// Arty build uses (keeps the hardcoded UART divisor valid, see
// chipyard.console.RocketConsoleSimConfig for the full story).
class RocketInt8AccelSimConfig extends Config(
  new WithInt8TileEngine ++
  new chipyard.harness.WithHarnessBinderClockFreqMHz(50) ++
  new chipyard.config.WithUniformBusFrequencies(50) ++
  new chipyard.RocketConfig)
