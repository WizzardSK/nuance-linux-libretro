// AArch64 implementations of the NativeCodeCache::X86Emit_* primitives.
//
// This file is the ARM64 counterpart of the x86 emit primitive bodies in
// nativecodecache.cpp (which are #ifndef NUANCE_ARCH_ARM64 guarded out on ARM).
// Each X86Emit_* member translates the *semantics* of the x86 micro-op the JIT
// handlers requested onto a fixed a64 register set. See asmjit_a64_emit.h for
// the register model and condition-code mapping.
//
// Flag model: the handlers rely on host flags only in narrow, local idioms:
//   * CMP/SUB/ADD followed by Jcc/CMOVcc/SETcc      -> a64 NZCV via subs/adds
//   * multi-word ADD/ADC and SUB/SBB carry chains   -> a64 adds/adcs/subs/sbcs
//     (x86 SUB sets CF=borrow; a64 SUBS sets C=!borrow; a64 SBC subtracts !C,
//      so x86 SUB/SBB maps exactly onto a64 SUBS/SBC and ADD/ADC onto ADDS/ADC)
//   * RCR reg,2 to inject NUON CC carry-bit into host CF before ADC
// We therefore emit the flag-setting (S) forms for arithmetic so these compose.

#ifdef USE_ASMJIT
#ifdef NUANCE_ARCH_ARM64

#include "basetypes.h"
#include "mpe.h"
#include "NativeCodeCache.h"
#include "asmjit_a64_emit.h"

using namespace asmjit;
namespace NJ = NuanceJitA64;

// File-local shorthands.
#define AS (*a64As)
static const a64::Gp ADDR = NJ::kAddrScratch; // x16
static const a64::Gp T0   = NJ::kTmp0;         // x17
static const a64::Gp T1   = NJ::kTmp1;         // x15

// ===========================================================================
// Helpers
// ===========================================================================

// Materialize a 32-bit immediate into a W register.
static inline void movImm32(a64::Assembler& a, const a64::Gp& wreg, uint32 imm)
{
  a.mov(wreg, (uint64_t)imm);
}

// Load a sized value from [base+index*scale+disp] into wDst (zero-extended).
static inline void memLoad(a64::Assembler& a, const a64::Gp& wDst, uint32 size,
                           uintptr_t base, x86IndexReg index, x86ScaleVal scale, int32 disp)
{
  a64::Mem m = NJ::buildAddr(a, base, index, scale, disp);
  switch (size) {
    case 1: a.ldrb(wDst, m); break;
    case 2: a.ldrh(wDst, m); break;
    default: a.ldr(wDst, m); break;
  }
}

static inline void memStore(a64::Assembler& a, const a64::Gp& wSrc, uint32 size,
                            uintptr_t base, x86IndexReg index, x86ScaleVal scale, int32 disp)
{
  a64::Mem m = NJ::buildAddr(a, base, index, scale, disp);
  switch (size) {
    case 1: a.strb(wSrc, m); break;
    case 2: a.strh(wSrc, m); break;
    default: a.str(wSrc, m); break;
  }
}

static inline uint32 regSize(x86Reg r)
{
  return NJ::isReg8(r) ? 1 : NJ::isReg16(r) ? 2 : 4;
}

// Apply an ALU group op (x86 groupIndex) to W registers: wd = wd OP ws.
// 0=ADD 1=OR 2=ADC 3=SBB 4=AND 5=SUB 6=XOR 7=CMP 17=MOV
static void aluRR_w(a64::Assembler& a, uint8 gi, const a64::Gp& wd, const a64::Gp& ws)
{
  switch (gi) {
    case 0:  a.adds(wd, wd, ws); break;
    case 1:  a.orr (wd, wd, ws); break;
    case 2:  a.adcs(wd, wd, ws); break;
    case 3:  a.sbcs(wd, wd, ws); break;
    case 4:  a.ands(wd, wd, ws); break;
    case 5:  a.subs(wd, wd, ws); break;
    case 6:  a.eor (wd, wd, ws); break;
    case 7:  a.subs(a64::wzr, wd, ws); break; // CMP
    case 17: a.mov (wd, ws); break;
    default: assert(false); break;
  }
}

