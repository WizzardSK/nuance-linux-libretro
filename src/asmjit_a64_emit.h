// asmjit AArch64 (a64) wrapper for the NuanceResurrection JIT.
//
// This is the ARM64 counterpart of asmjit_emit.h (which targets x86-64).
// The JIT handlers in Emit*.cpp describe each NUON instruction as a sequence
// of x86 micro-ops via the NativeCodeCache::X86Emit_* interface. On x86-64
// those map 1:1 onto asmjit's x86 assembler. On AArch64 we instead translate
// the *semantics* of each x86 micro-op onto a fixed set of a64 registers.
//
// Register model (see PUSHAD/POPAD in nativecodecache_a64.cpp):
//   x86 scratch GP regs are mapped to callee-saved a64 W/X registers so they
//   survive C helper calls (X86Emit_CALLI) automatically:
//     eax -> w19   ecx -> w20   edx -> w21   ebx -> w22
//     ebp -> w23   esi -> w24   edi -> w25
//   esp is the host stack pointer (never used as a scratch by the handlers).
//   x16 (IP0) / x17 (IP1) are intra-procedure scratch used to materialize
//   absolute addresses and large immediates (the role x86 r15 plays).
//   x86 xmm0..xmm7 -> a64 v0..v7 (128-bit NEON).
#ifndef ASMJIT_A64_EMIT_H
#define ASMJIT_A64_EMIT_H

#ifdef USE_ASMJIT
#ifdef NUANCE_ARCH_ARM64

// X11/Xlib.h defines "Bool" as a macro which conflicts with asmjit::Type::Bool
#ifdef Bool
#undef Bool
#endif

// Use the a64 backend header directly: the umbrella <asmjit/asmjit.h> is
// deprecated and only pulls in core + x86 (never the AArch64 backend), which
// left a64::Gp/Vec/Mem/Assembler undefined.
#include <asmjit/a64.h>
#include "X86EmitTypes.h"
#include <cstdint>
#include <cassert>

#define MAX_ASMJIT_LABELS 64

