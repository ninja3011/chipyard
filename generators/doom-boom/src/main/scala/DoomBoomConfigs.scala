package doom_boom

import boom.common._
import boom.ifu._
import freechips.rocketchip.subsystem._
import freechips.rocketchip.tile._
import freechips.rocketchip.tilelink._
import freechips.rocketchip.devices.tilelink._
import freechips.rocketchip.diplomacy._
import freechips.rocketchip.system._
import freechips.rocketchip.config._
import freechips.rocketchip.util._
import chipyard._

/**
 * DOOM Challenge BOOM CPU Configuration
 *
 * BOOM MediumConfig (4-wide issue, 8-stage pipeline) optimized for:
 * - Doom3/DOOM rendering math (FP-intensive)
 * - FPGA deployment on AWS F1 instances
 * - 21-day RTL → FPGA → Linux kernel → Game software stack
 */

object DoomBoomConfig extends Config(
  new boom.common.WithBoomCores(nCores=1) ++
  new WithDoomBoomMediumCore ++
  new freechips.rocketchip.subsystem.WithInclusiveLastLevelCache(
    capacityKB = 256,
    nWays = 4,
    lineBytes = 64) ++
  new freechips.rocketchip.subsystem.WithMemoryBus(
    beatBytes = 8,
    blockBytes = 64) ++
  new freechips.rocketchip.subsystem.WithNMemoryChannels(1) ++
  new freechips.rocketchip.subsystem.WithSystemBus(
    beatBytes = 8) ++
  new freechips.rocketchip.system.BaseConfig
)

/**
 * DoomBoomMediumCore: 4-wide BOOM configuration
 *
 * Design goals:
 * - Balance performance and FPGA resource usage
 * - Support full RV64IMAFDv ISA
 * - 256KB L2 cache (conservative, scalable)
 * - 8-stage pipeline for good frequency on FPGA synthesis
 */
class WithDoomBoomMediumCore extends Config((site, here, up) => {
  case BoomTilesKey => Seq(BoomTileParams(
    core = BoomCoreParams(
      // Pipeline dimensions
      fetchWidth = 2,                      // Fetch 2 instructions per cycle
      decodeWidth = 2,                     // Decode 2 instructions per cycle
      numIntIssueSlots = 12,               // Integer issue queue depth
      numFpIssueSlots = 8,                 // FP issue queue depth
      numMemIssueSlots = 4,                // Memory issue queue depth

      // Issue parameters
      issueParams = Seq(
        IssueParams(issueWidth=4, numEntries=16, iqType=IQT_INT.litValue),
        IssueParams(issueWidth=2, numEntries=8, iqType=IQT_FP.litValue),
        IssueParams(issueWidth=2, numEntries=8, iqType=IQT_MEM.litValue)
      ),

      // Execution units (ROB and physical registers)
      numRobEntries = 64,                  // Reorder buffer size (moderate for FPGA)
      numIntPhysRegs = 100,                // Integer physical registers
      numFpPhysRegs = 64,                  // FP physical registers
      numLsuPorts = 2,                     // Load/store unit ports

      // Memory subsystem
      icache = Some(ICacheParams(
        rowBits = 128,
        nSets = 64,                        // 32KB I-cache, 1-way (direct-mapped)
        nWays = 1,
        blockBytes = 64,
        latency = 2,
        nTLBEntries = 32
      )),
      dcache = Some(DCacheParams(
        rowBits = 128,
        nSets = 64,                        // 32KB D-cache, 2-way
        nWays = 2,
        blockBytes = 64,
        nMSHRs = 4,
        nTLBEntries = 32,
        nL1Cmd = 4
      )),

      // Functional units
      intWidth = 64,                       // Integer datapath width (64-bit)
      hasMultDiv = true,                   // Include multiply/divide unit
      hasFPU = true,                       // Include FP unit (for Doom3 math)
      fpWidth = 64,                        // FP datapath width

      // Branch prediction
      nBrHistLength = 12,                  // Branch history length
      brCond = Some(BranchPredictorParams(
        nSets = 512,
        nWays = 2,
        history_type = 1
      )),

      // Exception handling
      useAtomics = true,                   // Support atomic operations (RV64A)
      useCompressed = true,                // Support compressed instructions (16-bit)
      useRVE = false,                      // No RVE (embedded)
      useVM = true,                        // Virtual memory support (Sv39/Sv48)
      useSupervisor = true,                // Support supervisor mode (for Linux kernel)

      // Debug & tracing (optional)
      enableCommitLog = false,             // No commit logging (save FPGA resources)
      traceInstructions = false,

      // Custom CSRs
      nCustomMRWCSRs = 0
    ),
    btypeParams = BoomBranchTypeParams()
  ))

  case RocketTilesKey => Seq()             // BOOM only, no Rocket tiles
  case SystemBusKey => SystemBusParams(beatBytes = 8)
  case MemoryBusKey => MemoryBusParams(beatBytes = 8, blockBytes = 64)
})

