// C interface to chipyard.accel.Int8TileEngine (RoCC custom0). See the
// header comment of generators/chipyard/src/main/scala/accel/Int8TileEngine.scala
// for the full semantics. All tile addresses must be 8-byte aligned and all
// strides multiples of 8. Commands are blocking inside the engine but the
// core does not stall on non-xd ones, so call acc_sync() before the core
// reads any result the engine stored.
#ifndef INT8_ACCEL_H
#define INT8_ACCEL_H
#include <stdint.h>

#define ACC_F_CFG   0
#define ACC_F_LDA   1
#define ACC_F_LDB   2
#define ACC_F_CLR   3
#define ACC_F_MAC   4
#define ACC_F_ST32  5
#define ACC_F_ST8   6
#define ACC_F_CYC   7
#define ACC_F_SYNC  8

// funct3 = xd<<2 | xs1<<1 | xs2
#define ACC_INSN_1(f, rs1) \
  asm volatile(".insn r 0x0b, 2, %1, x0, %0, x0" :: "r"(rs1), "i"(f) : "memory")
#define ACC_INSN_2(f, rs1, rs2) \
  asm volatile(".insn r 0x0b, 3, %2, x0, %0, %1" :: "r"(rs1), "r"(rs2), "i"(f) : "memory")
#define ACC_INSN_0(f) \
  asm volatile(".insn r 0x0b, 0, %0, x0, x0, x0" :: "i"(f) : "memory")

static inline void acc_cfg(uint64_t load_stride, uint64_t store_stride) {
  ACC_INSN_2(ACC_F_CFG, load_stride, store_stride);
}
static inline void acc_load_a(const void *p) { ACC_INSN_1(ACC_F_LDA, (uint64_t)p); }
static inline void acc_load_b(const void *p) { ACC_INSN_1(ACC_F_LDB, (uint64_t)p); }
static inline void acc_clear(void) { ACC_INSN_0(ACC_F_CLR); }
static inline void acc_mac(int transA, int transB) {
  uint64_t f = (transA ? 1 : 0) | (transB ? 2 : 0);
  ACC_INSN_1(ACC_F_MAC, f);
}
static inline void acc_store32(void *p) { ACC_INSN_1(ACC_F_ST32, (uint64_t)p); }
// shift: right-shift applied to int32 accumulators; relu: clamp negatives to 0
static inline void acc_store8(void *p, int shift, int relu) {
  uint64_t r = (uint64_t)(shift & 31) | (relu ? 0x100 : 0);
  ACC_INSN_2(ACC_F_ST8, (uint64_t)p, r);
}
static inline uint64_t acc_cycles(void) {
  uint64_t v;
  asm volatile(".insn r 0x0b, 4, %1, %0, x0, x0" : "=r"(v) : "i"(ACC_F_CYC) : "memory");
  asm volatile("mv %0, %0" : "+r"(v));
  return v;
}
static inline void acc_sync(void) {
  uint64_t v;
  asm volatile(".insn r 0x0b, 4, %1, %0, x0, x0" : "=r"(v) : "i"(ACC_F_SYNC) : "memory");
  // Consume the result: the core only stalls on an xd RoCC response when a
  // later instruction reads rd, so without a real use the "sync" would
  // return before the engine finished.
  asm volatile("mv %0, %0" : "+r"(v));
}
#endif
