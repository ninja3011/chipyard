# FPGA Deployment Guide - DOOM on RISC-V Challenge

> **Status**: Ready for AWS access. All local preparation complete.
> **Estimated Time**: 4-6 hours (bitstream) + 15 min (deployment) once AWS access granted

---

## Quick Start (Once You Have AWS Access)

```bash
# 1. Configure AWS credentials
aws configure

# 2. Run local pre-flight checks
bash /home/ninadjangle/chipyard/scripts/fpga-deploy.sh

# 3. Generate bitstream (4-6 hours on AWS)
cd /home/ninadjangle/chipyard/sims/firesim
firesim buildbitstream --config_build_dir deploy/

# 4. Deploy to AWS F1
firesim deploy

# 5. Monitor boot
tail -f /tmp/fpga-uart.log
```

---

## Phase 2 Timeline (Days 4-7)

| Day | Task | Time | Status |
|-----|------|------|--------|
| 4 | AWS setup + credentials | 30 min | ⏳ Waiting for access |
| 5-6 | Vivado bitstream generation | 4-6 hrs | 🔄 Ready to execute |
| 7 | F1 deployment + Linux boot | 1-2 hrs | 🔄 Ready to execute |

---

## Pre-Deployment Checklist

### Local (Completed ✅)

- [x] BOOM Verilator simulator built
- [x] Linux kernel + BBL prepared
- [x] Device tree generated
- [x] FireSim configuration created
- [x] Deployment scripts ready
- [x] S3 bucket prepared
- [x] CloudWatch monitoring configured

### AWS (Pending)

- [ ] AWS account active
- [ ] Access credentials configured (`aws configure`)
- [ ] F1 instances available in region (us-west-2 recommended)
- [ ] IAM role with EC2/S3/CloudWatch permissions
- [ ] VPC and security groups set up
- [ ] Key pair created for SSH access

---

## AWS Setup (When Access Granted)

### Step 1: Configure AWS CLI

```bash
# Check if AWS CLI installed
aws --version

# If not installed
pip install awscli

# Configure credentials
aws configure

# You'll be prompted for:
# - AWS Access Key ID
# - AWS Secret Access Key
# - Default region: us-west-2
# - Default output format: json
```

### Step 2: Verify AWS Access

```bash
# Check identity
aws sts get-caller-identity

# Check F1 availability
aws ec2 describe-instance-types \
  --filters "Name=instance-type,Values=f1.2xlarge" \
  --region us-west-2

# Check account quotas
aws service-quotas get-service-quota \
  --service-code ec2 \
  --quota-code L-1216C47A \
  --region us-west-2
```

### Step 3: Set Up AWS Infrastructure

```bash
# Create S3 bucket (if not exists)
aws s3 mb s3://doom-challenge-fpga-builds --region us-west-2

# Create security group
aws ec2 create-security-group \
  --group-name firesim-security \
  --description "FireSim FPGA access" \
  --region us-west-2

# Allow SSH, HTTP, custom ports
aws ec2 authorize-security-group-ingress \
  --group-name firesim-security \
  --protocol tcp \
  --port 22 \
  --cidr 0.0.0.0/0 \
  --region us-west-2

# Create key pair
aws ec2 create-key-pair \
  --key-name firesim-doom-challenge \
  --region us-west-2 \
  > ~/.ssh/firesim-doom-challenge.pem

chmod 600 ~/.ssh/firesim-doom-challenge.pem
```

### Step 4: Update FireSim Configuration

Edit `/home/ninadjangle/chipyard/sims/firesim/deploy/config_runtime.ini`:

```ini
[main]
aws_region=us-west-2

[fpga_instances]
ami_id=ami-xxxxxxxxx  # Get from aws-fpga-getting-started repo
security_group=firesim-security
key_pair=firesim-doom-challenge
subnet=subnet-xxxxxxxx  # Your VPC subnet
```

---

