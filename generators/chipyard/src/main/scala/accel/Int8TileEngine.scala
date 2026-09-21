// INT8 8x8 tile matrix-multiply engine, attached to Rocket as a RoCC
// accelerator (custom0 opcode). Step 1 of the "own accelerator -> LoRA
// fine-tune" project: every layer of an MLP / transformer forward AND
// backward pass (including LoRA's small adapter matmuls) reduces to
// tiled C += A x B, with A and/or B optionally transposed. Transposition
// is free here because A and B live in register arrays, so a transposed
// operand is just a different index into the same storage.
//
// Deliberately simple and blocking: the core issues one command, the
// engine runs it to completion, and only then accepts the next. Memory
// traffic goes through Rocket's own L1 D-cache port (io.mem), so it is
// coherent with the core with no DMA/TileLink plumbing, and needs no
// virtual-memory support (bare-metal, physical addresses). Load/store
// rows must be 8-byte aligned (one 64-bit cache request per 8 int8s).
//
// ISA (RoCC funct7; funct3 = xd<<2 | xs1<<1 | xs2):
//   0 CFG      rs1 = load row stride (bytes, for A/B tiles)
//              rs2 = store row stride (bytes, for C tiles)
//   1 LOAD_A   rs1 = addr; A[i][0..7] <- 8 bytes at addr + i*loadStride
//   2 LOAD_B   rs1 = addr; B[k][0..7] <- 8 bytes at addr + k*loadStride
//   3 CLEAR    C <- 0
//   4 MAC      rs1 bit0 = transpose A, bit1 = transpose B
//              C[i][j] += sum_k A'[i][k] * B'[k][j]     (int32 accumulate)
//   5 ST32     rs1 = addr; C row i -> 8 int32 at addr + i*storeStride
//   6 ST8      rs1 = addr; C row i -> 8 int8 at addr + i*storeStride, where
//              each element is (C >> rs2[4:0]), optionally ReLU'd
//              (rs2 bit 8), saturated to [-128,127]
//   7 CYCLES   xd: returns the engine's free-running cycle counter
//   8 SYNC     xd: returns 0 once every previous command has completed
//              (needed because the core does not stall on non-xd RoCC
//              commands, so a following core load could otherwise race a
//              store still draining)
package chipyard.accel

import chisel3._
import chisel3.util._

import org.chipsalliance.cde.config.{Parameters, Config}
import freechips.rocketchip.tile._
import org.chipsalliance.diplomacy.lazymodule._
import freechips.rocketchip.rocket.constants.MemoryOpConstants

class Int8TileEngine(opcodes: OpcodeSet)(implicit p: Parameters) extends LazyRoCC(opcodes) {
  override lazy val module = new Int8TileEngineModuleImp(this)
}

class Int8TileEngineModuleImp(outer: Int8TileEngine)(implicit p: Parameters)
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

  when(io.mem.req.fire) {
    issued := issued + 1.U
    printf("[int8] req addr=%x cmd=%d tag=%d size=%d dprv=%d state=%d\n",
      io.mem.req.bits.addr, io.mem.req.bits.cmd, io.mem.req.bits.tag,
      io.mem.req.bits.size, io.mem.req.bits.dprv, state)
  }
  when(io.mem.resp.valid) {
    respCnt := respCnt + 1.U
    printf("[int8] resp tag=%d cnt=%d state=%d\n", io.mem.resp.bits.tag, respCnt, state)
  }
  val stateDbg = RegNext(state)
  when(state =/= stateDbg) { printf("[int8] state %d -> %d funct=%d\n", stateDbg, state, funct) }
  when(io.cmd.fire) { printf("[int8] cmd funct=%d rs1=%x\n", io.cmd.bits.inst.funct, io.cmd.bits.rs1) }

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

  // ---- MAC: 3-stage pipeline (operand mux -> multiply -> accumulate) -----
  val macK = RegInit(0.U(4.W))
  val v0 = RegInit(false.B)
  val v1 = RegInit(false.B)
  val aReg = Reg(Vec(D, SInt(8.W)))
  val bReg = Reg(Vec(D, SInt(8.W)))
  val prod = Reg(Vec(D, Vec(D, SInt(16.W))))

  val macIssue = state === sMac && macK < D.U
  val transA = cmdR.rs1(0)
  val transB = cmdR.rs1(1)
  v0 := macIssue
  when(macIssue) {
    for (i <- 0 until D) {
      aReg(i) := Mux(transA, a(macK(2, 0))(i), a(i)(macK(2, 0)))
      bReg(i) := Mux(transB, b(i)(macK(2, 0)), b(macK(2, 0))(i))
    }
    macK := macK + 1.U
  }
  v1 := v0
  for (i <- 0 until D; j <- 0 until D) {
    prod(i)(j) := aReg(i) * bReg(j)
    when(v1) { c(i)(j) := c(i)(j) + prod(i)(j) }
  }
  when(state === sMac && macK === D.U && !v0 && !v1) {
    macK := 0.U
    finish()
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

class WithInt8TileEngine(op: OpcodeSet = OpcodeSet.custom0) extends Config((site, here, up) => {
  case BuildRoCC => up(BuildRoCC) ++ Seq((p: Parameters) => {
    val eng = LazyModule(new Int8TileEngine(op)(p))
    eng
  })
})
