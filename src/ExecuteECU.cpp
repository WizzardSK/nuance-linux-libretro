#include "InstructionCache.h"
#include "mpe.h"
#include <cstdio>
#include <cstdlib>

// NUANCE_LOG_JSR=<lo_hex>:<hi_hex>[:<min_mpe>:<max_mpe>] traces every
// JSR execution where pcexec is in [lo, hi). Logs source pc, target,
// rz (return). Optionally restrict to specific MPE id(s). Filter is
// applied to the CALLER PC (jsr's source) so we only see calls FROM
// a specific code region (e.g. levelsel.run at 0x80030000..0x80300000).
static uint32 s_jsr_lo = 0, s_jsr_hi = 0;
static int s_jsr_mpe_min = 0, s_jsr_mpe_max = 3;
static int s_jsr_inited = 0;
static uint64 s_jsr_count = 0;
static inline void jsr_init() {
  if (s_jsr_inited) return;
  s_jsr_inited = 1;
  const char* s = getenv("NUANCE_LOG_JSR");
  if (!s) return;
  uint32 lo = 0, hi = 0; int mm = 3, mx = 3;
  int n = sscanf(s, "%x:%x:%d:%d", &lo, &hi, &mm, &mx);
  if (n >= 2) {
    s_jsr_lo = lo; s_jsr_hi = hi;
    if (n >= 4) { s_jsr_mpe_min = mm; s_jsr_mpe_max = mx; }
    else if (n == 3) { s_jsr_mpe_min = s_jsr_mpe_max = mm; }
    fprintf(stderr, "[JSR-TRACE] watching pc=[0x%08X,0x%08X) mpe=[%d..%d]\n",
            s_jsr_lo, s_jsr_hi, s_jsr_mpe_min, s_jsr_mpe_max);
  }
}
static inline void jsr_log(MPE &mpe, uint32 target) {
  if (!s_jsr_inited) jsr_init();
  if (s_jsr_lo == s_jsr_hi) return;
  if ((int)mpe.mpeIndex < s_jsr_mpe_min || (int)mpe.mpeIndex > s_jsr_mpe_max) return;
  const uint32 pc = mpe.pcexec;
  if (pc < s_jsr_lo || pc >= s_jsr_hi) return;
  s_jsr_count++;
  if (s_jsr_count < 5000 || (s_jsr_count % 1000) == 0)
    fprintf(stderr, "[JSR #%llu] mpe=%u pc=0x%08X -> 0x%08X (rz=0x%08X)\n",
            (unsigned long long)s_jsr_count, mpe.mpeIndex,
            pc, target, mpe.rz);
}

void Execute_ECU_NOP(MPE &mpe, const uint32 pRegs[48], const Nuance &nuance)
{
}

void Execute_Halt(MPE &mpe, const uint32 pRegs[48], const Nuance &nuance)
{
  if(!mpe.ecuSkipCounter)
  {
    mpe.excepsrc |= 0x01;
    if(MPE_IRAM_BASE < mpe.invalidateRegionStart)
      mpe.invalidateRegionStart = MPE_IRAM_BASE;
    if ((MPE_IRAM_BASE + MPE::overlayLengths[mpe.mpeIndex] - 1) > mpe.invalidateRegionEnd)
      mpe.invalidateRegionEnd = MPE_IRAM_BASE + MPE::overlayLengths[mpe.mpeIndex] - 1;

    //If the halt enable bit for the halt exception is not set
    if(!(mpe.excephalten & (1U << 0)))
    {
      //set exception bit in interrupt source register
      mpe.intsrc |= 0x01;
    }
    else
    {
      mpe.mpectl &= ~MPECTRL_MPEGO;
    }
  }
}

void Execute_BRAAlways(MPE &mpe, const uint32 pRegs[48], const Nuance &nuance)
{
  if(!mpe.ecuSkipCounter)
  {
    mpe.pcfetchnext = nuance.fields[FIELD_ECU_ADDRESS];
    mpe.ecuSkipCounter = 3;
  }
}

void Execute_BRAAlways_NOP(MPE &mpe, const uint32 pRegs[48], const Nuance &nuance)
{
  if(!mpe.ecuSkipCounter)
  {
    mpe.pcfetchnext = nuance.fields[FIELD_ECU_ADDRESS];
    mpe.ecuSkipCounter = 1;
  }
}

void Execute_BRAConditional(MPE &mpe, const uint32 pRegs[48], const Nuance &nuance)
{
  if(!mpe.ecuSkipCounter)
  {
    if(mpe.TestConditionCode((uint32_t)nuance.fields[FIELD_ECU_CONDITION]))
    {
      mpe.pcfetchnext = nuance.fields[FIELD_ECU_ADDRESS];
      mpe.ecuSkipCounter = 3;
    }
  }
}