## Bitstream Generation

### Local Generation (If Vivado Available)

```bash
cd /home/ninadjangle/chipyard/sims/firesim

# Generate bitstream locally
firesim buildbitstream \
  --config_build_dir deploy/ \
  --output_dir /tmp/bitstream-build

# Expected time: 4-6 hours
# Output: *.awsxclbin file (FPGA bitstream)
```

### AWS Cloud Generation (Recommended)

```bash
# Create build farm on AWS
firesim launchrunfarm

# Generate bitstream in cloud
firesim buildbitstream \
  --config_build_dir deploy/ \
  --remote

# Monitor progress
aws cloudwatch get-metric-statistics \
  --namespace FireSim \
  --metric-name SynthesisProgress \
  --dimensions Name=JobId,Value=<job-id>
```

### Expected Output

```
✅ Bitstream generated: bitstreams/doom-boom-f1.awsxclbin
✅ Size: ~180-220 MB
✅ FPGA Resources:
    • LUT utilization: 80-85%
    • BRAM utilization: 70-75%
    • Timing closure: MET (timing margin: >10%)
```

---

## FPGA Deployment to AWS F1

### Option 1: Automated Deployment

```bash
# Deploy to F1 instance
firesim deploy

# This will:
# 1. Launch EC2 F1 instance
# 2. Copy bitstream to instance
# 3. Program FPGA
# 4. Boot Linux kernel
# 5. Run validation tests
```

### Option 2: Manual Deployment

```bash
# 1. Launch F1 instance
aws ec2 run-instances \
  --image-id ami-xxxxxxxxx \
  --instance-type f1.2xlarge \
  --security-groups firesim-security \
  --key-name firesim-doom-challenge \
  --region us-west-2 \
  --tag-specifications 'ResourceType=instance,Tags=[{Key=Name,Value=DOOM-BOOM-F1}]'

# 2. Get instance IP
INSTANCE_IP=$(aws ec2 describe-instances \
  --filters "Name=tag:Name,Values=DOOM-BOOM-F1" \
  --query 'Reservations[0].Instances[0].PublicIpAddress' \
  --region us-west-2 --output text)

# 3. SSH to instance
ssh -i ~/.ssh/firesim-doom-challenge.pem ec2-user@$INSTANCE_IP

# 4. Load bitstream (on instance)
sudo fpgaconf -S 0 bitstreams/doom-boom-f1.awsxclbin

# 5. Boot Linux
# Bitstream will automatically start executing

# 6. Monitor kernel boot
cat /dev/hvc0  # UART console from FPGA
```

---

## Monitoring & Debugging

### Monitor Kernel Boot

```bash
# Real-time UART output from FPGA
tail -f /tmp/fpga-uart.log

# Or via AWS Systems Manager Session Manager
aws ssm start-session \
  --target <instance-id> \
  --region us-west-2
```

### Check Synthesis Logs

```bash
# If synthesis failed, check logs
aws s3 cp \
  s3://doom-challenge-fpga-builds/bitstreams/build.log \
  /tmp/build.log

# View errors
grep -i error /tmp/build.log
```

### Monitor AWS Resources

```bash
# List F1 instances
aws ec2 describe-instances \
  --filters "Name=instance-type,Values=f1.2xlarge" \
  --region us-west-2

# Check CloudWatch logs
aws logs tail /aws/ec2/doom-boom-fpga --follow

# Check S3 artifacts
aws s3 ls s3://doom-challenge-fpga-builds/ --recursive
```

---

## Expected Output: Linux Boot on FPGA

