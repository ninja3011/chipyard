#!/bin/bash
# AWS Pre-flight Check for FPGA Deployment
# Run this BEFORE and AFTER AWS access is granted

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

PASS=0
FAIL=0
WARN=0

check_pass() {
    echo -e "${GREEN}✓${NC} $1"
    ((PASS++))
}

check_fail() {
    echo -e "${RED}✗${NC} $1"
    ((FAIL++))
}

check_warn() {
    echo -e "${YELLOW}⚠${NC} $1"
    ((WARN++))
}

echo -e "${BLUE}╔════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║     AWS Pre-flight Check for FPGA Deployment              ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════════╝${NC}"

# ============================================================================
# LOCAL ENVIRONMENT CHECKS
# ============================================================================
echo -e "\n${YELLOW}[LOCAL] Environment Setup${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if [ -d "/home/ninadjangle/chipyard" ]; then
    check_pass "Chipyard directory exists"
else
    check_fail "Chipyard directory not found"
fi

if [ -f "/home/ninadjangle/chipyard/sims/verilator/simulator-chipyard.harness-MediumBoomV3Config" ]; then
    check_pass "BOOM simulator compiled"
    BOOM_SIZE=$(du -h /home/ninadjangle/chipyard/sims/verilator/simulator-chipyard.harness-MediumBoomV3Config | cut -f1)
    echo "           Size: $BOOM_SIZE"
else
    check_fail "BOOM simulator not found"
fi

if [ -f "/home/ninadjangle/chipyard/software/firemarshal/images/firechip/br-base/br-base-bin" ]; then
    check_pass "Linux kernel image available"
else
    check_fail "Linux kernel image not found"
fi

if [ -f "/home/ninadjangle/chipyard/fpga-build/devicetree.dtb" ]; then
    check_pass "Device tree compiled"
else
    check_warn "Device tree not compiled (will be generated)"
fi

# ============================================================================
# DEPLOYMENT SCRIPTS
# ============================================================================
echo -e "\n${YELLOW}[SCRIPTS] Deployment Infrastructure${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if [ -x "/home/ninadjangle/chipyard/scripts/fpga-deploy.sh" ]; then
    check_pass "fpga-deploy.sh is executable"
else
    check_fail "fpga-deploy.sh not executable"
fi

if [ -f "/home/ninadjangle/chipyard/sims/firesim/deploy/config_build.ini" ]; then
    check_pass "FireSim build config exists"
else
    check_fail "FireSim build config missing"
fi

if [ -f "/home/ninadjangle/chipyard/sims/firesim/deploy/config_runtime.ini" ]; then
    check_pass "FireSim runtime config exists"
else
    check_fail "FireSim runtime config missing"
fi

# ============================================================================
# DOCUMENTATION
# ============================================================================
echo -e "\n${YELLOW}[DOCS] Documentation Ready${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if [ -f "/home/ninadjangle/chipyard/FPGA-DEPLOYMENT.md" ]; then
    check_pass "FPGA deployment guide available"
else
    check_fail "FPGA deployment guide missing"
fi

if [ -f "/home/ninadjangle/chipyard/CLAUDE.md" ]; then
    check_pass "Architecture specification available"
else
    check_fail "Architecture specification missing"
fi

