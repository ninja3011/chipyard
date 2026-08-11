`timescale 1ns/1ps
//
// Top-level wrapper for TinyRocketDMIConfig on the Digilent Basys3
// (xc7a35tcpg236-1).
//
// No external hardware needed at all: firmware is loaded via the Debug
// Module's DMI interface, driven by DmiAutoloader.v -- a one-button
// hardware sequencer that replays the exact compiled ninad/firmware.mem
// bytes as a fixed sequence of DMI writes (see DmiAutoloader.v for the
// full explanation and sourcing of every register bit position used).
// TinyRocketDMIConfig's only memory is the Rocket core's 16KB DTIM (TCM),
// plain internal BRAM inferred from SyncReadMem in the generated RTL --
// Vivado infers it automatically, no external memory IP needed either.
//
// Port names on the left below (CLK100MHZ, BTNC, ...) are the exact net
// names used in Digilent's official Basys-3-Master.xdc
// (https://github.com/Digilent/digilent-xdc/blob/master/Basys-3-Master.xdc).
// Keep them in sync with basys3.xdc.
//
// Requires a "Clocking Wizard" IP named clk_wiz_0 (Vivado IP catalog),
// configured as:
//   Clocking Options : Primary input clock 100.000 MHz, single-ended
//   Output Clocks    : clk_out1 = 10.000 MHz (requested) -- kept low for
//                      timing margin; retune upward later using the
//                      Implemented Design's timing report as the guide.
//   Reset Type       : Active High
//   port names left at the IP's defaults: clk_in1, clk_out1, reset, locked

module BasysTop (
  input  wire CLK100MHZ,  // W5  -- board 100MHz oscillator
  input  wire BTNC,       // U18 -- center pushbutton, active-high when pressed: system reset
  input  wire BTNU,       // T18 -- up pushbutton, active-high when pressed: start firmware load

  input  wire SW0,        // V17 -- slide switch 0: must be ON to arm the loader (safety interlock)

  output wire LED0,       // U16 -- lit while the loader is running
  output wire LED1,       // E19 -- lit once the loader has finished
  output wire LED2,       // U19 -- mirrors SW0, confirms the board sees "armed"

  output wire RSTX,       // A18 -- Basys3 "RsTx": FPGA TX -> host RX
  input  wire RSRX        // B18 -- Basys3 "RsRx": host TX -> FPGA RX
);

  // ------------------------------------------------------------------
  // Clocking: 100MHz board osc -> Clocking Wizard -> 10MHz system clock
  // ------------------------------------------------------------------
  wire sys_clk;
  wire mmcm_locked;

  clk_wiz_0 clk_wiz_inst (
    .clk_in1  (CLK100MHZ),
    .reset    (1'b0),
    .clk_out1 (sys_clk),
    .locked   (mmcm_locked)
  );

  // ------------------------------------------------------------------
  // Reset synchronizer: BTNC (async, active-high when pressed) or MMCM
  // not yet locked -> clean synchronous active-high reset in sys_clk domain.
  // ------------------------------------------------------------------
  reg [1:0] reset_sync;
  wire async_reset_in = BTNC | ~mmcm_locked;

  always @(posedge sys_clk or posedge async_reset_in) begin
    if (async_reset_in)
      reset_sync <= 2'b11;
    else
      reset_sync <= {reset_sync[0], 1'b0};
  end

  wire sys_reset = reset_sync[1];

  // ------------------------------------------------------------------
  // Start button synchronizer (BTNU is asynchronous to sys_clk)
  // ------------------------------------------------------------------
  reg [2:0] start_sync;
  always @(posedge sys_clk) begin
    if (sys_reset)
      start_sync <= 3'b0;
    else
      start_sync <= {start_sync[1:0], BTNU};
  end
  wire start_armed = start_sync[2] & SW0;

  // ------------------------------------------------------------------
  // DMI autoloader
  // ------------------------------------------------------------------
  wire        dmi_req_valid, dmi_req_ready;
  wire [6:0]  dmi_req_addr;
  wire [31:0] dmi_req_data;
  wire [1:0]  dmi_req_op;
  wire        dmi_resp_ready, dmi_resp_valid;
  wire [31:0] dmi_resp_data;
  wire [1:0]  dmi_resp_resp;
  wire        loader_busy, loader_done;

  DmiAutoloader autoloader (
    .clk            (sys_clk),
    .rst            (sys_reset),
    .start          (start_armed),
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

  assign LED0 = loader_busy;
  assign LED1 = loader_done;
  assign LED2 = SW0;

  // ------------------------------------------------------------------
  // ChipTop instantiation
  // ------------------------------------------------------------------
  ChipTop chiptop (
    .uart_0_txd              (RSTX),
    .uart_0_rxd               (RSRX),

    // Default boot address (BootAddrReg reset value, testchipip/boot/BootAddrReg.scala)
    // is already 0x80000000 == the DTIM base; the autoloader drives ndmreset
    // itself via DMI, so custom_boot is not needed here.
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
    .dmi_dmiClock              (sys_clk),
    .dmi_dmiReset              (sys_reset),

    .reset_io                 (sys_reset),
    .clock_uncore             (sys_clk),
    .clock_tap                (),           // debug clock monitor output, unused

    // serial_tl is testchipip's serial-tilelink link, only used by the
    // simulation TestHarness to back external memory during `make
    // run-binary`. TinyRocketDMIConfig has WithNoMemPort, so there is no
    // external memory here and nothing on real hardware drives this link --
    // tie it off so it stays idle.
    .serial_tl_0_in_ready      (),
    .serial_tl_0_in_valid      (1'b0),
    .serial_tl_0_in_bits_phit  (32'h0),
    .serial_tl_0_out_ready     (1'b0),
    .serial_tl_0_out_valid     (),
    .serial_tl_0_out_bits_phit (),
    .serial_tl_0_clock_in      (sys_clk)
  );

endmodule
