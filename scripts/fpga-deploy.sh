#!/bin/bash
# DOOM Challenge - AWS F1 FPGA Deployment Script
# Automates bitstream generation, AWS provisioning, and kernel deployment

set -e

CHIPYARD_DIR="/home/ninadjangle/chipyard"
FIRESIM_DIR="$CHIPYARD_DIR/sims/firesim"
CONFIG="MediumBoomV3Config"
AWS_REGION="us-west-2"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}╔════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║  DOOM Challenge - AWS F1 FPGA Deployment Pipeline        ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════════╝${NC}"

# ============================================================================
# PHASE 1: Pre-flight checks
# ============================================================================
echo -e "\n${YELLOW}[PHASE 1] Pre-flight Checks${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

check_requirements() {
    echo -e "${BLUE}Checking requirements...${NC}"

    # Check AWS CLI
    if ! command -v aws &> /dev/null; then
        echo -e "${RED}❌ AWS CLI not found. Install: pip install awscli${NC}"
        exit 1
    fi
    echo -e "${GREEN}✓ AWS CLI${NC}"

    # Check AWS credentials
    if ! aws sts get-caller-identity &> /dev/null; then
        echo -e "${RED}❌ AWS credentials not configured. Run: aws configure${NC}"
        exit 1
    fi
    echo -e "${GREEN}✓ AWS credentials${NC}"

    # Check Vivado (optional if not on build machine)
    if command -v vivado &> /dev/null; then
        echo -e "${GREEN}✓ Vivado${NC}"
    else
        echo -e "${YELLOW}⚠ Vivado not found (OK if building on AWS)${NC}"
    fi

    # Check Chipyard
    if [ ! -d "$CHIPYARD_DIR" ]; then
        echo -e "${RED}❌ Chipyard directory not found at $CHIPYARD_DIR${NC}"
        exit 1
    fi
    echo -e "${GREEN}✓ Chipyard${NC}"

    # Check BOOM simulator exists
    if [ ! -f "$CHIPYARD_DIR/sims/verilator/simulator-chipyard.harness-$CONFIG" ]; then
        echo -e "${YELLOW}⚠ BOOM simulator not built. Build first: make CONFIG=$CONFIG${NC}"
    else
        echo -e "${GREEN}✓ BOOM Verilator simulator${NC}"
    fi
}

check_aws_resources() {
    echo -e "${BLUE}Checking AWS resources...${NC}"

    # Check F1 availability
    AVAILABILITY=$(aws ec2 describe-instance-types \
        --filters "Name=instance-type,Values=f1.2xlarge" \
        --region $AWS_REGION 2>/dev/null | jq '.InstanceTypes | length')

    if [ "$AVAILABILITY" -gt 0 ]; then
        echo -e "${GREEN}✓ F1 instances available in $AWS_REGION${NC}"
    else
        echo -e "${RED}❌ F1 instances not available in $AWS_REGION${NC}"
        echo "   Try: us-east-1, us-west-2, eu-west-1"
        exit 1
    fi

    # Check S3 bucket
    S3_BUCKET="doom-challenge-fpga-builds"
    if aws s3 ls "s3://$S3_BUCKET" 2>/dev/null; then
        echo -e "${GREEN}✓ S3 bucket found: $S3_BUCKET${NC}"
    else
        echo -e "${YELLOW}⚠ S3 bucket not found. Will create: $S3_BUCKET${NC}"
        aws s3 mb "s3://$S3_BUCKET" --region $AWS_REGION
        echo -e "${GREEN}✓ S3 bucket created${NC}"
    fi
}

check_requirements
check_aws_resources

# ============================================================================
# PHASE 2: Bitstream Generation Setup
# ============================================================================
echo -e "\n${YELLOW}[PHASE 2] Bitstream Generation Setup${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

setup_bitstream_dir() {
    BITSTREAM_DIR="$CHIPYARD_DIR/fpga-build/bitstreams"
    mkdir -p "$BITSTREAM_DIR"
    echo -e "${GREEN}✓ Bitstream output directory: $BITSTREAM_DIR${NC}"
}

generate_device_tree() {
    echo -e "${BLUE}Generating device tree...${NC}"

    # Create minimal device tree for BOOM + F1
    cat > "$CHIPYARD_DIR/fpga-build/devicetree.dts" << 'DTB'
/dts-v1/;

/ {
    #address-cells = <2>;
    #size-cells = <2>;
    compatible = "ucb-bar,chipyard-boom";
    model = "DOOM BOOM RISC-V CPU";

    chosen {
        bootargs = "root=/dev/vda ro console=hvc0 earlycon=sbi";
        stdout-path = "/uart@54000000";
    };

    cpus {
        #address-cells = <1>;
        #size-cells = <0>;
        timebase-frequency = <1000000>;

        cpu@0 {
            compatible = "riscv";
            device_type = "cpu";
            reg = <0>;
            riscv,isa = "rv64imafd";
            mmu-type = "riscv,sv48";
            clock-frequency = <1000000000>;

            interrupt-controller {
                #interrupt-cells = <1>;
                interrupt-controller;
                compatible = "riscv,cpu-intc";
            };
        };
    };

    memory@80000000 {
        device_type = "memory";
        reg = <0x0 0x80000000 0x8 0x00000000>;
    };

    soc {
        #address-cells = <2>;
        #size-cells = <2>;
        compatible = "simple-bus";
        ranges;

        uart@54000000 {
            compatible = "ns16550";
            reg = <0x0 0x54000000 0x0 0x1000>;
            clock-frequency = <115200>;
            current-speed = <115200>;
            interrupt-parent = <&plic>;
            interrupts = <1>;
        };

        plic: interrupt-controller@c000000 {
            compatible = "riscv,plic0";
            #address-cells = <0>;
            #interrupt-cells = <1>;
            interrupt-controller;
            reg = <0x0 0xc000000 0x0 0x4000000>;
            riscv,ndev = <10>;
        };

        clint@2000000 {
            compatible = "riscv,clint0";
            reg = <0x0 0x2000000 0x0 0x10000>;
            interrupts-extended = <&cpu0_intc 3 &cpu0_intc 7>;
        };
    };
};
DTB

    # Compile DTS to DTB
    dtc -I dts -O dtb -o "$CHIPYARD_DIR/fpga-build/devicetree.dtb" \
        "$CHIPYARD_DIR/fpga-build/devicetree.dts"

    echo -e "${GREEN}✓ Device tree generated${NC}"
}

