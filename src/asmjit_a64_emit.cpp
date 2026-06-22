// asmjit AArch64 block management for the NuanceResurrection JIT.
// ARM64 counterpart of asmjit_emit.cpp (NativeCodeCache block/label plumbing).
#ifdef USE_ASMJIT
#ifdef NUANCE_ARCH_ARM64

#include "asmjit_a64_emit.h"
#include "NativeCodeCache.h"
#include <cstdio>
#include <cstring>

// Self-test: generate a function returning 42, then one that writes to a
// 64-bit address, to confirm the a64 assembler + JitRuntime work on the host.
bool asmjit_a64_selftest()
{
    using namespace asmjit;

    JitRuntime rt;
    CodeHolder code;
    code.init(rt.environment(), rt.cpu_features());
    a64::Assembler a(&code);

    a.mov(a64::w0, (uint64_t)42);
    a.ret(a64::x30);

    typedef int (*TestFunc)();
    TestFunc fn;
    Error err = rt.add(&fn, &code);
    if (err != kErrorOk) {
        fprintf(stderr, "asmjit a64 selftest: codegen failed (err=%d)\n", err);
        return false;
    }
    int result = fn();
    rt.release(fn);
    if (result != 42) {
        fprintf(stderr, "asmjit a64 selftest: expected 42, got %d\n", result);
        return false;
    }

    // Test 2: write 99 to a 64-bit address.
    CodeHolder code2;
    code2.init(rt.environment(), rt.cpu_features());
    a64::Assembler a2(&code2);
    static volatile int testVal = 0;
    a2.mov(a64::x9, (uint64_t)(uintptr_t)&testVal);
    a2.mov(a64::w10, (uint64_t)99);
    a2.str(a64::w10, a64::ptr(a64::x9));
    a2.ret(a64::x30);

    typedef void (*WriteFunc)();
    WriteFunc fn2;
    err = rt.add(&fn2, &code2);
    if (err != kErrorOk) { fprintf(stderr, "asmjit a64 selftest2: codegen failed\n"); return false; }
    fn2();
    rt.release(fn2);
    if (testVal != 99) {
        fprintf(stderr, "asmjit a64 selftest2: expected 99, got %d\n", testVal);
        return false;
    }
    fprintf(stderr, "asmjit a64 selftest: PASSED\n");
    return true;
}

// --- NativeCodeCache a64 block management ---

void NativeCodeCache::AsmJit_BeginBlock()
{
    if (asmjitCode) {
        delete a64As;   a64As = nullptr;
        delete asmjitCode; asmjitCode = nullptr;
    }

    asmjitCode = new asmjit::CodeHolder();
    asmjitCode->init(asmjitRuntime.environment(), asmjitRuntime.cpu_features());
    a64As = new asmjit::a64::Assembler(asmjitCode);

    for (int i = 0; i < MAX_ASMJIT_LABELS; i++) {
        asmjitLabels[i] = asmjit::Label();
        asmjitLabelBound[i] = false;
    }
    asmjitBlockActive = true;
}

uint32 NativeCodeCache::AsmJit_EndBlock()
{
    if (!a64As || !asmjitCode) {
        asmjitBlockActive = false;
        return 0;
    }

    asmjitCode->flatten();
    asmjitCode->resolve_cross_section_fixups();

    size_t codeSize = asmjitCode->code_size();
    if (codeSize == 0) {
        delete a64As; a64As = nullptr;
        delete asmjitCode; asmjitCode = nullptr;
        asmjitBlockActive = false;
        return 0;
    }

    asmjitCode->relocate_to_base((uint64_t)(uintptr_t)pEmitLoc);
    asmjitCode->copy_flattened_data(pEmitLoc, codeSize);

    // Make the freshly written code visible to the instruction stream. On
    // AArch64 this requires explicit dcache clean + icache invalidate; the
    // compiler builtin emits the proper DC CVAU / IC IVAU / DSB / ISB sequence.
    __builtin___clear_cache((char*)pEmitLoc, (char*)pEmitLoc + codeSize);

    delete a64As; a64As = nullptr;
    delete asmjitCode; asmjitCode = nullptr;
    asmjitBlockActive = false;
    return (uint32)codeSize;
}

void NativeCodeCache::AsmJit_BindLabel(uint32 labelIndex)
{
    if (!a64As || labelIndex >= MAX_ASMJIT_LABELS) return;

    if (!asmjitLabelBound[labelIndex]) {
        if (!asmjitLabels[labelIndex].is_valid()) {
            asmjitLabels[labelIndex] = a64As->new_label();
        }
        a64As->bind(asmjitLabels[labelIndex]);
        asmjitLabelBound[labelIndex] = true;
    } else {
        a64As->bind(asmjitLabels[labelIndex]);
    }
}

asmjit::Label& NativeCodeCache::AsmJit_GetLabel(uint32 labelIndex)
{
    assert(labelIndex < MAX_ASMJIT_LABELS);
    // Mirror the x86 path: hand out a fresh label if the slot was already bound
    // (Emit_* helpers assume small label indices are fresh per handler).
    if (asmjitLabelBound[labelIndex] || !asmjitLabels[labelIndex].is_valid()) {
        asmjitLabels[labelIndex] = a64As->new_label();
        asmjitLabelBound[labelIndex] = false;
    }
    return asmjitLabels[labelIndex];
}

#endif // NUANCE_ARCH_ARM64
#endif // USE_ASMJIT
