## basys3.xdc -- TinyRocketDMIConfig on Basys3 (xc7a35tcpg236-1)
##
## Pin locations below are copied verbatim from Digilent's official master XDC:
## https://github.com/Digilent/digilent-xdc/blob/master/Basys-3-Master.xdc
## Only the ports BasysTop.v actually uses are uncommented here.

## 100MHz system oscillator
set_property -dict { PACKAGE_PIN W5   IOSTANDARD LVCMOS33 } [get_ports CLK100MHZ]
create_clock -name CLK100MHZ -period 10.00 -waveform {0 5} [get_ports CLK100MHZ]

## Buttons
set_property -dict { PACKAGE_PIN U18  IOSTANDARD LVCMOS33 } [get_ports BTNC] ;# center: system reset
set_property -dict { PACKAGE_PIN T18  IOSTANDARD LVCMOS33 } [get_ports BTNU] ;# up: start firmware load

## Switches
set_property -dict { PACKAGE_PIN V17  IOSTANDARD LVCMOS33 } [get_ports SW0]  ;# sw[0]: arm/disarm the loader
set_property -dict { PACKAGE_PIN V16  IOSTANDARD LVCMOS33 } [get_ports SW1]  ;# sw[1]: program select bit 0 (LSB)
set_property -dict { PACKAGE_PIN W16  IOSTANDARD LVCMOS33 } [get_ports SW2]  ;# sw[2]: program select bit 1
set_property -dict { PACKAGE_PIN W17  IOSTANDARD LVCMOS33 } [get_ports SW3]  ;# sw[3]: program select bit 2 (MSB)

## LEDs
set_property -dict { PACKAGE_PIN U16  IOSTANDARD LVCMOS33 } [get_ports LED0] ;# led[0]: loader busy
set_property -dict { PACKAGE_PIN E19  IOSTANDARD LVCMOS33 } [get_ports LED1] ;# led[1]: loader done
set_property -dict { PACKAGE_PIN U19  IOSTANDARD LVCMOS33 } [get_ports LED2] ;# led[2]: mirrors SW0 (armed)
set_property -dict { PACKAGE_PIN V19  IOSTANDARD LVCMOS33 } [get_ports LED3] ;# led[3]: mirrors SW1
set_property -dict { PACKAGE_PIN W18  IOSTANDARD LVCMOS33 } [get_ports LED4] ;# led[4]: mirrors SW2
set_property -dict { PACKAGE_PIN U15  IOSTANDARD LVCMOS33 } [get_ports LED5] ;# led[5]: mirrors SW3

## USB-RS232 (routed over the same USB cable as programming, via the FTDI chip)
set_property -dict { PACKAGE_PIN A18  IOSTANDARD LVCMOS33 } [get_ports RSTX]
set_property -dict { PACKAGE_PIN B18  IOSTANDARD LVCMOS33 } [get_ports RSRX]

## Configuration options (from Digilent's master XDC, applies to all designs)
set_property CONFIG_VOLTAGE 3.3 [current_design]
set_property CFGBVS VCCO [current_design]
set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]
set_property BITSTREAM.CONFIG.CONFIGRATE 33 [current_design]
set_property CONFIG_MODE SPIx4 [current_design]

## BTNC/BTNU/SW0-SW3 feed only asynchronous inputs of BasysTop.v's
## synchronizers (reset_sync / start_sync) or are sampled combinationally
## into a register on the same edge as start_armed (SW1-SW3 -> prog_sel),
## so none of these are synchronous-timing paths.
set_false_path -from [get_ports BTNC]
set_false_path -from [get_ports BTNU]
set_false_path -from [get_ports SW0]
set_false_path -from [get_ports SW1]
set_false_path -from [get_ports SW2]
set_false_path -from [get_ports SW3]
