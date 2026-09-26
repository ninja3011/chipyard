// Systolic-array variant of the INT8 8x8 tile engine (same RoCC ISA as
// Int8TileEngine, so all existing software runs unchanged). Only the multiply
// stage differs: instead of broadcasting one column of A and one row of B to
// all 64 multipliers each cycle, this is a genuine OUTPUT-STATIONARY SYSTOLIC
// ARRAY -- an 8x8 grid of processing elements (PEs). Each PE owns one C
// accumulator and only ever talks to its left and top neighbours:
//     A values flow left -> right along each row,
//     B values flow top -> bottom down each column,
//     every cycle each PE does acc += a_in * b_in and forwards a and b on.
// Row i of A is injected i cycles late and column j of B is injected j cycles
// late (the "skew"), so A'[i][k] and B'[k][j] arrive at PE(i,j) on the same
// cycle (k+i+j+1). Fan-out per wire is 1 instead of 8: shorter wires, better
// timing, and it scales to bigger arrays. Latency per tile is 3D-2 cycles.
// Loads/stores/commands are identical to Int8TileEngine (see its header).
package chipyard.accel

import chisel3._
import chisel3.util._

import org.chipsalliance.cde.config.{Parameters, Config}
import freechips.rocketchip.tile._
import org.chipsalliance.diplomacy.lazymodule._
import freechips.rocketchip.rocket.constants.MemoryOpConstants

class SystolicTileEngine(opcodes: OpcodeSet)(implicit p: Parameters) extends LazyRoCC(opcodes) {
  override lazy val module = new SystolicTileEngineModuleImp(this)
}

