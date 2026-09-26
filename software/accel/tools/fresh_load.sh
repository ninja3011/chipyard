#!/bin/bash
# Reprogram the Arty FPGA over JTAG (Windows Vivado), re-attach USB into WSL, then
# load and start an ELF with uart_tsi, capturing the FT232R console.
#   usage: [BITTCL=program_int8.tcl] [EXTRA="+init_write=0x86100000:0x600DF00D"] fresh_load.sh <elf> <console_log>
# The image's start gate waits for the flag written by EXTRA (see accel_start_gated.S).
ELF=$1; CLOG=$2
BITTCL=${BITTCL:-program_int8.tcl}
UT=/home/ninadjangle/chipyard/generators/testchipip/uart_tsi/uart_tsi
LOADLOG=${LOADLOG:-/tmp/fresh_load.log}
pkill -9 -x uart_tsi; pkill -9 -x cat; pkill -9 -f '[s]erial_log.py'; sleep 1
LIST=$(powershell.exe -NoProfile -Command "usbipd list" 2>&1 | tr -d '\r')
BUS=$(echo "$LIST" | grep "0403:6010" | awk '{print $1}')
echo "[fresh] arty=$BUS bittcl=$BITTCL"
powershell.exe -NoProfile -Command "usbipd detach --busid $BUS" >/dev/null 2>&1
sleep 2
echo "[fresh] programming FPGA"
powershell.exe -NoProfile -Command "Set-Location C:\arty100t-build; & 'C:\AMDDesignTools\2026.1\Vivado\bin\vivado.bat' -mode batch -nojournal -source C:\arty100t-build\\$BITTCL" 2>&1 | tr -d '\r\0' | grep -E "End of startup|ERROR: \[" | tail -2
powershell.exe -NoProfile -Command "usbipd attach --wsl --busid $BUS" >/dev/null 2>&1
FT=$(echo "$LIST" | grep "0403:6001" | awk '{print $1}')
powershell.exe -NoProfile -Command "usbipd attach --wsl --busid $FT" >/dev/null 2>&1
for i in $(seq 1 40); do ls /dev/serial/by-id/*Digilent*if01* >/dev/null 2>&1 && ls /dev/serial/by-id/*FT232R* >/dev/null 2>&1 && break; sleep 1; done
LOAD=$(readlink -f /dev/serial/by-id/*Digilent*if01*); CON=$(readlink -f /dev/serial/by-id/*FT232R*)
echo "[fresh] load=$LOAD console=$CON"
timeout 10 stty -F $CON 115200 cs8 -cstopb -parenb raw -echo clocal || echo "[fresh] stty FAILED"
rm -f ${CLOG%.log}.ts.jsonl; : > $CLOG
(timeout 43200 python3 /home/ninadjangle/chipyard/software/accel/tools/serial_log.py $CON $CLOG > /dev/null 2>&1 &)
(timeout ${LOAD_TIMEOUT:-1500} $UT +tty=$LOAD +baudrate=921600 +no_hart0_msip +init_read=0x80000000 $EXTRA $ELF > $LOADLOG 2>&1 &)
for i in $(seq 1 ${LOAD_TIMEOUT:-1500}); do grep -q "Reading 80000000" $LOADLOG && break; sleep 1; done
echo "[fresh] loaded: $(tail -1 $LOADLOG | cut -c1-60)"
