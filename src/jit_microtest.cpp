// AArch64 JIT micro-harness: directly emits tiny native blocks through the
// NativeCodeCache::X86Emit_* primitives, runs them on the host, and compares
// the result against the x86 semantics computed in C++. This validates the a64
// backend (especially the flag model and carry chains) without needing a NUON
// ROM or the block-compilation path to engage. Triggered by NUANCE_JITTEST.
#ifdef USE_ASMJIT
#ifdef NUANCE_ARCH_ARM64

#include "basetypes.h"
#include <windows.h>            // VirtualAlloc shim (src/linux_compat.h) for the code buffer
#include "mpe.h"
#include "NativeCodeCache.h"
#include "X86EmitTypes.h"
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace {

using R = x86Reg;
constexpr auto NONE = x86IndexReg::x86IndexReg_none;
constexpr auto S1   = x86ScaleVal::x86Scale_1;
typedef void (*BlockFn)();

uint32 g_out[8];
int g_pass = 0, g_fail = 0;

// Emit PUSHAD + body + POPAD + RETN, write to the cache buffer, run it.
void runBlock(NativeCodeCache& cc, const std::function<void(NativeCodeCache&)>& body)
{
  for (int i = 0; i < 8; i++) g_out[i] = 0xCCCCCCCCu;
  uint8* ep = cc.GetEmitPointer();
  cc.AsmJit_BeginBlock();
  cc.X86Emit_PUSHAD();
  body(cc);
  cc.X86Emit_POPAD();
  cc.X86Emit_RETN(0);
  cc.AsmJit_EndBlock();
  ((BlockFn)ep)();
}

void store(NativeCodeCache& cc, R r, int slot)
{
  cc.X86Emit_MOVRM(r, (uintptr_t)&g_out[slot], NONE, S1, 0);
}

void check(const char* name, uint32 got, uint32 exp)
{
  if (got == exp) { g_pass++; }
  else { g_fail++; fprintf(stderr, "[jitmicro] FAIL %-14s got=%08x exp=%08x\n", name, got, exp); }
}

// Single-output ALU/op test: load a,b into eax,ecx, run op(eax,ecx), read eax.
void t_rr(NativeCodeCache& cc, const char* name, uint32 a, uint32 b, uint32 exp,
          void (NativeCodeCache::*op)(const x86Reg, const x86Reg))
{
  runBlock(cc, [&](NativeCodeCache& c){
    c.X86Emit_MOVIR((int32)a, R::x86Reg_eax);
    c.X86Emit_MOVIR((int32)b, R::x86Reg_ecx);
    (c.*op)(R::x86Reg_eax, R::x86Reg_ecx);
    store(c, R::x86Reg_eax, 0);
  });
  check(name, g_out[0], exp);
}

} // namespace