class SystolicTileEngineModuleImp(outer: SystolicTileEngine)(implicit p: Parameters)
    extends LazyRoCCModuleImp(outer) with HasCoreParameters with MemoryOpConstants {

  val D = 8

  // ---- architectural state ------------------------------------------------
  val a = Reg(Vec(D, Vec(D, SInt(8.W))))
  val b = Reg(Vec(D, Vec(D, SInt(8.W))))
  val c = RegInit(VecInit(Seq.fill(D)(VecInit(Seq.fill(D)(0.S(32.W))))))
  val loadStride  = RegInit(0.U(xLen.W))
  val storeStride = RegInit(0.U(xLen.W))
  val cyc = RegInit(0.U(64.W))
  cyc := cyc + 1.U

  // ---- command latch / FSM -----------------------------------------------
  val sIdle :: sLoad :: sMac :: sStRow :: sStPack :: sStIssue :: sDrain :: sRespond :: Nil = Enum(8)
  val state = RegInit(sIdle)
  val cmdR = Reg(new RoCCCommand)
  val result = Reg(UInt(xLen.W))

  val funct = cmdR.inst.funct
  val F_CFG = 0.U; val F_LDA = 1.U; val F_LDB = 2.U; val F_CLR = 3.U
  val F_MAC = 4.U; val F_ST32 = 5.U; val F_ST8 = 6.U; val F_CYC = 7.U; val F_SYNC = 8.U

  io.cmd.ready := state === sIdle
  io.busy := state =/= sIdle
  io.interrupt := false.B

  // request/response bookkeeping shared by loads and stores
  val issued  = RegInit(0.U(6.W))
  val respCnt = RegInit(0.U(6.W))
  val stRow   = RegInit(0.U(4.W))
  val stW     = RegInit(0.U(3.W))
  val rowVals = Reg(Vec(D, SInt(32.W)))
  val packed  = Reg(Vec(4, UInt(64.W)))

  val is8 = funct === F_ST8

  def finish(): Unit = {
    state := Mux(cmdR.inst.xd, sRespond, sIdle)
  }

  when(io.cmd.fire) {
    cmdR := io.cmd.bits
    issued := 0.U
    respCnt := 0.U
    stRow := 0.U
    stW := 0.U
    val f = io.cmd.bits.inst.funct
    switch(f) {
      is(F_CFG) {
        loadStride := io.cmd.bits.rs1
        storeStride := io.cmd.bits.rs2
      }
      is(F_LDA) { state := sLoad }
      is(F_LDB) { state := sLoad }
      is(F_CLR) {
        for (i <- 0 until D; j <- 0 until D) c(i)(j) := 0.S
      }
      is(F_MAC) { state := sMac }
      is(F_ST32) { state := sStRow }
      is(F_ST8) { state := sStRow }
      is(F_CYC) { result := cyc; state := sRespond }
      is(F_SYNC) { result := 0.U; state := sRespond }
    }
  }

  // ---- memory request defaults -------------------------------------------
  io.mem.req.valid := false.B
  io.mem.req.bits := DontCare
  // NOT phys=true: the D-cache's TLB forces the PMP privilege of physical
  // ("passthrough", page-table-walker style) requests to S-mode, and Rocket's
  // PMP denies S-mode accesses when no PMP entry is configured, so every
  // such request raised an access exception. Normal requests at M-mode pass
  // through the TLB untranslated while satp is bare (this bare-metal case).
  io.mem.req.bits.phys := false.B
  io.mem.req.bits.no_resp := false.B
  // These two were left as DontCare at first; no_xcpt in particular is
  // read by the D-cache as "slave-port access" and changes whether TLB/PMP
  // exceptions are generated at all, so it must be an explicit value.
  io.mem.req.bits.no_alloc := false.B
  io.mem.req.bits.no_xcpt := false.B
  io.mem.req.bits.dprv := cmdR.status.dprv
  io.mem.req.bits.dv := cmdR.status.dv
  io.mem.req.bits.size := 3.U // 64-bit accesses
  io.mem.req.bits.signed := false.B

  when(io.mem.req.fire) { issued := issued + 1.U }
  when(io.mem.resp.valid) { respCnt := respCnt + 1.U }

  // ---- LOAD_A / LOAD_B ----------------------------------------------------
  when(state === sLoad) {
    io.mem.req.valid := issued < D.U
    io.mem.req.bits.addr := cmdR.rs1 + issued * loadStride
    io.mem.req.bits.tag := issued(2, 0)
    io.mem.req.bits.cmd := M_XRD
    io.mem.req.bits.data := 0.U
    when(issued === D.U && respCnt === D.U) { finish() }
  }
  when(io.mem.resp.valid && state === sLoad) {
    val row = io.mem.resp.bits.tag(2, 0)
    val data = io.mem.resp.bits.data
    for (j <- 0 until D) {
      val v = data(8 * j + 7, 8 * j).asSInt
      when(funct === F_LDA) { a(row)(j) := v } .otherwise { b(row)(j) := v }
    }
  }

  // ---- MAC: output-stationary systolic array ------------------------------
  val macT = RegInit(0.U(6.W))               // cycle counter inside a MAC command
  val aPE = RegInit(VecInit(Seq.fill(D)(VecInit(Seq.fill(D)(0.S(8.W))))))   // a value at PE(i,j)
  val bPE = RegInit(VecInit(Seq.fill(D)(VecInit(Seq.fill(D)(0.S(8.W))))))   // b value at PE(i,j)
  // CLEAR must beat the always-on accumulate below (last connection wins)
  val clr = io.cmd.fire && io.cmd.bits.inst.funct === F_CLR
  val transA = cmdR.rs1(0)
  val transB = cmdR.rs1(1)
  val macRun = state === sMac
  val lastT = (3 * D - 2).U                  // last product reaches PE(7,7) at t = 3D-2
  // skewed injection: row i gets A'[i][t-i], column j gets B'[t-j][j]
  for (i <- 0 until D) {
    val k = macT - i.U
    val ok = macRun && macT >= i.U && k < D.U
    val kk = k(2, 0)
    val av = Mux(transA, a(kk)(i), a(i)(kk))
    aPE(i)(0) := Mux(ok, av, 0.S)
    for (j <- 1 until D) aPE(i)(j) := aPE(i)(j - 1)
  }
  for (j <- 0 until D) {
    val k = macT - j.U
    val ok = macRun && macT >= j.U && k < D.U
    val kk = k(2, 0)
    val bv = Mux(transB, b(j)(kk), b(kk)(j))
    bPE(0)(j) := Mux(ok, bv, 0.S)
    for (i <- 1 until D) bPE(i)(j) := bPE(i - 1)(j)
  }
  for (i <- 0 until D; j <- 0 until D) {
    c(i)(j) := Mux(clr, 0.S, c(i)(j) + aPE(i)(j) * bPE(i)(j))   // zeros when idle: adds nothing
  }
  when(macRun) {
    macT := macT + 1.U
    when(macT === lastT + 1.U) {                  // pipeline fully drained
      macT := 0.U
      finish()
    }
  }

  // ---- ST32 / ST8 ----------------------------------------------------------
  when(state === sStRow) {
    rowVals := c(stRow(2, 0))
    state := sStPack
  }
  when(state === sStPack) {
    val sh = cmdR.rs2(4, 0)
    val relu = cmdR.rs2(8)
    val bytes = (0 until D).map { j =>
      val s = (rowVals(j) >> sh).asSInt
      val r = Mux(relu && s < 0.S, 0.S, s)
      val sat = Mux(r > 127.S, 127.S, Mux(r < (-128).S, (-128).S, r))
      sat(7, 0)
    }
    packed(0) := Mux(is8, Cat(bytes.reverse), Cat(rowVals(1).asUInt, rowVals(0).asUInt))
    packed(1) := Cat(rowVals(3).asUInt, rowVals(2).asUInt)
    packed(2) := Cat(rowVals(5).asUInt, rowVals(4).asUInt)
    packed(3) := Cat(rowVals(7).asUInt, rowVals(6).asUInt)
    state := sStIssue
  }
  when(state === sStIssue) {
    val nW = Mux(is8, 1.U, 4.U)
    io.mem.req.valid := stW < nW
    io.mem.req.bits.addr := cmdR.rs1 + stRow * storeStride + (stW << 3)
    io.mem.req.bits.tag := issued(2, 0)
    io.mem.req.bits.cmd := M_XWR
    io.mem.req.bits.data := packed(stW(1, 0))
    when(io.mem.req.fire) { stW := stW + 1.U }
    when(stW === nW) {
      stW := 0.U
      stRow := stRow + 1.U
      state := Mux(stRow === (D - 1).U, sDrain, sStRow)
    }
  }
  when(state === sDrain) {
    val total = Mux(is8, D.U, (4 * D).U)
    when(respCnt === total) { finish() }
  }

  // ---- response ----------------------------------------------------------
  io.resp.valid := state === sRespond
  io.resp.bits.rd := cmdR.inst.rd
  io.resp.bits.data := result
  when(state === sRespond && io.resp.ready) { state := sIdle }
}

class WithSystolicTileEngine(op: OpcodeSet = OpcodeSet.custom0) extends Config((site, here, up) => {
  case BuildRoCC => up(BuildRoCC) ++ Seq((p: Parameters) => {
    val eng = LazyModule(new SystolicTileEngine(op)(p))
    eng
  })
})