// ===========================================================================
// Group ALU dispatchers (reg/reg, reg/mem, mem/reg, imm/reg, imm/mem)
// ===========================================================================

void NativeCodeCache::X86Emit_Group1RR(const x86Reg regDest, const x86Reg regSrc, const uint8 groupIndex)
{
  aluRR_w(AS, groupIndex, NJ::w(regDest), NJ::w(regSrc));
}

// regDest OP= [mem]
void NativeCodeCache::X86Emit_Group1MR(const x86Reg regDest, const uintptr_t base, const x86IndexReg index, const x86ScaleVal scale, const int32 disp, const uint8 groupIndex)
{
  memLoad(AS, T1.w(), regSize(regDest), base, index, scale, disp);
  aluRR_w(AS, groupIndex, NJ::w(regDest), T1.w());
}

// [mem] OP= regSrc
void NativeCodeCache::X86Emit_Group1RM(const x86Reg regSrc, const uintptr_t base, const x86IndexReg index, const x86ScaleVal scale, const int32 disp, const uint8 groupIndex)
{
  const uint32 sz = regSize(regSrc);
  if (groupIndex == 7) { // CMP [mem], reg : no writeback
    memLoad(AS, T1.w(), sz, base, index, scale, disp);
    AS.subs(a64::wzr, T1.w(), NJ::w(regSrc));
    return;
  }
  memLoad(AS, T1.w(), sz, base, index, scale, disp);
  aluRR_w(AS, groupIndex, T1.w(), NJ::w(regSrc));
  memStore(AS, T1.w(), sz, base, index, scale, disp);
}

void NativeCodeCache::X86Emit_Group1IR(const intptr_t imm, const x86Reg regDest, const uint8 groupIndex)
{
  if ((uintptr_t)imm > (uintptr_t)INT32_MAX && (groupIndex == 0 || groupIndex == 5 || groupIndex == 17 || groupIndex == 7)) {
    // 64-bit pointer arithmetic (rare: host addresses)
    AS.mov(T0, (uint64_t)(uintptr_t)imm);
    a64::Gp xd = NJ::x(regDest);
    switch (groupIndex) {
      case 0:  AS.add(xd, xd, T0); break;
      case 5:  AS.sub(xd, xd, T0); break;
      case 7:  AS.subs(a64::xzr, xd, T0); break;
      case 17: AS.mov(xd, T0); break;
    }
    return;
  }
  if (groupIndex == 17) { movImm32(AS, NJ::w(regDest), (uint32)imm); return; }
  movImm32(AS, T0.w(), (uint32)imm);
  aluRR_w(AS, groupIndex, NJ::w(regDest), T0.w());
}

void NativeCodeCache::X86Emit_Group1IM(const int32 imm, const x86MemPtr ptrType, const uintptr_t base, const x86IndexReg index, const x86ScaleVal scale, const int32 disp, const uint8 groupIndex)
{
  const uint32 sz = NJ::ptrSize(ptrType);
  if (groupIndex == 17) { // MOV [mem], imm
    movImm32(AS, T1.w(), (uint32)imm);
    memStore(AS, T1.w(), sz, base, index, scale, disp);
    return;
  }
  memLoad(AS, T1.w(), sz, base, index, scale, disp);
  movImm32(AS, T0.w(), (uint32)imm);
  if (groupIndex == 7) { AS.subs(a64::wzr, T1.w(), T0.w()); return; } // CMP
  aluRR_w(AS, groupIndex, T1.w(), T0.w());
  memStore(AS, T1.w(), sz, base, index, scale, disp);
}

