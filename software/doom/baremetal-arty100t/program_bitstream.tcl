# Programs Arty100THarness.bit onto the board over JTAG via Vivado's
# Hardware Manager, non-interactively.
#
# Chipyard's own docs (docs/Prototyping/Arty.rst) mention "after
# programming the bitstream" but never say how -- there's no `make
# program` target. This fills that gap.
#
# Run from Windows (the board's JTAG will enumerate as a Windows USB
# device unless routed into WSL via usbipd):
#   vivado -mode batch -source program_bitstream.tcl
#
# If Vivado reports more than one hw_target, this connects to the first
# one found -- check the printed target list if that's wrong for your
# setup (e.g. multiple boards/programmers attached at once).

set bit_file "C:/arty100t-build/chipyard/fpga/generated-src/chipyard.fpga.arty100t.Arty100THarness.RocketArty100TVGAConfig/obj/Arty100THarness.bit"

open_hw_manager
connect_hw_server

if {[catch {set targets [get_hw_targets]} errmsg]} {
  puts "ERROR: no hw_target found (Vivado said: $errmsg)"
  puts "Check: is the board plugged in and powered on? If routing USB into WSL via"
  puts "usbipd, has 'usbipd attach --wsl' been run for this device's busid? This"
  puts "script needs the board visible to WINDOWS, not WSL -- if it's currently"
  puts "attached into WSL, detach it first (usbipd detach) so Windows/Vivado can see it."
  close_hw_manager
  exit 1
}
puts "Available hw_targets: $targets"

open_hw_target
set hw_devices [get_hw_devices]
puts "Available hw_devices: $hw_devices"

if {[llength $hw_devices] == 0} {
  puts "ERROR: hw_target opened but no hw_devices found on it."
  close_hw_target
  close_hw_manager
  exit 1
}

set dev [lindex $hw_devices 0]
set_property PROGRAM.FILE $bit_file $dev
program_hw_devices $dev

puts "Done. If this printed no errors above, the bitstream is now running on the board."
puts "Next: run bringup.sh (see this same directory) to load and run doom-arty100t.elf via uart_tsi."

close_hw_target
close_hw_manager