/**
 * Additional configuration helpers
 */
object DoomBoomSmallConfig extends Config(
  new boom.common.WithBoomCores(nCores=1) ++
  new WithDoomBoomSmallCore ++
  new freechips.rocketchip.subsystem.WithInclusiveLastLevelCache(
    capacityKB = 128,
    nWays = 4,
    lineBytes = 64) ++
  new freechips.rocketchip.system.BaseConfig
)

class WithDoomBoomSmallCore extends Config((site, here, up) => {
  case BoomTilesKey => Seq(BoomTileParams(
    core = BoomCoreParams(
      fetchWidth = 2,
      decodeWidth = 2,
      numIntIssueSlots = 8,
      numFpIssueSlots = 4,
      numMemIssueSlots = 2,
      issueParams = Seq(
        IssueParams(issueWidth=2, numEntries=8, iqType=IQT_INT.litValue),
        IssueParams(issueWidth=1, numEntries=4, iqType=IQT_FP.litValue),
        IssueParams(issueWidth=1, numEntries=4, iqType=IQT_MEM.litValue)
      ),
      numRobEntries = 32,
      numIntPhysRegs = 64,
      numFpPhysRegs = 32,
      icache = Some(ICacheParams(rowBits = 128, nSets = 32, nWays = 1, blockBytes = 64)),
      dcache = Some(DCacheParams(rowBits = 128, nSets = 32, nWays = 2, blockBytes = 64)),
      hasMultDiv = true,
      hasFPU = true,
      useAtomics = true,
      useCompressed = true
    ),
    btypeParams = BoomBranchTypeParams()
  ))
  case RocketTilesKey => Seq()
  case SystemBusKey => SystemBusParams(beatBytes = 8)
})

/**
 * BOOM config with Serial TL disabled (avoids TileLink monitor issues)
 *
 * Uses WithSerialTLTiedOff to bypass the test harness TSIHarness
 * which has strict TileLink protocol checking that rejects PutPartial.
 * This version boots directly from ROM instead of using serial interface.
 */
class DoomBoomNoSerialTLConfig extends Config(
  new chipyard.harness.WithSerialTLTiedOff ++           // Disable serial TL interface
  new boom.common.WithBoomCores(nCores=1) ++
  new WithDoomBoomMediumCore ++
  new freechips.rocketchip.subsystem.WithInclusiveLastLevelCache(
    capacityKB = 256,
    nWays = 4,
    lineBytes = 64) ++
  new freechips.rocketchip.subsystem.WithMemoryBus(
    beatBytes = 8,
    blockBytes = 64) ++
  new freechips.rocketchip.subsystem.WithNMemoryChannels(1) ++
  new freechips.rocketchip.subsystem.WithSystemBus(
    beatBytes = 8) ++
  new freechips.rocketchip.system.BaseConfig
)

/**
 * Default configuration (fallback to Rocket if BOOM fails)
 */
object DoomRocketConfig extends Config(
  new freechips.rocketchip.subsystem.WithNBigCores(1) ++
  new freechips.rocketchip.subsystem.WithInclusiveLastLevelCache(
    capacityKB = 256,
    nWays = 4,
    lineBytes = 64) ++
  new freechips.rocketchip.system.BaseConfig
)
