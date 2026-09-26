#!/bin/bash
# Build an Arty bitstream for a Chipyard FPGA config using Windows-side Vivado.
#   usage: build_bitstream.sh <ConfigName>          e.g. RocketArty100TSystolicConfig
# Prereq: `make -C fpga SUB_PROJECT=arty100t CONFIG=<ConfigName> verilog` already done.
# Steps (each was a real failure once): make the .vsrcs.f list, copy to the Windows mirror,
# rewrite /home/... paths, append the autoloader file the generator omits, pass the 4th IP
# tcl (ILA), run Vivado batch. Also writes program_<name>.tcl for fresh_load.sh (BITTCL=...).
set -e
CFG=$1; NAME=arty100t.Arty100THarness.$CFG
LONG=chipyard.fpga.$NAME
SRC=/home/ninadjangle/chipyard/fpga/generated-src/$LONG
DST=/mnt/c/arty100t-build/chipyard/fpga/generated-src/$LONG
WIN=C:/arty100t-build/chipyard/fpga/generated-src/$LONG
source /home/ninadjangle/chipyard/env.sh >/dev/null 2>&1
( cd /home/ninadjangle/chipyard/fpga && make SUB_PROJECT=arty100t CONFIG=$CFG $SRC/$LONG.vsrcs.f >/dev/null )
rm -rf "$DST"; cp -r "$SRC" "$DST"
python3 - "$DST/$LONG.vsrcs.f" "$DST" "$WIN" <<'PY'
import sys,os
v,dst,win=sys.argv[1:4]
s=open(v).read().replace('/home/ninadjangle/chipyard','C:/arty100t-build/chipyard')
lines=[l for l in s.split('\n') if l.strip()]
auto=win+'/gen-collateral/Arty100TDmiAutoloader.sv'
assert os.path.exists(dst+'/gen-collateral/Arty100TDmiAutoloader.sv')
if auto not in lines: lines.append(auto)
missing=[l for l in lines if not os.path.exists('/mnt/c/'+l[3:])]
print('vsrcs entries',len(lines),'missing',len(missing)); assert not missing, missing[:5]
open(v,'w').write('\n'.join(lines)+'\n')
PY
cat > /mnt/c/arty100t-build/build_$CFG.ps1 <<PS
\$buildDir = "C:\\arty100t-build\\chipyard\\fpga\\generated-src\\$LONG"
Set-Location \$buildDir
\$p = "$LONG"
\$vivado = "C:\\AMDDesignTools\\2026.1\\Vivado\\bin\\vivado.bat"
\$vivadoTcl = "C:/arty100t-build/chipyard/fpga/fpga-shells/xilinx/common/tcl/vivado.tcl"
\$ila = "C:/arty100t-build/chipyard/software/doom/baremetal-arty100t/ila_gen.vivado.tcl"
\$ipTcls = "\$buildDir/\$p.arty100tmig.vivado.tcl \$buildDir/\$p.harnessSysPLLNode.vivado.tcl \$buildDir/\$p.shell.vivado.tcl \$ila"
& \$vivado -nojournal -mode batch -source \$vivadoTcl -tclargs -top-module "Arty100THarness" -F "\$buildDir/\$p.vsrcs.f" -board "arty_a7_100" -ip-vivado-tcls \$ipTcls
Write-Output "BUILD_SCRIPT_EXIT_CODE=\$LASTEXITCODE"
PS
sed "s|RocketArty100TInt8Config/obj|$CFG/obj|" /mnt/c/arty100t-build/program_int8.tcl > /mnt/c/arty100t-build/program_$CFG.tcl
cd /tmp && nohup powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\\arty100t-build\\build_$CFG.ps1" > /mnt/c/arty100t-build/build_$CFG.log 2>&1 &
echo "vivado started; log: C:\\arty100t-build\\build_$CFG.log ; program with BITTCL=program_$CFG.tcl"