void Execute_BRAConditional_NOP(MPE &mpe, const uint32 pRegs[48], const Nuance &nuance)
{
  if(!mpe.ecuSkipCounter)
  {
    if(mpe.TestConditionCode((uint32_t)nuance.fields[FIELD_ECU_CONDITION]))
    {
      mpe.pcfetchnext = nuance.fields[FIELD_ECU_ADDRESS];
      mpe.ecuSkipCounter = 1;
    }
  }
}

void Execute_JMPAlwaysIndirect(MPE &mpe, const uint32 pRegs[48], const Nuance &nuance)
{
  if(!mpe.ecuSkipCounter)
  {
    mpe.pcfetchnext = pRegs[nuance.fields[FIELD_ECU_ADDRESS]];
    mpe.ecuSkipCounter = 3;
  }
}

void Execute_JMPAlwaysIndirect_NOP(MPE &mpe, const uint32 pRegs[48], const Nuance &nuance)
{
  if(!mpe.ecuSkipCounter)
  {
    mpe.pcfetchnext = pRegs[nuance.fields[FIELD_ECU_ADDRESS]];
    mpe.ecuSkipCounter = 1;
  }
}

void Execute_JMPConditionalIndirect(MPE &mpe, const uint32 pRegs[48], const Nuance &nuance)
{
  if(!mpe.ecuSkipCounter)
  {
    if(mpe.TestConditionCode((uint32_t)nuance.fields[FIELD_ECU_CONDITION]))
    {
      mpe.pcfetchnext = pRegs[nuance.fields[FIELD_ECU_ADDRESS]];
      mpe.ecuSkipCounter = 3;
    }
  }
}

void Execute_JMPConditionalIndirect_NOP(MPE &mpe, const uint32 pRegs[48], const Nuance &nuance)
{
  if(!mpe.ecuSkipCounter)
  {
    if(mpe.TestConditionCode((uint32_t)nuance.fields[FIELD_ECU_CONDITION]))
    {
      mpe.pcfetchnext = pRegs[nuance.fields[FIELD_ECU_ADDRESS]];
      mpe.ecuSkipCounter = 1;
    }
  }
}

void Execute_JSRAlways(MPE &mpe, const uint32 pRegs[48], const Nuance &nuance)
{
  if(!mpe.ecuSkipCounter)
  {
    mpe.rz = nuance.fields[FIELD_ECU_PCFETCHNEXT];
    mpe.pcfetchnext = nuance.fields[FIELD_ECU_ADDRESS];
    mpe.ecuSkipCounter = 3;
    jsr_log(mpe, (uint32)mpe.pcfetchnext);
  }
}

void Execute_JSRAlways_NOP(MPE &mpe, const uint32 pRegs[48], const Nuance &nuance)
{
  if(!mpe.ecuSkipCounter)
  {
    mpe.rz = nuance.fields[FIELD_ECU_PCROUTE];
    mpe.pcfetchnext = nuance.fields[FIELD_ECU_ADDRESS];
    mpe.ecuSkipCounter = 1;
    jsr_log(mpe, (uint32)mpe.pcfetchnext);
  }
}

void Execute_JSRConditional(MPE &mpe, const uint32 pRegs[48], const Nuance &nuance)
{
  if(!mpe.ecuSkipCounter)
  {
    if(mpe.TestConditionCode((uint32_t)nuance.fields[FIELD_ECU_CONDITION]))
    {
      mpe.rz = nuance.fields[FIELD_ECU_PCFETCHNEXT];
      mpe.pcfetchnext = nuance.fields[FIELD_ECU_ADDRESS];
      mpe.ecuSkipCounter = 3;
      jsr_log(mpe, (uint32)mpe.pcfetchnext);
    }
  }
}

void Execute_JSRConditional_NOP(MPE &mpe, const uint32 pRegs[48], const Nuance &nuance)
{
  if(!mpe.ecuSkipCounter)
  {
    if(mpe.TestConditionCode((uint32_t)nuance.fields[FIELD_ECU_CONDITION]))
    {
      mpe.rz = nuance.fields[FIELD_ECU_PCROUTE];
      mpe.pcfetchnext = nuance.fields[FIELD_ECU_ADDRESS];
      mpe.ecuSkipCounter = 1;
      jsr_log(mpe, (uint32)mpe.pcfetchnext);
    }
  }
}

void Execute_JSRAlwaysIndirect(MPE &mpe, const uint32 pRegs[48], const Nuance &nuance)
{
  if(!mpe.ecuSkipCounter)
  {
    mpe.rz = nuance.fields[FIELD_ECU_PCFETCHNEXT];
    mpe.pcfetchnext = pRegs[nuance.fields[FIELD_ECU_ADDRESS]];
    mpe.ecuSkipCounter = 3;
    jsr_log(mpe, (uint32)mpe.pcfetchnext);
  }
}

void Execute_JSRAlwaysIndirect_NOP(MPE &mpe, const uint32 pRegs[48], const Nuance &nuance)
{
  if(!mpe.ecuSkipCounter)
  {
    mpe.rz = nuance.fields[FIELD_ECU_PCROUTE];
    mpe.pcfetchnext = pRegs[nuance.fields[FIELD_ECU_ADDRESS]];
    mpe.ecuSkipCounter = 1;
    jsr_log(mpe, (uint32)mpe.pcfetchnext);
  }
}