namespace NuanceJitA64 {

using namespace asmjit;

// Intra-procedure scratch registers (caller-clobbered, never part of the
// mapped x86 register set) used to build addresses / large immediates.
static constexpr a64::Gp kAddrScratch = a64::x16; // address materialization
static constexpr a64::Gp kTmp0        = a64::x17; // generic 64-bit temp
static constexpr a64::Gp kTmp1        = a64::x15; // second 32/64-bit temp

// Map an x86Reg enum value to the a64 register id that backs it.
// 8/16/32-bit x86 views all share the same underlying a64 register; the
// width is selected by returning a W (32-bit) or X (64-bit) view.
inline uint32_t x86RegId(x86Reg reg)
{
    switch (reg) {
        case x86Reg::x86Reg_al: case x86Reg::x86Reg_ax: case x86Reg::x86Reg_eax: return 19;
        case x86Reg::x86Reg_cl: case x86Reg::x86Reg_cx: case x86Reg::x86Reg_ecx: return 20;
        case x86Reg::x86Reg_dl: case x86Reg::x86Reg_dx: case x86Reg::x86Reg_edx: return 21;
        case x86Reg::x86Reg_bl: case x86Reg::x86Reg_bx: case x86Reg::x86Reg_ebx: return 22;
        case x86Reg::x86Reg_ah: case x86Reg::x86Reg_bp: case x86Reg::x86Reg_ebp: return 23;
        case x86Reg::x86Reg_ch: case x86Reg::x86Reg_si: case x86Reg::x86Reg_esi: return 24;
        case x86Reg::x86Reg_dh: case x86Reg::x86Reg_di: case x86Reg::x86Reg_edi: return 25;
        // sp/esp would be id-for-stack; the handlers never use it as a scratch.
        case x86Reg::x86Reg_sp: case x86Reg::x86Reg_esp: return 26;
        default: return 19;
    }
}

inline bool isReg8(x86Reg reg)  { return reg >= x86Reg::x86Reg_al && reg <= x86Reg::x86Reg_bh; }
inline bool isReg16(x86Reg reg) { return reg >= x86Reg::x86Reg_ax && reg <= x86Reg::x86Reg_di; }
inline bool isReg32(x86Reg reg) { return reg >= x86Reg::x86Reg_eax && reg <= x86Reg::x86Reg_edi; }

// 32-bit (W) view of a mapped x86 GP register.
inline a64::Gp w(x86Reg reg) { return a64::w(x86RegId(reg)); }
// 64-bit (X) view of a mapped x86 GP register (for pointer arithmetic).
inline a64::Gp x(x86Reg reg) { return a64::x(x86RegId(reg)); }

// 128-bit NEON view of a mapped x86 XMM register.
inline a64::Vec v(x86Reg reg)
{
    int idx = (int)reg - (int)x86Reg::x86Reg_xmm0;
    if (idx < 0 || idx > 7) idx = 0;
    return a64::v(idx);
}

// Translate an x86 condition code (X86_CC_*) to an a64 CondCode.
// NOTE: this assumes flag-producing ops (CMP/SUB/ADD/...) were emitted with
// the a64 flag-setting form so that NZCV carries x86-compatible meaning.
// The x86 carry-after-subtract is inverted relative to arm; that inversion is
// absorbed by mapping B->LO / NB->HS / BE->LS / NBE->HI (arm unsigned aliases).
inline a64::CondCode toCond(int x86cc)
{
    switch (x86cc & 0xF) {
        case X86_CC_O:   return a64::CondCode::kVS;
        case X86_CC_NO:  return a64::CondCode::kVC;
        case X86_CC_B:   return a64::CondCode::kLO;
        case X86_CC_NB:  return a64::CondCode::kHS;
        case X86_CC_Z:   return a64::CondCode::kEQ;
        case X86_CC_NZ:  return a64::CondCode::kNE;
        case X86_CC_BE:  return a64::CondCode::kLS;
        case X86_CC_NBE: return a64::CondCode::kHI;
        case X86_CC_S:   return a64::CondCode::kMI;
        case X86_CC_NS:  return a64::CondCode::kPL;
        case X86_CC_L:   return a64::CondCode::kLT;
        case X86_CC_NL:  return a64::CondCode::kGE;
        case X86_CC_LE:  return a64::CondCode::kLE;
        case X86_CC_NLE: return a64::CondCode::kGT;
        // X86_CC_P / X86_CC_NP (parity) have no arm equivalent and are not
        // used by the Nuance handlers.
        default:         assert(false); return a64::CondCode::kAL;
    }
}

inline uint32_t ptrSize(x86MemPtr ptr)
{
    switch (ptr) {
        case x86MemPtr::x86MemPtr_byte:  return 1;
        case x86MemPtr::x86MemPtr_word:  return 2;
        case x86MemPtr::x86MemPtr_dword: return 4;
        case x86MemPtr::x86MemPtr_qword: return 8;
        default: return 4;
    }
}

// Materialize the effective address [base + index*scale + disp] into a 64-bit
// register and return an a64::Mem referencing it. Mirrors NuanceJit::buildMem:
//   base == X86_NO_BASE  -> [index<<scale + disp]
//   base > 7             -> absolute host address (load into kAddrScratch)
//   base 0..7            -> mapped x86 GP register as base
// `out` receives the register holding the base; the returned Mem is [out].
// x86 base/index enum slots use GP encoding order:
//   0=eax 1=ecx 2=edx 3=ebx 4=esp 5=ebp 6=esi 7=edi
// Map each slot to the backing a64 register id (see x86RegId above).
inline uint32_t baseSlotToId(unsigned slot)
{
    static const uint32_t ids[8] = { 19, 20, 21, 22, 26, 23, 24, 25 };
    return ids[slot & 7];
}

inline a64::Mem buildAddr(a64::Assembler& a, uintptr_t base,
                          x86IndexReg index, x86ScaleVal scale, int32_t disp)
{
    const uint32_t shift = (uint32_t)scale; // x86Scale_1..8 -> 0..3
    const bool haveIndex = (index != x86IndexReg::x86IndexReg_none);

    // --- Base address into kAddrScratch (64-bit) ---
    if (base == X86_NO_BASE) {
        a.mov(kAddrScratch, (uint64_t)0);
    } else if (base > 7) {
        a.mov(kAddrScratch, (uint64_t)base);          // absolute host address
    } else {
        // mapped GP register as a 32-bit base; zero-extend into scratch.
        a.mov(kAddrScratch.w(), a64::w(baseSlotToId((unsigned)base)));
    }

    // --- Add signed displacement ---
    if (disp > 0) {
        a.add(kAddrScratch, kAddrScratch, (uint64_t)(uint32_t)disp);
    } else if (disp < 0) {
        a.sub(kAddrScratch, kAddrScratch, (uint64_t)(uint32_t)(-(int64_t)disp));
    }

    // --- Add (zero-extended) index << scale ---
    if (haveIndex) {
        a.mov(kTmp0.w(), a64::w(baseSlotToId((unsigned)index))); // uxtw: zero-extend 32->64
        if (shift)
            a.add(kAddrScratch, kAddrScratch, kTmp0, a64::lsl(shift));
        else
            a.add(kAddrScratch, kAddrScratch, kTmp0);
    }

    return a64::ptr(kAddrScratch);
}

} // namespace NuanceJitA64

#endif // NUANCE_ARCH_ARM64
#endif // USE_ASMJIT
#endif // ASMJIT_A64_EMIT_H