# ============================================================================
# AWS CLI CHECKS (if credentials configured)
# ============================================================================
echo -e "\n${YELLOW}[AWS] Credentials & Access${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if command -v aws &> /dev/null; then
    check_pass "AWS CLI installed"
    AWS_VERSION=$(aws --version)
    echo "           $AWS_VERSION"

    # Check credentials
    if aws sts get-caller-identity &> /dev/null 2>&1; then
        check_pass "AWS credentials valid"

        ACCOUNT_ID=$(aws sts get-caller-identity --query Account --output text 2>/dev/null)
        ARN=$(aws sts get-caller-identity --query Arn --output text 2>/dev/null)
        echo "           Account: $ACCOUNT_ID"
        echo "           ARN: $ARN"

        # Check region
        REGION=$(aws configure get region)
        if [ -z "$REGION" ]; then
            check_warn "Default region not set (use: aws configure)"
        else
            check_pass "Default region configured: $REGION"
        fi

        # Check F1 availability
        F1_COUNT=$(aws ec2 describe-instance-types \
            --filters "Name=instance-type,Values=f1.2xlarge" \
            --region us-west-2 \
            --query 'InstanceTypes | length' \
            --output text 2>/dev/null || echo "ERROR")

        if [ "$F1_COUNT" != "ERROR" ] && [ "$F1_COUNT" -gt 0 ]; then
            check_pass "F1 instances available in us-west-2"
        else
            check_fail "Cannot query F1 availability (permissions issue?)"
        fi

        # Check S3 bucket
        BUCKET_NAME="doom-challenge-fpga-builds"
        if aws s3 ls "s3://$BUCKET_NAME" &> /dev/null 2>&1; then
            check_pass "S3 bucket exists: $BUCKET_NAME"
        else
            check_warn "S3 bucket doesn't exist (will be created during deploy)"
        fi

    else
        check_fail "AWS credentials not valid or not configured"
        echo "           Run: aws configure"
    fi
else
    check_fail "AWS CLI not installed"
    echo "           Install: pip install awscli"
fi

# ============================================================================
# PYTHON & DEPENDENCIES
# ============================================================================
echo -e "\n${YELLOW}[PYTHON] Dependencies${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if command -v python3 &> /dev/null; then
    check_pass "Python 3 installed"
    PYTHON_VERSION=$(python3 --version)
    echo "           $PYTHON_VERSION"
else
    check_fail "Python 3 not found"
fi

# Check for boto3 (AWS SDK)
if python3 -c "import boto3" &> /dev/null 2>&1; then
    check_pass "boto3 (AWS SDK) installed"
else
    check_warn "boto3 not installed (needed for advanced features)"
fi

# ============================================================================
# FIRESIM CHECKS
# ============================================================================
echo -e "\n${YELLOW}[FIRESIM] Setup Status${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if [ -d "/home/ninadjangle/chipyard/sims/firesim" ]; then
    check_pass "FireSim directory exists"

    if [ -f "/home/ninadjangle/chipyard/sims/firesim/build-setup.sh" ]; then
        check_pass "FireSim setup script available"
    else
        check_warn "FireSim setup script not found (submodule may not be initialized)"
    fi
else
    check_fail "FireSim directory not found"
fi

# ============================================================================
# SUMMARY
# ============================================================================
echo -e "\n${BLUE}╔════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║                      SUMMARY                               ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════════╝${NC}"

TOTAL=$((PASS + FAIL + WARN))

echo -e "\n${GREEN}Passed:${NC}  $PASS/$TOTAL"
echo -e "${RED}Failed:${NC}  $FAIL/$TOTAL"
echo -e "${YELLOW}Warnings:${NC} $WARN/$TOTAL"

if [ $FAIL -eq 0 ]; then
    echo -e "\n${GREEN}✅ ALL CHECKS PASSED${NC}"

    if aws sts get-caller-identity &> /dev/null 2>&1; then
        echo -e "\n${GREEN}🚀 READY FOR FPGA DEPLOYMENT!${NC}"
        echo -e "\nNext step:"
        echo -e "  bash /home/ninadjangle/chipyard/scripts/fpga-deploy.sh"
    else
        echo -e "\n${YELLOW}⏳ LOCAL READY - WAITING FOR AWS ACCESS${NC}"
        echo -e "\nOnce you have AWS access:"
        echo -e "  1. aws configure"
        echo -e "  2. Run this script again"
        echo -e "  3. bash /home/ninadjangle/chipyard/scripts/fpga-deploy.sh"
    fi
else
    echo -e "\n${RED}❌ SOME CHECKS FAILED${NC}"
    echo -e "\nPlease fix the above issues before proceeding."
fi

echo ""