// ===========================================================================
// ALU public wrappers (delegate to Group dispatchers)
// ===========================================================================
#define ALU_WRAP(NAME, GI) \
  void NativeCodeCache::X86Emit_##NAME##RR(const x86Reg d, const x86Reg s){ X86Emit_Group1RR(d,s,GI); } \
  void NativeCodeCache::X86Emit_##NAME##MR(const x86Reg d, const uintptr_t b, const x86IndexReg i, const x86ScaleVal sc, const int32 dp){ X86Emit_Group1MR(d,b,i,sc,dp,GI); } \
  void NativeCodeCache::X86Emit_##NAME##RM(const x86Reg s, const uintptr_t b, const x86IndexReg i, const x86ScaleVal sc, const int32 dp){ X86Emit_Group1RM(s,b,i,sc,dp,GI); } \
  void NativeCodeCache::X86Emit_##NAME##IR(const intptr_t imm, const x86Reg d){ X86Emit_Group1IR(imm,d,GI); } \
  void NativeCodeCache::X86Emit_##NAME##IM(const int32 imm, const x86MemPtr p, const uintptr_t b, const x86IndexReg i, const x86ScaleVal sc, const int32 dp){ X86Emit_Group1IM(imm,p,b,i,sc,dp,GI); }

ALU_WRAP(ADD, 0)
ALU_WRAP(OR,  1)
ALU_WRAP(ADC, 2)
ALU_WRAP(SBB, 3)
ALU_WRAP(AND, 4)
ALU_WRAP(SUB, 5)
ALU_WRAP(XOR, 6)
ALU_WRAP(CMP, 7)
#undef ALU_WRAP

// 64-bit pointer variants used for host-pointer math.
void NativeCodeCache::X86Emit_ADDRR64(const x86Reg d, const x86Reg s){ AS.add(NJ::x(d), NJ::x(d), NJ::x(s)); }
void NativeCodeCache::X86Emit_MOVRR64(const x86Reg d, const x86Reg s){ AS.mov(NJ::x(d), NJ::x(s)); }

// ===========================================================================
// MOV family
// ===========================================================================
void NativeCodeCache::X86Emit_MOVRR(const x86Reg d, const x86Reg s){ AS.mov(NJ::w(d), NJ::w(s)); }
void NativeCodeCache::X86Emit_MOVMR(const x86Reg d, const uintptr_t b, const x86IndexReg i, const x86ScaleVal sc, const int32 dp){ memLoad(AS, NJ::w(d), regSize(d), b,i,sc,dp); }
void NativeCodeCache::X86Emit_MOVRM(const x86Reg s, const uintptr_t b, const x86IndexReg i, const x86ScaleVal sc, const int32 dp){ memStore(AS, NJ::w(s), regSize(s), b,i,sc,dp); }
void NativeCodeCache::X86Emit_MOVIR(const intptr_t imm, const x86Reg d){ movImm32(AS, NJ::w(d), (uint32)imm); }
void NativeCodeCache::X86Emit_MOVIR_Ptr(const uintptr_t addr, const x86Reg d){ AS.mov(NJ::x(d), (uint64_t)addr); }
void NativeCodeCache::X86Emit_MOVIM(const int32 imm, const x86MemPtr p, const uintptr_t b, const x86IndexReg i, const x86ScaleVal sc, const int32 dp)
{
  movImm32(AS, T1.w(), (uint32)imm);
  memStore(AS, T1.w(), NJ::ptrSize(p), b,i,sc,dp);
}

void NativeCodeCache::X86Emit_MOVZXRR(const x86Reg d, const x86Reg s)
{
  if (NJ::isReg8(s)) AS.uxtb(NJ::w(d), NJ::w(s)); else AS.uxth(NJ::w(d), NJ::w(s));
}
void NativeCodeCache::X86Emit_MOVSXRR(const x86Reg d, const x86Reg s)
{
  if (NJ::isReg8(s)) AS.sxtb(NJ::w(d), NJ::w(s)); else AS.sxth(NJ::w(d), NJ::w(s));
}
void NativeCodeCache::X86Emit_MOVZXMR(const x86Reg d, const x86MemPtr p, const uintptr_t b, const x86IndexReg i, const x86ScaleVal sc, const int32 dp)
{
  memLoad(AS, NJ::w(d), NJ::ptrSize(p), b,i,sc,dp); // ldrb/ldrh already zero-extend
}
void NativeCodeCache::X86Emit_MOVSXMR(const x86Reg d, const x86MemPtr p, const uintptr_t b, const x86IndexReg i, const x86ScaleVal sc, const int32 dp)
{
  a64::Mem m = NJ::buildAddr(AS, b,i,sc,dp);
  if (NJ::ptrSize(p) == 1) AS.ldrsb(NJ::w(d), m); else AS.ldrsh(NJ::w(d), m);
}

