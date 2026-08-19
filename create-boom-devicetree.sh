#!/bin/bash
# Create minimal device tree for BOOM test harness
# Based on what we know works: single CPU, UART, memory

set -e

cd /home/ninadjangle/chipyard

echo "╔════════════════════════════════════════════════════════════╗"
echo "║      Creating Minimal Device Tree for BOOM                ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""

# Create simple device tree source
cat > /tmp/boom-minimal.dts << 'EOF'
/dts-v1/;

/ {
    #address-cells = <2>;
    #size-cells = <2>;
    compatible = "boom,sim";
    model = "BOOM Verilator";

    memory@80000000 {
        device_type = "memory";
        reg = <0x0 0x80000000 0x0 0x10000000>;  /* 256MB RAM at 0x80000000 */
    };

    cpus {
        #address-cells = <1>;
        #size-cells = <0>;
        timebase-frequency = <1000000>;

        cpu@0 {
            compatible = "riscv";
            device_type = "cpu";
            reg = <0>;
            status = "okay";
            riscv,isa = "rv64imafd";
            mmu-type = "riscv,sv39";

            intc0: interrupt-controller {
                compatible = "riscv,cpu-intc";
                interrupt-controller;
                #interrupt-cells = <1>;
            };
        };
    };

    /* Platform-level interrupt controller */
    plic: interrupt-controller@c000000 {
        compatible = "riscv,plic0";
        interrupt-controller;
        #interrupt-cells = <2>;
        #address-cells = <0>;
        riscv,max-priority = <7>;
        riscv,ndev = <10>;
        reg = <0x0 0x0c000000 0x0 0x04000000>;
        interrupts-extended = <&intc0 11 &intc0 9>;
    };

    /* Clock */
    clocks {
        sys_clk: sys_clk {
            compatible = "fixed-clock";
            clock-frequency = <1000000>;
            clock-output-names = "sys_clk";
            #clock-cells = <0>;
        };
    };

    /* UART - critical for console */
    uart0: serial@10010000 {
        compatible = "ns16550a";
        reg = <0x0 0x10010000 0x0 0x100>;
        reg-shift = <0>;
        reg-io-width = <4>;
        interrupts = <10>;
        interrupt-parent = <&plic>;
        clock-frequency = <1000000>;
    };

    /* Timer - minimal */
    rtc {
        compatible = "riscv,aclint-mtimer";
        reg = <0x0 0x02000000 0x0 0x10000>;
        interrupts-extended = <&intc0 7>;
    };

    /* Root node has stdout for console */
    chosen {
        bootargs = "console=ttyS0 earlycon=uart8250,mmio,0x10010000,1000000n8 earlyprintk";
        stdout-path = "/uart0:1000000";
    };
};
EOF

echo "✅ Device tree created at /tmp/boom-minimal.dts"
echo ""
echo "To compile:"
echo "  dtc -O dtb -o /tmp/boom-minimal.dtb /tmp/boom-minimal.dts"
echo ""
echo "To embed in kernel:"
echo "  1. Place .dtb in kernel tree"
echo "  2. Configure kernel: CONFIG_BUILTIN_DTB_FILE=/tmp/boom-minimal.dtb"
echo "  3. Rebuild kernel"
echo ""
echo "Device tree features:"
echo "  - 256MB RAM at 0x80000000"
echo "  - Single RV64IMAFDv core"
echo "  - UART0 at 0x10010000"
echo "  - PLIC interrupt controller"
echo "  - Console bootargs configured"
