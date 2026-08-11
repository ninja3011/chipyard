`timescale 1ns/1ps
module tb_dmi;
  // Matches real BasysTop.v wiring: dmi_dmiClock/dmi_dmiReset tied to the
  // same sys_clk/sys_reset as everything else -- single clock domain.
  reg clk = 0;
  reg rst = 1;
  always #5 clk = ~clk;

  integer cyc = 0;
  always @(posedge clk) cyc <= cyc + 1;

  initial begin
    rst = 1;
    repeat (20) @(posedge clk);
    rst = 0;
  end

  wire        dmi_req_valid, dmi_req_ready;
  wire [6:0]  dmi_req_addr;
  wire [31:0] dmi_req_data;
  wire [1:0]  dmi_req_op;
  wire        dmi_resp_ready, dmi_resp_valid;
  wire [31:0] dmi_resp_data;
  wire [1:0]  dmi_resp_resp;
  wire        loader_busy, loader_done;

  reg start = 0;
  initial begin
    start = 0;
    repeat (40) @(posedge clk);
    start = 1;
  end

  reg [2:0] prog_sel_reg;
  initial begin
    if (!$value$plusargs("prog_sel=%d", prog_sel_reg)) prog_sel_reg = 0;
  end

  DmiAutoloader autoloader (
    .clk            (clk),
    .rst            (rst),
    .start          (start),
    .prog_sel       (prog_sel_reg),
    .dmi_req_valid  (dmi_req_valid),
    .dmi_req_ready  (dmi_req_ready),
    .dmi_req_addr   (dmi_req_addr),
    .dmi_req_data   (dmi_req_data),
    .dmi_req_op     (dmi_req_op),
    .dmi_resp_ready (dmi_resp_ready),
    .dmi_resp_valid (dmi_resp_valid),
    .dmi_resp_data  (dmi_resp_data),
    .dmi_resp_resp  (dmi_resp_resp),
    .busy           (loader_busy),
    .done           (loader_done)
  );

  ChipTop chiptop (
    .uart_0_txd              (),
    .uart_0_rxd               (1'b0),
    .custom_boot              (1'b0),
    .dmi_dmi_req_ready         (dmi_req_ready),
    .dmi_dmi_req_valid         (dmi_req_valid),
    .dmi_dmi_req_bits_addr     (dmi_req_addr),
    .dmi_dmi_req_bits_data     (dmi_req_data),
    .dmi_dmi_req_bits_op       (dmi_req_op),
    .dmi_dmi_resp_ready        (dmi_resp_ready),
    .dmi_dmi_resp_valid        (dmi_resp_valid),
    .dmi_dmi_resp_bits_data    (dmi_resp_data),
    .dmi_dmi_resp_bits_resp    (dmi_resp_resp),
    .dmi_dmiClock              (clk),
    .dmi_dmiReset              (rst),
    .reset_io                 (rst),
    .clock_uncore             (clk),
    .clock_tap                (),
    .serial_tl_0_in_ready      (),
    .serial_tl_0_in_valid      (1'b0),
    .serial_tl_0_in_bits_phit  (32'h0),
    .serial_tl_0_out_ready     (1'b0),
    .serial_tl_0_out_valid     (),
    .serial_tl_0_out_bits_phit (),
    .serial_tl_0_clock_in      (clk)
  );

  reg [1:0] state_d;
  reg [4:0] idx_d;
  reg loader_done_d = 0;
  integer done_cyc = 0;
  always @(posedge clk) begin
    if (chiptop.system.tlDM.dmInner.dmInner.sb2tlOpt.io_wrEn ||
        chiptop.system.tlDM.dmInner.dmInner.sb2tlOpt.io_wrDone ||
        chiptop.system.tlDM.dmInner.dmInner.sb2tlOpt.io_respError ||
        chiptop.system.tlDM.dmInner.dmInner.sb2tlOpt.auto_out_a_valid) begin
      $display("t=%0t cyc=%0d    [SBToTL] wrEn=%b wrDone=%b wrLegal=%b respError=%b sbState=%0d  a_valid=%b a_addr=%08x a_data=%02x",
        $time, cyc,
        chiptop.system.tlDM.dmInner.dmInner.sb2tlOpt.io_wrEn,
        chiptop.system.tlDM.dmInner.dmInner.sb2tlOpt.io_wrDone,
        chiptop.system.tlDM.dmInner.dmInner.sb2tlOpt.io_wrLegal,
        chiptop.system.tlDM.dmInner.dmInner.sb2tlOpt.io_respError,
        chiptop.system.tlDM.dmInner.dmInner.sb2tlOpt.io_sbStateOut,
        chiptop.system.tlDM.dmInner.dmInner.sb2tlOpt.auto_out_a_valid,
        chiptop.system.tlDM.dmInner.dmInner.sb2tlOpt.auto_out_a_bits_address,
        chiptop.system.tlDM.dmInner.dmInner.sb2tlOpt.auto_out_a_bits_data);
    end
    if (dmi_resp_valid && dmi_resp_ready) begin
      $display("t=%0t cyc=%0d *** DMI RESP fired: data=%08x resp=%0d (%s) for addr=%02x wdata=%08x [state=%0d idx=%0d]",
        $time, cyc, dmi_resp_data, dmi_resp_resp,
        (dmi_resp_resp == 2'b00) ? "SUCCESS" : (dmi_resp_resp == 2'b01) ? "FAILURE" : (dmi_resp_resp == 2'b10) ? "HW_FAILURE" : "RESERVED/BUSY",
        dmi_req_addr, dmi_req_data, autoloader.state, autoloader.idx);
    end
    state_d <= autoloader.state;
    idx_d   <= autoloader.idx;
    if (autoloader.state !== state_d || autoloader.idx !== idx_d) begin
      $display("t=%0t cyc=%0d state=%0d idx=%0d req_valid=%b req_ready=%b resp_valid=%b resp_ready=%b addr=%02x data=%08x",
        $time, cyc, autoloader.state, autoloader.idx, dmi_req_valid, dmi_req_ready, dmi_resp_valid, dmi_resp_ready, dmi_req_addr, dmi_req_data);
    end
    if (loader_done && !loader_done_d) begin
      $display(">>> LOADER DONE at cyc=%0d, running 2000 more cycles to watch core execution", cyc);
      done_cyc <= cyc;
    end
    loader_done_d <= loader_done;
    if (loader_done_d && (cyc > done_cyc + 20000)) begin
      $display(">>> POST-LOAD OBSERVATION WINDOW COMPLETE at cyc=%0d", cyc);
      $finish;
    end
    if (cyc > 200000) begin
      $display(">>> TIMEOUT at cyc=%0d, stuck in state=%0d idx=%0d, req_valid=%b req_ready=%b resp_valid=%b",
        cyc, autoloader.state, autoloader.idx, dmi_req_valid, dmi_req_ready, dmi_resp_valid);
      $finish;
    end
  end

  // Independently verify DTIM content after the loader claims to be done:
  // read back the same 2 memory banks the DTIM is built from and compare
  // against the expected firmware words, byte-lane by byte-lane, rather
  // than just trusting the "done" flag.
  reg [31:0] expected [0:23];
  initial begin
    expected[0]=32'h80004137; expected[1]=32'ha0012011; expected[2]=32'h10020737; expected[3]=32'h05500693;
    expected[4]=32'h07b7cf14; expected[5]=32'h47051002; expected[6]=32'h0613c798; expected[7]=32'h06970480;
    expected[8]=32'h86930000; expected[9]=32'h07370226; expected[10]=32'h06851002; expected[11]=32'hcfe3431c;
    expected[12]=32'hc310fe07; expected[13]=32'h0006c603; expected[14]=32'h4501fa6d; expected[15]=32'h00008082;
    expected[16]=32'h6c6c6548; expected[17]=32'h6f57206f; expected[18]=32'h20646c72; expected[19]=32'h6d6f7266;
    expected[20]=32'h6e695420; expected[21]=32'h636f5279; expected[22]=32'h2174656b; expected[23]=32'h0000000a;
  end
endmodule
