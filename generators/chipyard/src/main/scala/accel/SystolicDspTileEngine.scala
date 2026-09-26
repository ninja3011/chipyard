// DSP-mapped systolic-array variant. Same behaviour and ISA as SystolicTileEngine, but each of the 64
// processing elements is a small Verilog black box (SystolicPE) with (* use_dsp = "yes" *): one FPGA DSP48
// slice per PE doing acc += a*b in hardware. The plain-Chisel version synthesized its 64 8x8 multipliers
// into ~8,400 LUTs and only 2 DSPs (of 240 on the Arty A7-100T); this frees that logic and shortens paths.
package chipyard.accel

import chisel3._
import chisel3.util._

import org.chipsalliance.cde.config.{Parameters, Config}
import freechips.rocketchip.tile._
import org.chipsalliance.diplomacy.lazymodule._
import freechips.rocketchip.rocket.constants.MemoryOpConstants
import chisel3.util.HasBlackBoxInline


class SystolicPE extends BlackBox with HasBlackBoxInline {
  val io = IO(new Bundle {
    val clock = Input(Clock())
    val a_in  = Input(SInt(8.W)); val b_in = Input(SInt(8.W)); val clr = Input(Bool())
    val a_out = Output(SInt(8.W)); val b_out = Output(SInt(8.W)); val acc = Output(SInt(32.W))
  })
  setInline("SystolicPE.v",
    """module SystolicPE(
      |  input clock,
      |  input signed [7:0] a_in, input signed [7:0] b_in, input clr,
      |  output signed [7:0] a_out, output signed [7:0] b_out, output signed [31:0] acc);
      |  reg signed [7:0] a_r, b_r;
      |  (* use_dsp = "yes" *) reg signed [31:0] acc_r;
      |  always @(posedge clock) begin
      |    a_r <= a_in; b_r <= b_in;
      |    if (clr) acc_r <= 32'sd0; else acc_r <= acc_r + a_r * b_r;
      |  end
      |  assign a_out = a_r; assign b_out = b_r; assign acc = acc_r;
      |endmodule
      |""".stripMargin)
}

class SystolicDspTileEngine(opcodes: OpcodeSet)(implicit p: Parameters) extends LazyRoCC(opcodes) {
  override lazy val module = new SystolicDspTileEngineModuleImp(this)
}

class SystolicDspTileEngineModuleImp(outer: SystolicDspTileEngine)(implicit p: Parameters)
    extends LazyRoCCModuleImp(outer) with HasCoreParameters with MemoryOpConstants {

  val D = 8

  // ---- architectural state ------------------------------------------------
  val a = Reg(Vec(D, Vec(D, SInt(8.W))))
  val b = Reg(Vec(D, Vec(D, SInt(8.W))))
  val pes = Seq.fill(D, D)(Module(new SystolicPE))
  val c = VecInit(pes.map(r => VecInit(r.map(_.io.acc))))    // accumulators live inside the DSP-mapped PEs
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
      is(F_CLR) { }              // cleared through the PEs' clr input (see MAC section)
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

  // ---- MAC: output-stationary systolic array of DSP-mapped PEs --------------
  val macT = RegInit(0.U(6.W))               // cycle counter inside a MAC command
  val transA = cmdR.rs1(0)
  val transB = cmdR.rs1(1)
  val macRun = state === sMac
  val lastT = (3 * D - 2).U                  // last product reaches PE(7,7) at t = 3D-2
  val clr = io.cmd.fire && io.cmd.bits.inst.funct === F_CLR
  // skewed injection: row i gets A'[i][t-i], column j gets B'[t-j][j]
  val aInj = Wire(Vec(D, SInt(8.W))); val bInj = Wire(Vec(D, SInt(8.W)))
  for (i <- 0 until D) {
    val k = macT - i.U; val ok = macRun && macT >= i.U && k < D.U; val kk = k(2, 0)
    aInj(i) := Mux(ok, Mux(transA, a(kk)(i), a(i)(kk)), 0.S)
  }
  for (j <- 0 until D) {
    val k = macT - j.U; val ok = macRun && macT >= j.U && k < D.U; val kk = k(2, 0)
    bInj(j) := Mux(ok, Mux(transB, b(j)(kk), b(kk)(j)), 0.S)
  }
  for (i <- 0 until D; j <- 0 until D) {
    pes(i)(j).io.clock := clock
    pes(i)(j).io.clr := clr
    pes(i)(j).io.a_in := (if (j == 0) aInj(i) else pes(i)(j - 1).io.a_out)
    pes(i)(j).io.b_in := (if (i == 0) bInj(j) else pes(i - 1)(j).io.b_out)
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

class WithSystolicDspTileEngine(op: OpcodeSet = OpcodeSet.custom0) extends Config((site, here, up) => {
  case BuildRoCC => up(BuildRoCC) ++ Seq((p: Parameters) => {
    val eng = LazyModule(new SystolicDspTileEngine(op)(p))
    eng
  })
})
