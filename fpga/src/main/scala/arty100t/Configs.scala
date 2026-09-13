// See LICENSE for license details.
package chipyard.fpga.arty100t

import org.chipsalliance.cde.config._
import freechips.rocketchip.subsystem._
import freechips.rocketchip.devices.debug._
import freechips.rocketchip.devices.tilelink._
import org.chipsalliance.diplomacy._
import org.chipsalliance.diplomacy.lazymodule._
import freechips.rocketchip.system._
import freechips.rocketchip.tile._

import sifive.blocks.devices.uart._
import sifive.fpgashells.shell.{DesignKey}

import testchipip.serdes.{SerialTLKey}

import chipyard.{BuildSystem}

// don't use FPGAShell's DesignKey
class WithNoDesignKey extends Config((site, here, up) => {
  case DesignKey => (p: Parameters) => new SimpleLazyRawModule()(p)
})

// Enables the Debug Module's System Bus Access (SBA) registers, so memory
// (and CLINT/other MMIO) can be written directly over DMI without halting
// the hart or using the abstract-command/program-buffer path. Off by
// default (rocket-chip DebugModuleParams.hasBusMaster = false). Copied
// verbatim from the Basys3 project (ninad/basys3, commit e16df548) where
// this exact mechanism was used to work around the same
// writes-report-success-but-never-land CLINT/msip bug seen on this board.
class WithSBADebugModule extends Config((site, here, up) => {
  case DebugModuleKey => up(DebugModuleKey).map(_.copy(hasBusMaster = true))
})

// By default, this uses the on-board USB-UART for the TSI-over-UART link
// The PMODUART HarnessBinder maps the actual UART device to JD pin
//
// Debug transport switched from JTAG to DMI, and SBA enabled: this lets an
// on-chip Arty100TDmiAutoloader (see DmiAutoloader.scala + the WithArty100TDMI
// HarnessBinder below) drive the Debug Module directly, the same way the
// Basys3 board's DmiAutoloader.v did -- specifically so it can hold the hart
// in reset while writing CLINT's msip register, which uart_tsi's simpler
// TSI-to-TileLink path has no way to do and which real-hardware testing
// showed is necessary (see RocketArty100TVGAConfig below for the full
// finding). This trades away the physical JTAG pins (WithArty100TJTAG,
// unused anyway -- no external probe is wired to this board) for an
// entirely on-chip wake mechanism.
class WithArty100TTweaks(freqMHz: Double = 50) extends Config(
  new WithArty100TILA ++
  new chipyard.config.WithTraceIO ++
  new chipyard.iobinders.WithClintDebugPunchthrough ++
  new WithArty100TJAUART ++
  new WithArty100TUARTTSI ++
  new WithArty100TDDRTL ++
  new WithArty100TDMI ++
  new WithClintDebugTap ++
  new WithSBADebugModule ++
  new chipyard.config.WithDMIDTM ++
  new WithNoDesignKey ++
  new testchipip.tsi.WithUARTTSIClient ++
  new chipyard.harness.WithSerialTLTiedOff ++
  new chipyard.harness.WithHarnessBinderClockFreqMHz(freqMHz) ++
  new chipyard.config.WithUniformBusFrequencies(freqMHz) ++
  new chipyard.harness.WithAllClocksFromHarnessClockInstantiator ++
  new chipyard.clocking.WithPassthroughClockGenerator ++
  new chipyard.config.WithTLBackingMemory ++ // FPGA-shells converts the AXI to TL for us
  new freechips.rocketchip.subsystem.WithExtMemSize(BigInt(256) << 20) ++ // 256mb on ARTY
  new freechips.rocketchip.subsystem.WithoutTLMonitors)

class RocketArty100TConfig extends Config(
  new WithArty100TTweaks ++
  new chipyard.config.WithBroadcastManager ++ // no l2
  new chipyard.RocketConfig)