// LEA: compute effective address as a 32-bit value into d.
void NativeCodeCache::X86Emit_LEA(const x86Reg d, const uintptr_t base, const x86IndexReg index, const x86ScaleVal scale, const int32 disp)
{
  a64::Gp wd = NJ::w(d);
  const uint32 shift = (uint32)scale;
  // base component
  if (base == X86_NO_BASE)        AS.mov(wd, (uint64_t)0);
  else if (base > 7)              AS.mov(wd, (uint64_t)(uint32)base);
  else                           AS.mov(wd, a64::w(NJ::baseSlotToId((unsigned)base)));
  if (disp) { movImm32(AS, T0.w(), (uint32)disp); AS.add(wd, wd, T0.w()); }
  if (index != x86IndexReg::x86IndexReg_none) {
    a64::Gp wi = a64::w(NJ::baseSlotToId((unsigned)index));
    if (shift) AS.add(wd, wd, wi, a64::lsl(shift)); else AS.add(wd, wd, wi);
  }
}

// ===========================================================================
// Unary / misc GP
// ===========================================================================
void NativeCodeCache::X86Emit_INCR(const x86Reg r){ AS.add(NJ::w(r), NJ::w(r), (uint64_t)1); }   // x86 INC leaves CF untouched
void NativeCodeCache::X86Emit_DECR(const x86Reg r){ AS.sub(NJ::w(r), NJ::w(r), (uint64_t)1); }
void NativeCodeCache::X86Emit_NEGR(const x86Reg r){ AS.negs(NJ::w(r), NJ::w(r)); }
void NativeCodeCache::X86Emit_NOTR(const x86Reg r){ AS.mvn(NJ::w(r), NJ::w(r)); }
void NativeCodeCache::X86Emit_BSWAP(const x86Reg r){ AS.rev(NJ::w(r), NJ::w(r)); }
void NativeCodeCache::X86Emit_BSRRR(const x86Reg d, const x86Reg s)
{
  AS.clz(T0.w(), NJ::w(s));
  AS.mov(T1.w(), (uint64_t)31);
  AS.sub(NJ::w(d), T1.w(), T0.w()); // bsr = 31 - clz
}

void NativeCodeCache::X86Emit_TESTRR(const x86Reg d, const x86Reg s){ AS.tst(NJ::w(d), NJ::w(s)); }
void NativeCodeCache::X86Emit_TESTIR(const uint32 imm, const x86Reg s){ movImm32(AS, T0.w(), imm); AS.tst(NJ::w(s), T0.w()); }

// ===========================================================================
// Shifts / rotates (immediate)
// ===========================================================================
void NativeCodeCache::X86Emit_SHLIR(const x86Reg d, const uint8 n){ AS.lsl(NJ::w(d), NJ::w(d), n & 31); }
void NativeCodeCache::X86Emit_SALIR(const x86Reg d, const uint8 n){ AS.lsl(NJ::w(d), NJ::w(d), n & 31); }
void NativeCodeCache::X86Emit_SHRIR(const x86Reg d, const uint8 n){ AS.lsr(NJ::w(d), NJ::w(d), n & 31); }
void NativeCodeCache::X86Emit_SARIR(const x86Reg d, const uint8 n){ AS.asr(NJ::w(d), NJ::w(d), n & 31); }
void NativeCodeCache::X86Emit_RORIR(const x86Reg d, const uint8 n){ AS.ror(NJ::w(d), NJ::w(d), n & 31); }
void NativeCodeCache::X86Emit_ROLIR(const x86Reg d, const uint8 n){ AS.ror(NJ::w(d), NJ::w(d), (32 - (n & 31)) & 31); }

// RCR reg,n through carry. The Nuance handlers only use this to lift NUON CC
// bit (n-1) into the host carry flag for a following ADC; the rotated value is
// discarded. We set NZCV.C = bit(n-1) of reg (and rotate reg as a best effort).
void NativeCodeCache::X86Emit_RCRIR(const x86Reg d, const uint8 n)
{
  const uint8 bit = (uint8)((n - 1) & 31);
  AS.ubfx(T0.w(), NJ::w(d), bit, 1);   // T0 = target carry bit (0/1)
  AS.ror(NJ::w(d), NJ::w(d), n & 31);  // best-effort value (unused by handlers)
  AS.subs(a64::wzr, T0.w(), (uint64_t)1); // C = (T0>=1) = bit
}