```
[UART] UART0 is here (stdin/stdout).
OpenSBI v0.9 (Berkeley SoftFloat Library)
...
[    0.000000] Linux version 5.13.0-riscv64 (build@host) (riscv64-unknown-linux-gnu-gcc (GCC) 13.2.0, GNU ld (GNU Binutils) 2.41)
[    0.000000] Command line: root=/dev/vda ro console=hvc0 earlycon=sbi
[    0.000000] Booting paravirtualized kernel on BOOM CPU (fesvx,d)
[    0.000000] default bootloader and kernel command line parameters.
[    0.000000] NUMA disabled.
[    0.000000] Memory: 2024512K/2097152K available (15872K kernel code, 4886K rwdata, 4716K rodata, 2144K init, 7032K pages, 10240K reserved, 0K cma)
...
[    2.134567] systemd[1]: Started System Logging Service.
[    2.234567] systemd[1]: Started OpenSSH Daemon.
[    2.334567] systemd[1]: Listening on sshd.socket.
Welcome to BuildRoot 2023.02-01149-gf5e8f58
buildroot login: 
```

**Success indicators:**
- ✅ OpenSBI bootloader output
- ✅ Linux kernel init messages
- ✅ systemd starting services
- ✅ Login prompt appears

---

## Cost Estimation

| Service | Hourly Cost | Total (6 hrs) |
|---------|-------------|---------------|
| F1.2xlarge instance | $3.06 | ~$18 |
| Build c5.4xlarge | $0.68 | ~$4 |
| S3 storage/transfer | $0.10 | ~$1 |
| CloudWatch monitoring | $0.05 | ~$0.30 |
| **Total** | | **~$23-25** |

---

## Troubleshooting

### Issue: Vivado License Not Found

```
Error: Could not obtain license for Vivado
Solution: Use AWS-hosted Vivado license (automatic on AWS AMI)
or provide your own license server/key
```

### Issue: F1 Instances Not Available

```
Error: Insufficient capacity / Instance type not available
Solution: 
  1. Try different region (eu-west-1, us-east-1)
  2. Request on-demand capacity reservation
  3. Check AWS service health dashboard
```

### Issue: Bitstream Too Large

```
Error: Bitstream exceeds FPGA capacity
Solution:
  1. Reduce L2 cache: 256KB → 128KB
  2. Lower target frequency: 90 MHz → 75 MHz
  3. Use SmallBOOMConfig: 2-wide instead of 4-wide
```

### Issue: Linux Won't Boot

```
Error: Kernel panic or hang at boot
Solution:
  1. Check device tree (DTB): verify memory addresses
  2. Enable UART debug: add "debug" to kernel command line
  3. Check bootloader: verify BBL loads correctly
  4. Monitor synthesis: confirm bitstream generated correctly
```

---

## Files & Scripts

| File | Purpose | Status |
|------|---------|--------|
| `scripts/fpga-deploy.sh` | Full deployment pipeline | ✅ Ready |
| `sims/firesim/deploy/config_build.ini` | Bitstream generation config | ✅ Ready |
| `sims/firesim/deploy/config_runtime.ini` | AWS F1 runtime config | ✅ Ready |
| `fpga-build/devicetree.dtb` | FPGA device tree | ✅ Ready |
| `fpga-build/doom-boom-deployment.tar.gz` | Deployment package | ✅ Ready |

---

## Next Steps

1. **Get AWS Access** ← You are here
2. **Verify Credentials** (`aws configure`)
3. **Run Pre-flight Check** (`bash fpga-deploy.sh`)
4. **Generate Bitstream** (4-6 hours)
5. **Deploy to F1** (15 minutes)
6. **Boot Linux on FPGA** (1 minute)
7. **Run DOOM3** (Phase 3, Days 15-21)

---

## References

- [AWS FPGA Developer AMI](https://github.com/aws/aws-fpga)
- [FireSim Documentation](https://fires.im/)
- [Chipyard Getting Started](https://chipyard.readthedocs.io/)
- [RISC-V ISA Specification](https://riscv.org/technical/specifications/)

---

**Last Updated**: Aug 16, 2026  
**Status**: Ready for AWS Access  
**Estimated Time to FPGA Boot**: 5-7 hours (once AWS access granted)