void Execute_JSRConditionalIndirect(MPE &mpe, const uint32 pRegs[48], const Nuance &nuance)
{
  if(!mpe.ecuSkipCounter)
  {
    if(mpe.TestConditionCode((uint32_t)nuance.fields[FIELD_ECU_CONDITION]))
    {
      mpe.rz = nuance.fields[FIELD_ECU_PCFETCHNEXT];
      mpe.pcfetchnext = pRegs[nuance.fields[FIELD_ECU_ADDRESS]];
      mpe.ecuSkipCounter = 3;
      jsr_log(mpe, (uint32)mpe.pcfetchnext);
    }
  }
}

void Execute_JSRConditionalIndirect_NOP(MPE &mpe, const uint32 pRegs[48], const Nuance &nuance)
{
  if(!mpe.ecuSkipCounter)
  {
    if(mpe.TestConditionCode((uint32_t)nuance.fields[FIELD_ECU_CONDITION]))
    {
      mpe.rz = nuance.fields[FIELD_ECU_PCROUTE];
      mpe.pcfetchnext = pRegs[nuance.fields[FIELD_ECU_ADDRESS]];
      mpe.ecuSkipCounter = 1;
      jsr_log(mpe, (uint32)mpe.pcfetchnext);
    }
  }
}

void Execute_RTSAlways(MPE &mpe, const uint32 pRegs[48], const Nuance &nuance)
{
  if(!mpe.ecuSkipCounter)
  {
    mpe.pcfetchnext = pRegs[RZ_REG+0];
    mpe.ecuSkipCounter = 3;
  }
}

void Execute_RTSAlways_NOP(MPE &mpe, const uint32 pRegs[48], const Nuance &nuance)
{
  if(!mpe.ecuSkipCounter)
  {
    mpe.pcfetchnext = pRegs[RZ_REG+0];
    mpe.ecuSkipCounter = 1;
  }
}

void Execute_RTSConditional(MPE &mpe, const uint32 pRegs[48], const Nuance &nuance)
{
  if(!mpe.ecuSkipCounter)
  {
    if(mpe.TestConditionCode((uint32_t)nuance.fields[FIELD_ECU_CONDITION]))
    {
      mpe.pcfetchnext = pRegs[RZ_REG+0];
      mpe.ecuSkipCounter = 3;
    }
  }
}

void Execute_RTSConditional_NOP(MPE &mpe, const uint32 pRegs[48], const Nuance &nuance)
{
  if(!mpe.ecuSkipCounter)
  {
    if(mpe.TestConditionCode((uint32_t)nuance.fields[FIELD_ECU_CONDITION]))
    {
      mpe.pcfetchnext = pRegs[RZ_REG+0];
      mpe.ecuSkipCounter = 1;
    }
  }
}

void Execute_RTI1Conditional(MPE &mpe, const uint32 pRegs[48], const Nuance &nuance)
{
  if(!mpe.ecuSkipCounter)
  {
    if(mpe.TestConditionCode((uint32_t)nuance.fields[FIELD_ECU_CONDITION]))
    {
      mpe.intctl &= ~(1U << 1);
      mpe.pcfetchnext = pRegs[RZ_REG+1];
      mpe.ecuSkipCounter = 3;
    }
  }
}

void Execute_RTI1Conditional_NOP(MPE &mpe, const uint32 pRegs[48], const Nuance &nuance)
{
  if(!mpe.ecuSkipCounter)
  {
    if(mpe.TestConditionCode((uint32_t)nuance.fields[FIELD_ECU_CONDITION]))
    {
      mpe.intctl &= ~(1U << 1);
      mpe.pcfetchnext = pRegs[RZ_REG+1];
      mpe.ecuSkipCounter = 1;
    }
  }
}

void Execute_RTI2Conditional(MPE &mpe, const uint32 pRegs[48], const Nuance &nuance)
{
  if(!mpe.ecuSkipCounter)
  {
    if(mpe.TestConditionCode((uint32_t)nuance.fields[FIELD_ECU_CONDITION]))
    {
      mpe.intctl &= ~(1U << 5);
      mpe.pcfetchnext = pRegs[RZ_REG+2];
      mpe.ecuSkipCounter = 3;
    }
  }
}

void Execute_RTI2Conditional_NOP(MPE &mpe, const uint32 pRegs[48], const Nuance &nuance)
{
  if(!mpe.ecuSkipCounter)
  {
    if(mpe.TestConditionCode((uint32_t)nuance.fields[FIELD_ECU_CONDITION]))
    {
      mpe.intctl &= ~(1U << 5);
      mpe.pcfetchnext = pRegs[RZ_REG+2];
      mpe.ecuSkipCounter = 1;
    }
  }
}