// Shifts by CL (x86 ecx low 5 bits).
void NativeCodeCache::X86Emit_SHLRR(const x86Reg d){ AS.lslv(NJ::w(d), NJ::w(d), NJ::w(x86Reg::x86Reg_ecx)); }
void NativeCodeCache::X86Emit_SHRRR(const x86Reg d){ AS.lsrv(NJ::w(d), NJ::w(d), NJ::w(x86Reg::x86Reg_ecx)); }
void NativeCodeCache::X86Emit_SARRR(const x86Reg d){ AS.asrv(NJ::w(d), NJ::w(d), NJ::w(x86Reg::x86Reg_ecx)); }
void NativeCodeCache::X86Emit_RORRR(const x86Reg d){ AS.rorv(NJ::w(d), NJ::w(d), NJ::w(x86Reg::x86Reg_ecx)); }
void NativeCodeCache::X86Emit_ROLRR(const x86Reg d){ AS.neg(T0.w(), NJ::w(x86Reg::x86Reg_ecx)); AS.rorv(NJ::w(d), NJ::w(d), T0.w()); }

void NativeCodeCache::X86Emit_SHLRM(const x86MemPtr p, const uintptr_t b, const x86IndexReg i, const x86ScaleVal sc, const int32 dp)
{
  const uint32 sz = NJ::ptrSize(p);
  memLoad(AS, T1.w(), sz, b,i,sc,dp);
  AS.lslv(T1.w(), T1.w(), NJ::w(x86Reg::x86Reg_ecx));
  memStore(AS, T1.w(), sz, b,i,sc,dp);
}

// Double-precision shifts (immediate).
// SHRD d,s,n : d = (d >> n) | (s << (32-n))  == extr(d, s, d, n)
void NativeCodeCache::X86Emit_SHRDIRR(const x86Reg d, const x86Reg s, const uint8 n)
{
  AS.extr(NJ::w(d), NJ::w(s), NJ::w(d), n & 31);
}
// SHLD d,s,n : d = (d << n) | (s >> (32-n)) == extr(d, d, s, 32-n)
void NativeCodeCache::X86Emit_SHLDIRR(const x86Reg d, const x86Reg s, const uint8 n)
{
  AS.extr(NJ::w(d), NJ::w(d), NJ::w(s), (32 - (n & 31)) & 31);
}
// SHLD/SHRD by CL.
void NativeCodeCache::X86Emit_SHLDRRR(const x86Reg d, const x86Reg s)
{
  // d = (d << cl) | (s >> (32-cl))
  a64::Gp cl = NJ::w(x86Reg::x86Reg_ecx);
  AS.lslv(T0.w(), NJ::w(d), cl);
  AS.mov(T1.w(), (uint64_t)32); AS.sub(T1.w(), T1.w(), cl);
  AS.lsrv(T1.w(), NJ::w(s), T1.w());
  AS.orr(NJ::w(d), T0.w(), T1.w());
}
void NativeCodeCache::X86Emit_SHRDRRR(const x86Reg d, const x86Reg s)
{
  // d = (d >> cl) | (s << (32-cl))
  a64::Gp cl = NJ::w(x86Reg::x86Reg_ecx);
  AS.lsrv(T0.w(), NJ::w(d), cl);
  AS.mov(T1.w(), (uint64_t)32); AS.sub(T1.w(), T1.w(), cl);
  AS.lslv(T1.w(), NJ::w(s), T1.w());
  AS.orr(NJ::w(d), T0.w(), T1.w());
}