bool NuanceJit_RunMicroTests()
{
  NativeCodeCache cc;
  g_pass = g_fail = 0;
  fprintf(stderr, "[jitmicro] running a64 JIT primitive tests...\n");

  // ---- ALU reg/reg ----
  t_rr(cc, "ADDRR", 0x12345678, 0x11111111, 0x12345678u + 0x11111111u, &NativeCodeCache::X86Emit_ADDRR);
  t_rr(cc, "SUBRR", 0x90000000, 0x10000000, 0x80000000u, &NativeCodeCache::X86Emit_SUBRR);
  t_rr(cc, "ANDRR", 0x0F0F0F0F, 0x00FF00FF, 0x000F000Fu, &NativeCodeCache::X86Emit_ANDRR);
  t_rr(cc, "ORRR",  0x0F0F0F0F, 0x00FF00FF, 0x0FFF0FFFu, &NativeCodeCache::X86Emit_ORRR);
  t_rr(cc, "XORRR", 0x0F0F0F0F, 0x00FF00FF, 0x0FF00FF0u, &NativeCodeCache::X86Emit_XORRR);
  t_rr(cc, "IMULRRR", 0x00010002, 0x00000003, (0x00010002u * 0x00000003u), &NativeCodeCache::X86Emit_IMULRRR);

  // ---- ALU reg/imm ----
  runBlock(cc, [](NativeCodeCache& c){ c.X86Emit_MOVIR(0x1000, R::x86Reg_eax); c.X86Emit_ADDIR(0x234, R::x86Reg_eax); store(c, R::x86Reg_eax, 0); });
  check("ADDIR", g_out[0], 0x1234);
  runBlock(cc, [](NativeCodeCache& c){ c.X86Emit_MOVIR(0xFF, R::x86Reg_eax); c.X86Emit_ANDIR(0x0F, R::x86Reg_eax); store(c, R::x86Reg_eax, 0); });
  check("ANDIR", g_out[0], 0x0F);

  // ---- unary ----
  runBlock(cc, [](NativeCodeCache& c){ c.X86Emit_MOVIR(0x10, R::x86Reg_eax); c.X86Emit_INCR(R::x86Reg_eax); store(c, R::x86Reg_eax, 0); });
  check("INCR", g_out[0], 0x11);
  runBlock(cc, [](NativeCodeCache& c){ c.X86Emit_MOVIR(0x10, R::x86Reg_eax); c.X86Emit_DECR(R::x86Reg_eax); store(c, R::x86Reg_eax, 0); });
  check("DECR", g_out[0], 0x0F);
  runBlock(cc, [](NativeCodeCache& c){ c.X86Emit_MOVIR(5, R::x86Reg_eax); c.X86Emit_NEGR(R::x86Reg_eax); store(c, R::x86Reg_eax, 0); });
  check("NEGR", g_out[0], (uint32)(-5));
  runBlock(cc, [](NativeCodeCache& c){ c.X86Emit_MOVIR(0x0F0F0F0F, R::x86Reg_eax); c.X86Emit_NOTR(R::x86Reg_eax); store(c, R::x86Reg_eax, 0); });
  check("NOTR", g_out[0], 0xF0F0F0F0u);
  runBlock(cc, [](NativeCodeCache& c){ c.X86Emit_MOVIR(0x11223344, R::x86Reg_eax); c.X86Emit_BSWAP(R::x86Reg_eax); store(c, R::x86Reg_eax, 0); });
  check("BSWAP", g_out[0], 0x44332211u);

  // ---- shifts (immediate) ----
  runBlock(cc, [](NativeCodeCache& c){ c.X86Emit_MOVIR(0x00000001, R::x86Reg_eax); c.X86Emit_SHLIR(R::x86Reg_eax, 4); store(c, R::x86Reg_eax, 0); });
  check("SHLIR", g_out[0], 0x10);
  runBlock(cc, [](NativeCodeCache& c){ c.X86Emit_MOVIR(0x80000000, R::x86Reg_eax); c.X86Emit_SHRIR(R::x86Reg_eax, 4); store(c, R::x86Reg_eax, 0); });
  check("SHRIR", g_out[0], 0x08000000u);
  runBlock(cc, [](NativeCodeCache& c){ c.X86Emit_MOVIR(0x80000000, R::x86Reg_eax); c.X86Emit_SARIR(R::x86Reg_eax, 4); store(c, R::x86Reg_eax, 0); });
  check("SARIR", g_out[0], 0xF8000000u);
  runBlock(cc, [](NativeCodeCache& c){ c.X86Emit_MOVIR(0x00000001, R::x86Reg_eax); c.X86Emit_RORIR(R::x86Reg_eax, 4); store(c, R::x86Reg_eax, 0); });
  check("RORIR", g_out[0], 0x10000000u);
  runBlock(cc, [](NativeCodeCache& c){ c.X86Emit_MOVIR(0x10000000, R::x86Reg_eax); c.X86Emit_ROLIR(R::x86Reg_eax, 4); store(c, R::x86Reg_eax, 0); });
  check("ROLIR", g_out[0], 0x00000001u);

  // ---- shift by CL ----
  runBlock(cc, [](NativeCodeCache& c){ c.X86Emit_MOVIR(0x00000001, R::x86Reg_eax); c.X86Emit_MOVIR(8, R::x86Reg_ecx); c.X86Emit_SHLRR(R::x86Reg_eax); store(c, R::x86Reg_eax, 0); });
  check("SHLRR(cl=8)", g_out[0], 0x100);

  // ---- MOVZX / MOVSX (from 8-bit view of the same reg) ----
  runBlock(cc, [](NativeCodeCache& c){ c.X86Emit_MOVIR(0x123456FF, R::x86Reg_eax); c.X86Emit_MOVZXRR(R::x86Reg_edx, R::x86Reg_al); store(c, R::x86Reg_edx, 0); });
  check("MOVZXRR(al)", g_out[0], 0x000000FFu);
  runBlock(cc, [](NativeCodeCache& c){ c.X86Emit_MOVIR(0x12345680, R::x86Reg_eax); c.X86Emit_MOVSXRR(R::x86Reg_edx, R::x86Reg_al); store(c, R::x86Reg_edx, 0); });
  check("MOVSXRR(al)", g_out[0], 0xFFFFFF80u);

  // ---- double shift (immediate) ----
  // SHRD edx, eax, 8 : edx = (edx >> 8) | (eax << 24)
  runBlock(cc, [](NativeCodeCache& c){
    c.X86Emit_MOVIR(0xAABBCCDD, R::x86Reg_edx);
    c.X86Emit_MOVIR(0x11223344, R::x86Reg_eax);
    c.X86Emit_SHRDIRR(R::x86Reg_edx, R::x86Reg_eax, 8);
    store(c, R::x86Reg_edx, 0);
  });
  check("SHRDIRR", g_out[0], (0xAABBCCDDu >> 8) | (0x11223344u << 24));

  // ---- flags: SETcc after CMP / arithmetic ----
  // CMP eq -> ZF=1 ; CMP ne -> ZF=0
  runBlock(cc, [](NativeCodeCache& c){ c.X86Emit_MOVIR(7, R::x86Reg_eax); c.X86Emit_MOVIR(7, R::x86Reg_ecx); c.X86Emit_CMPRR(R::x86Reg_eax, R::x86Reg_ecx);
                                       c.X86Emit_MOVIR(0, R::x86Reg_edx); c.X86Emit_SETZR(R::x86Reg_edx); store(c, R::x86Reg_edx, 0); });
  check("SETZ(eq)", g_out[0] & 0xFF, 1);
  runBlock(cc, [](NativeCodeCache& c){ c.X86Emit_MOVIR(7, R::x86Reg_eax); c.X86Emit_MOVIR(9, R::x86Reg_ecx); c.X86Emit_CMPRR(R::x86Reg_eax, R::x86Reg_ecx);
                                       c.X86Emit_MOVIR(0, R::x86Reg_edx); c.X86Emit_SETZR(R::x86Reg_edx); store(c, R::x86Reg_edx, 0); });
  check("SETZ(ne)", g_out[0] & 0xFF, 0);
  // unsigned below: 1 < 2 -> CF set -> SETB=1 (tests x86 CF == !a64 C mapping)
  runBlock(cc, [](NativeCodeCache& c){ c.X86Emit_MOVIR(1, R::x86Reg_eax); c.X86Emit_MOVIR(2, R::x86Reg_ecx); c.X86Emit_CMPRR(R::x86Reg_eax, R::x86Reg_ecx);
                                       c.X86Emit_MOVIR(0, R::x86Reg_edx); c.X86Emit_SETBR(R::x86Reg_edx); store(c, R::x86Reg_edx, 0); });
  check("SETB(1<2)", g_out[0] & 0xFF, 1);
  runBlock(cc, [](NativeCodeCache& c){ c.X86Emit_MOVIR(5, R::x86Reg_eax); c.X86Emit_MOVIR(2, R::x86Reg_ecx); c.X86Emit_CMPRR(R::x86Reg_eax, R::x86Reg_ecx);
                                       c.X86Emit_MOVIR(0, R::x86Reg_edx); c.X86Emit_SETBR(R::x86Reg_edx); store(c, R::x86Reg_edx, 0); });
  check("SETB(5<2)", g_out[0] & 0xFF, 0);
  // sign: 3 - 5 < 0 -> SF=1
  runBlock(cc, [](NativeCodeCache& c){ c.X86Emit_MOVIR(3, R::x86Reg_eax); c.X86Emit_SUBIR(5, R::x86Reg_eax);
                                       c.X86Emit_MOVIR(0, R::x86Reg_edx); c.X86Emit_SETSR(R::x86Reg_edx); store(c, R::x86Reg_edx, 0); });
  check("SETS(3-5)", g_out[0] & 0xFF, 1);
  // signed overflow: 0x7FFFFFFF + 1 -> OF=1
  runBlock(cc, [](NativeCodeCache& c){ c.X86Emit_MOVIR(0x7FFFFFFF, R::x86Reg_eax); c.X86Emit_MOVIR(1, R::x86Reg_ecx); c.X86Emit_ADDRR(R::x86Reg_eax, R::x86Reg_ecx);
                                       c.X86Emit_MOVIR(0, R::x86Reg_edx); c.X86Emit_SETOR(R::x86Reg_edx); store(c, R::x86Reg_edx, 0); });
  check("SETO(ovf)", g_out[0] & 0xFF, 1);

  // ---- CMOVcc ----
  // CMOVZ taken (eq) : edx becomes eax
  runBlock(cc, [](NativeCodeCache& c){ c.X86Emit_MOVIR(1, R::x86Reg_eax); c.X86Emit_MOVIR(1, R::x86Reg_ecx); c.X86Emit_CMPRR(R::x86Reg_eax, R::x86Reg_ecx);
                                       c.X86Emit_MOVIR(0xDEAD, R::x86Reg_edx); c.X86Emit_MOVIR(0xBEEF, R::x86Reg_ebx); c.X86Emit_CMOVZRR(R::x86Reg_edx, R::x86Reg_ebx); store(c, R::x86Reg_edx, 0); });
  check("CMOVZ(taken)", g_out[0], 0xBEEF);
  runBlock(cc, [](NativeCodeCache& c){ c.X86Emit_MOVIR(1, R::x86Reg_eax); c.X86Emit_MOVIR(2, R::x86Reg_ecx); c.X86Emit_CMPRR(R::x86Reg_eax, R::x86Reg_ecx);
                                       c.X86Emit_MOVIR(0xDEAD, R::x86Reg_edx); c.X86Emit_MOVIR(0xBEEF, R::x86Reg_ebx); c.X86Emit_CMOVZRR(R::x86Reg_edx, R::x86Reg_ebx); store(c, R::x86Reg_edx, 0); });
  check("CMOVZ(not taken)", g_out[0], 0xDEAD);

  // ---- carry chains (the riskiest part of the flag model) ----
  // 64-bit add: (0:0xFFFFFFFF) + (0:1) = (1:0)
  runBlock(cc, [](NativeCodeCache& c){
    c.X86Emit_MOVIR((int32)0xFFFFFFFF, R::x86Reg_eax); c.X86Emit_MOVIR(1, R::x86Reg_ecx); c.X86Emit_ADDRR(R::x86Reg_eax, R::x86Reg_ecx); // lo, sets CF
    c.X86Emit_MOVIR(0, R::x86Reg_edx); c.X86Emit_MOVIR(0, R::x86Reg_ebx); c.X86Emit_ADCRR(R::x86Reg_edx, R::x86Reg_ebx);                  // hi + carry
    store(c, R::x86Reg_eax, 0); store(c, R::x86Reg_edx, 1);
  });
  check("ADD/ADC lo", g_out[0], 0x00000000u);
  check("ADD/ADC hi", g_out[1], 0x00000001u);
  // 64-bit sub: (5:0) - (0:1) = (4:0xFFFFFFFF) -- tests x86 borrow vs a64 carry
  runBlock(cc, [](NativeCodeCache& c){
    c.X86Emit_MOVIR(0, R::x86Reg_eax); c.X86Emit_MOVIR(1, R::x86Reg_ecx); c.X86Emit_SUBRR(R::x86Reg_eax, R::x86Reg_ecx); // lo, sets borrow
    c.X86Emit_MOVIR(5, R::x86Reg_edx); c.X86Emit_MOVIR(0, R::x86Reg_ebx); c.X86Emit_SBBRR(R::x86Reg_edx, R::x86Reg_ebx); // hi - borrow
    store(c, R::x86Reg_eax, 0); store(c, R::x86Reg_edx, 1);
  });
  check("SUB/SBB lo", g_out[0], 0xFFFFFFFFu);
  check("SUB/SBB hi", g_out[1], 0x00000004u);

  fprintf(stderr, "[jitmicro] DONE: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail == 0;
}

#endif // NUANCE_ARCH_ARM64
#endif // USE_ASMJIT