// Adds a monochrome VGA framebuffer (DOOM + Bad Apple bring-up target) on
// top of the already-clean RocketArty100TConfig. Kept as a separate
// config, not a modification of RocketArty100TConfig itself, so the new
// hardware's risk (a brand new peripheral, never synthesized before) is
// isolated from the already-verified, already-committed clean-timing build.
class RocketArty100TVGAConfig extends Config(
  // NOTE: previously overrode WithBootROM(hang=0x80000000L) to bypass the
  // boot ROM entirely. That was reverted: bypassing the boot ROM means the
  // core starts executing immediately at cold reset, racing ahead of the
  // slow UART-TSI load -- by the time DRAM actually has our program in it,
  // the core has already long since fetched garbage and wedged itself.
  // uart_tsi's only "start the program" primitive is an MSIP poke aimed at
  // a parked WFI loop (see uart_tsi usage/no_hart0_msip), it has no
  // halt-then-load-then-resume path over this link. So the boot ROM's
  // WFI-park + MSIP-wake handshake is a load-order necessity here, not
  // just legacy plumbing -- keeping stock boot ROM behavior below.
  //
  // New finding: verified via write-then-readback over uart_tsi that a
  // write to CLINT MSIP (0x2000000) reports success but reads back as 0
  // -- it never reaches the register, while writes to DRAM (0x80000000+)
  // work fine. Tried forcing the FBUS->SBUS clock crossing synchronous as
  // a guess at why (ruled out on real hardware -- no change). The Basys3
  // board hit the same symptom and root-caused it differently: SBA writes
  // to CLINT are only honored while the hart is held in reset; once it's
  // running (even just parked in WFI) they're silently dropped. Since
  // uart_tsi has no way to control reset at all, WithArty100TTweaks now
  // switches to DMI+SBA and an on-chip autoloader (see DmiAutoloader.scala)
  // that replicates Basys3's exact working sequence: hold reset, write
  // BOOTADDR_REG and CLINT msip while still in reset, release reset last.
  new WithArty100TVGA ++
  new chipyard.vga.WithVGAFramebuffer(address = 0x4000000L) ++
  new WithArty100TTweaks ++
  new chipyard.config.WithBroadcastManager ++ // no l2
  new chipyard.RocketConfig)

// A deliberately UN-modified baseline, decoupled from every change made
// tonight while chasing the CLINT/msip mystery (DMI transport, SBA, the
// on-chip autoloader, the CLINT debug tap, VGA) -- stock JTAG transport
// (unused, no probe wired to it, but that's fine, we're not driving DMI
// here), stock boot ROM (WFI + MSIP wake via uart_tsi's own default
// behavior), the same ILA already proven to work. Built specifically to
// run a genuine chipyard/riscv-tests binary (real tohost/fromhost
// symbols, so uart_tsi can talk to it properly instead of needing the
// magic-DRAM-value workaround minimal_test.S needed) as a sanity check:
// does ANYTHING boot and run at all in as close to chipyard's own
// out-of-the-box setup as this board allows, independent of every
// hypothesis chased so far.
class WithArty100TCleanTweaks(freqMHz: Double = 50) extends Config(
  new WithArty100TILA ++
  new chipyard.config.WithTraceIO ++
  new WithArty100TJAUART ++
  new WithArty100TUARTTSI ++
  new WithArty100TDDRTL ++
  new WithArty100TJTAG ++
  new WithNoDesignKey ++
  new testchipip.tsi.WithUARTTSIClient ++
  new chipyard.harness.WithSerialTLTiedOff ++
  new chipyard.harness.WithHarnessBinderClockFreqMHz(freqMHz) ++
  new chipyard.config.WithUniformBusFrequencies(freqMHz) ++
  new chipyard.harness.WithAllClocksFromHarnessClockInstantiator ++
  new chipyard.clocking.WithPassthroughClockGenerator ++
  new chipyard.config.WithTLBackingMemory ++ // FPGA-shells converts the AXI to TL for us
  new freechips.rocketchip.subsystem.WithExtMemSize(BigInt(256) << 20) ++ // 256mb on ARTY
  new freechips.rocketchip.subsystem.WithoutTLMonitors)

class RocketArty100TCleanConfig extends Config(
  new WithArty100TCleanTweaks ++
  new chipyard.config.WithBroadcastManager ++ // no l2
  new chipyard.RocketConfig)

class NoCoresArty100TConfig extends Config(
  new WithArty100TTweaks ++
  new chipyard.config.WithBroadcastManager ++ // no l2
  new chipyard.NoCoresConfig)

// This will fail to close timing above 50 MHz
class BringupArty100TConfig extends Config(
  new WithArty100TSerialTLToGPIO ++
  new WithArty100TTweaks(freqMHz = 50) ++
  new testchipip.serdes.WithSerialTLPHYParams(testchipip.serdes.DecoupledInternalSyncSerialPhyParams(freqMHz=50)) ++
  new chipyard.ChipBringupHostConfig)