// ===========================================================================
// Multiply
// ===========================================================================
void NativeCodeCache::X86Emit_IMULRRR(const x86Reg d, const x86Reg s){ AS.mul(NJ::w(d), NJ::w(d), NJ::w(s)); }
void NativeCodeCache::X86Emit_IMULIRR(const x86Reg d, const int32 imm, const x86Reg s)
{
  movImm32(AS, T0.w(), (uint32)imm);
  AS.mul(NJ::w(d), NJ::w(s), T0.w());
}
// One-operand IMUL: edx:eax = eax * src (signed 32x32 -> 64).
void NativeCodeCache::X86Emit_IMULRR(const x86Reg s)
{
  AS.smull(T0, NJ::w(x86Reg::x86Reg_eax), NJ::w(s));  // T0(64) = eax*s
  AS.lsr(T1, T0, 32);
  AS.mov(NJ::w(x86Reg::x86Reg_eax), T0.w());           // low -> eax
  AS.mov(NJ::w(x86Reg::x86Reg_edx), T1.w());           // high -> edx
}
void NativeCodeCache::X86Emit_IMULMR(const x86MemPtr p, const uintptr_t b, const x86IndexReg i, const x86ScaleVal sc, const int32 dp)
{
  memLoad(AS, T1.w(), NJ::ptrSize(p), b,i,sc,dp);
  AS.smull(T0, NJ::w(x86Reg::x86Reg_eax), T1.w());
  AS.lsr(T1, T0, 32);
  AS.mov(NJ::w(x86Reg::x86Reg_eax), T0.w());
  AS.mov(NJ::w(x86Reg::x86Reg_edx), T1.w());
}

// ===========================================================================
// CMOVcc / SETcc
// ===========================================================================
#define CMOV_WRAP(NAME, CC) \
  void NativeCodeCache::X86Emit_CMOV##NAME##RR(const x86Reg d, const x86Reg s){ AS.csel(NJ::w(d), NJ::w(s), NJ::w(d), NJ::toCond(CC)); }
CMOV_WRAP(Z,   X86_CC_Z)
CMOV_WRAP(NZ,  X86_CC_NZ)
CMOV_WRAP(L,   X86_CC_L)
CMOV_WRAP(NL,  X86_CC_NL)
CMOV_WRAP(NLE, X86_CC_NLE)
CMOV_WRAP(NB,  X86_CC_NB)
CMOV_WRAP(NBE, X86_CC_NBE)
#undef CMOV_WRAP

// SETcc writes only the low 8 bits of the target register (x86 semantics).
#define SET_WRAP(NAME, CC) \
  void NativeCodeCache::X86Emit_SET##NAME##R(const x86Reg r){ AS.cset(T0.w(), NJ::toCond(CC)); AS.bfxil(NJ::w(r), T0.w(), 0, 8); }
SET_WRAP(Z, X86_CC_Z)
SET_WRAP(S, X86_CC_S)
SET_WRAP(O, X86_CC_O)
SET_WRAP(B, X86_CC_B)
#undef SET_WRAP

// ===========================================================================
// Branches / calls / block prologue-epilogue
// ===========================================================================
void NativeCodeCache::X86Emit_JCC_Label(const int8 conditionCode, const uint32 labelIndex)
{
  AS.b(NJ::toCond(conditionCode), AsmJit_GetLabel(labelIndex));
}
void NativeCodeCache::X86Emit_JMPI_Label(const uint32 labelIndex)
{
  AS.b(AsmJit_GetLabel(labelIndex));
}
void NativeCodeCache::X86Emit_RETN(uint16){ AS.ret(a64::x30); }

// __fastcall(ecx,edx) -> AAPCS64(x0,x1), result in eax. Pointer args are full
// 64-bit (handlers load them via MOVIR_Ptr into rcx/rdx == x20/x21).
void NativeCodeCache::X86Emit_CALLI(uintptr_t offset, uint16 /*seg*/)
{
  AS.mov(a64::x0, NJ::x(x86Reg::x86Reg_ecx));
  AS.mov(a64::x1, NJ::x(x86Reg::x86Reg_edx));
  AS.mov(ADDR, (uint64_t)offset);
  AS.blr(ADDR);
  AS.mov(NJ::w(x86Reg::x86Reg_eax), a64::w0); // return value -> eax
}