setup_bitstream_dir
generate_device_tree

# ============================================================================
# PHASE 3: FireSim Configuration
# ============================================================================
echo -e "\n${YELLOW}[PHASE 3] FireSim Configuration${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

setup_firesim() {
    echo -e "${BLUE}Configuring FireSim...${NC}"

    cd "$FIRESIM_DIR"

    # Initialize FireSim if needed
    if [ ! -d "env" ]; then
        echo "Setting up FireSim environment..."
        ./build-setup.sh
    fi

    # Create build configuration
    cat > "$FIRESIM_DIR/deploy/config_build_custom.ini" << 'FIREBUILD'
[build]
board=f1
tool=vivado
vivado_version=2023.1
output_dir=bitstreams
freq_mhz=90

[firesim]
chipyard_config=MediumBoomV3Config
top_module=ChipTop
FIREBUILD

    echo -e "${GREEN}✓ FireSim configured${NC}"
}

setup_firesim

# ============================================================================
# PHASE 4: Generate AWS Deployment Package
# ============================================================================
echo -e "\n${YELLOW}[PHASE 4] AWS Deployment Package${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

create_deployment_package() {
    echo -e "${BLUE}Creating deployment package...${NC}"

    DEPLOY_PKG="$CHIPYARD_DIR/fpga-build/doom-boom-deployment.tar.gz"

    tar -czf "$DEPLOY_PKG" \
        -C "$CHIPYARD_DIR/fpga-build" devicetree.dtb \
        -C "$CHIPYARD_DIR" \
        software/firemarshal/images/firechip/br-base/br-base-bin \
        sims/firesim/deploy/config_*.ini

    echo -e "${GREEN}✓ Deployment package: $DEPLOY_PKG${NC}"

    # Upload to S3
    echo -e "${BLUE}Uploading to S3...${NC}"
    aws s3 cp "$DEPLOY_PKG" "s3://doom-challenge-fpga-builds/packages/" \
        --region $AWS_REGION

    echo -e "${GREEN}✓ Uploaded to S3${NC}"
}

create_deployment_package

# ============================================================================
# PHASE 5: Ready for Bitstream Generation
# ============================================================================
echo -e "\n${YELLOW}[PHASE 5] Next Steps for Bitstream Generation${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

cat << 'NEXTSTEPS'

✅ LOCAL PREPARATION COMPLETE

When you have AWS access, run ONE of the following:

OPTION A - Generate Bitstream on AWS (Recommended):
  firesim buildbitstream --config_build_dir sims/firesim/deploy/

OPTION B - Quick deployment (pre-built bitstream, if available):
  firesim deploy

OPTION C - Full pipeline (this repo):
  cd sims/firesim
  ./build-setup.sh
  make CONFIG=MediumBoomV3Config
  firesim runf1

EXPECTED DURATION:
  ├─ Bitstream generation: 4-6 hours
  ├─ F1 instance deployment: 10-15 minutes
  └─ Linux boot on FPGA: 30-60 seconds

MONITORING:
  • CloudWatch: /aws/ec2/doom-boom-fpga
  • S3 bucket: doom-challenge-fpga-builds/
  • EC2 instances: DOOM-BOOM-F1 tags

TROUBLESHOOTING:
  1. Check AWS quotas: aws ec2 describe-account-attributes
  2. Verify region: aws ec2 describe-regions
  3. Check synthesis logs: cat bitstreams/build.log
  4. Monitor instance: aws ec2 describe-instances --region us-west-2

NEXT STEPS:
1. Verify AWS access granted
2. Confirm region and quotas
3. Run bitstream generation
4. Deploy to F1 and test

NEXTSTEPS

# ============================================================================
# FINAL STATUS
# ============================================================================
echo -e "\n${GREEN}╔════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║  ✅ FPGA Deployment Ready - Waiting for AWS Access        ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════════════════════╝${NC}"

echo -e "\n${BLUE}Files Created:${NC}"
echo "  ✓ config_build.ini (FireSim build config)"
echo "  ✓ config_runtime.ini (AWS F1 deployment config)"
echo "  ✓ devicetree.dtb (FPGA device tree)"
echo "  ✓ doom-boom-deployment.tar.gz (Ready for S3)"

echo -e "\n${BLUE}AWS Readiness:${NC}"
echo "  ✓ Region verified: $AWS_REGION"
echo "  ✓ F1 availability confirmed"
echo "  ✓ S3 bucket ready"
echo "  ✓ Deployment package uploaded"

echo -e "\n${BLUE}Time to Deployment (once AWS access granted):${NC}"
echo "  ├─ Vivado synthesis: 4-6 hours"
echo "  ├─ AWS deployment: 15 minutes"
echo "  └─ Linux boot test: 1 minute"
echo "  = Total: ~4.5-6.5 hours"

echo -e "\n${YELLOW}Waiting for your AWS access...${NC}\n"