// PUSHAD/POPAD: save/restore the callee-saved a64 regs we map x86 GPs onto,
// plus the link register (block calls C helpers via CALLI).
//   eax=w19 ecx=w20 edx=w21 ebx=w22 ebp=w23 esi=w24 edi=w25  + x30(LR)
void NativeCodeCache::X86Emit_PUSHAD()
{
  AS.stp(a64::x19, a64::x20, a64::ptr_pre(a64::sp, -64));
  AS.stp(a64::x21, a64::x22, a64::ptr(a64::sp, 16));
  AS.stp(a64::x23, a64::x24, a64::ptr(a64::sp, 32));
  AS.stp(a64::x25, a64::x30, a64::ptr(a64::sp, 48));
}
void NativeCodeCache::X86Emit_POPAD()
{
  AS.ldp(a64::x21, a64::x22, a64::ptr(a64::sp, 16));
  AS.ldp(a64::x23, a64::x24, a64::ptr(a64::sp, 32));
  AS.ldp(a64::x25, a64::x30, a64::ptr(a64::sp, 48));
  AS.ldp(a64::x19, a64::x20, a64::ptr_post(a64::sp, 64));
}

// ===========================================================================
// SIMD (SSE -> NEON). x86 xmm0..7 -> a64 v0..7; scratch v16/v17.
// ===========================================================================
static inline a64::Mem simdAddr(a64::Assembler& a, uintptr_t b, x86IndexReg i, x86ScaleVal sc, int32 dp)
{ return NJ::buildAddr(a, b, i, sc, dp); }

void NativeCodeCache::X86Emit_PADDRR(const x86Reg d, const x86Reg s){ AS.add(NJ::v(d).s4(), NJ::v(d).s4(), NJ::v(s).s4()); }   // PADDD
void NativeCodeCache::X86Emit_PSUBDRR(const x86Reg d, const x86Reg s){ AS.sub(NJ::v(d).s4(), NJ::v(d).s4(), NJ::v(s).s4()); }
void NativeCodeCache::X86Emit_PANDRR(const x86Reg d, const x86Reg s){ AS.and_(NJ::v(d).b16(), NJ::v(d).b16(), NJ::v(s).b16()); }
void NativeCodeCache::X86Emit_PMULLDRR(const x86Reg d, const x86Reg s){ AS.mul(NJ::v(d).s4(), NJ::v(d).s4(), NJ::v(s).s4()); }
void NativeCodeCache::X86Emit_PHADDDRR(const x86Reg d, const x86Reg s){ AS.addp(NJ::v(d).s4(), NJ::v(d).s4(), NJ::v(s).s4()); }

// PSHUFB d,s : per byte, d[i] = (s[i]&0x80)?0 : d[s[i]&0x0F]
void NativeCodeCache::X86Emit_PSHUFBRR(const x86Reg d, const x86Reg s)
{
  a64::Vec ctl = NJ::v(s).b16();
  a64::Vec idx = a64::v(17).b16();
  a64::Vec tbl = a64::v(16).b16();
  AS.mov(tbl, NJ::v(d).b16());          // table = original d
  AS.movi(a64::v(18).b16(), 0x0F);
  AS.and_(idx, ctl, a64::v(18).b16());  // low nibble
  AS.sshr(a64::v(19).b16(), ctl, 7);    // 0xFF where bit7 set
  AS.movi(a64::v(20).b16(), 0x10);
  AS.and_(a64::v(19).b16(), a64::v(19).b16(), a64::v(20).b16()); // 0x10 where bit7
  AS.orr(idx, idx, a64::v(19).b16());   // >=16 -> TBL yields 0
  AS.tbl(NJ::v(d).b16(), tbl, idx);
}

// Packed dword shifts.
void NativeCodeCache::X86Emit_PSLDIR(const x86Reg d, const uint8 n){ AS.shl(NJ::v(d).s4(), NJ::v(d).s4(), n & 31); }
void NativeCodeCache::X86Emit_PSRADIR(const x86Reg d, const uint8 n){ AS.sshr(NJ::v(d).s4(), NJ::v(d).s4(), n & 31); }
void NativeCodeCache::X86Emit_PSLDRR(const x86Reg d, const x86Reg s)
{
  // shift count = low 64 bits of s (use lane0 32-bit), broadcast, ushl.
  AS.umov(T0.w(), NJ::v(s).s(0));
  AS.dup(a64::v(16).s4(), T0.w());
  AS.ushl(NJ::v(d).s4(), NJ::v(d).s4(), a64::v(16).s4());
}

// movd / movq / movss / movdq*
void NativeCodeCache::X86Emit_MOVDRR(const x86Reg d, const x86Reg s){ AS.fmov(NJ::v(d).s(), NJ::w(s)); }   // r32 -> xmm (zero upper)
void NativeCodeCache::X86Emit_MOVDRR2(const x86Reg d, const x86Reg s){ AS.fmov(NJ::w(d), NJ::v(s).s()); }  // xmm -> r32
void NativeCodeCache::X86Emit_MOVDRM(const x86Reg s, const uintptr_t b, const x86IndexReg i, const x86ScaleVal sc, const int32 dp){ AS.str(NJ::v(s).s(), simdAddr(AS,b,i,sc,dp)); }
void NativeCodeCache::X86Emit_MOVSSMR(const x86Reg d, const uintptr_t b, const x86IndexReg i, const x86ScaleVal sc, const int32 dp){ AS.ldr(NJ::v(d).s(), simdAddr(AS,b,i,sc,dp)); }
void NativeCodeCache::X86Emit_MOVQRM(const x86Reg s, const uintptr_t b, const x86IndexReg i, const x86ScaleVal sc, const int32 dp){ AS.str(NJ::v(s).d(), simdAddr(AS,b,i,sc,dp)); }
void NativeCodeCache::X86Emit_MOVQMR(const x86Reg d, const uintptr_t b, const x86IndexReg i, const x86ScaleVal sc, const int32 dp){ AS.ldr(NJ::v(d).d(), simdAddr(AS,b,i,sc,dp)); }
void NativeCodeCache::X86Emit_MOVDQURM(const x86Reg s, const uintptr_t b, const x86IndexReg i, const x86ScaleVal sc, const int32 dp){ AS.str(NJ::v(s).q(), simdAddr(AS,b,i,sc,dp)); }
void NativeCodeCache::X86Emit_MOVDQUMR(const x86Reg d, const uintptr_t b, const x86IndexReg i, const x86ScaleVal sc, const int32 dp){ AS.ldr(NJ::v(d).q(), simdAddr(AS,b,i,sc,dp)); }
void NativeCodeCache::X86Emit_MOVDQARM(const x86Reg s, const uintptr_t b, const x86IndexReg i, const x86ScaleVal sc, const int32 dp){ AS.str(NJ::v(s).q(), simdAddr(AS,b,i,sc,dp)); }
void NativeCodeCache::X86Emit_MOVDQAMR(const x86Reg d, const uintptr_t b, const x86IndexReg i, const x86ScaleVal sc, const int32 dp){ AS.ldr(NJ::v(d).q(), simdAddr(AS,b,i,sc,dp)); }

void NativeCodeCache::X86Emit_UNPCKLRR(const x86Reg d, const x86Reg s){ AS.zip1(NJ::v(d).s4(), NJ::v(d).s4(), NJ::v(s).s4()); }  // unpcklps
void NativeCodeCache::X86Emit_MOVLHRR(const x86Reg d, const x86Reg s){ AS.ins(NJ::v(d).d(1), NJ::v(s).d(0)); }                  // movlhps

// SHUFPS d,s,imm : d[0]=d[sel0] d[1]=d[sel1] d[2]=s[sel2] d[3]=s[sel3]
void NativeCodeCache::X86Emit_SHUFIR(const x86Reg d, const x86Reg s, const uint8 imm)
{
  a64::Vec td = a64::v(16), ts = a64::v(17);
  AS.mov(td.b16(), NJ::v(d).b16());
  AS.mov(ts.b16(), NJ::v(s).b16());
  AS.ins(NJ::v(d).s(0), td.s((imm >> 0) & 3));
  AS.ins(NJ::v(d).s(1), td.s((imm >> 2) & 3));
  AS.ins(NJ::v(d).s(2), ts.s((imm >> 4) & 3));
  AS.ins(NJ::v(d).s(3), ts.s((imm >> 6) & 3));
}

#undef AS
#endif // NUANCE_ARCH_ARM64
#endif // USE_ASMJIT
