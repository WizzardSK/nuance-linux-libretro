#include "basetypes.h"
#include "Bios.h"
#include "mpe.h"
#include "mpe_alloc.h"
#include "NuonEnvironment.h"
#include "NuonMemoryMap.h"
#include <cstdio>
#include <cstdlib>

extern NuonEnvironment nuonEnv;
extern uint32 media_mpe_allocated;
extern uint32 media_mpe;

constexpr uint32 mpeFlags_init[4] = {
(MPE_HAS_CACHES | MPE_IRAM_8K | MPE_DTRAM_8K), //MPE0
0, //MPE1
0, //MPE2
MPE_HAS_CACHES //MPE3
};

uint32 mpeFlags[4];

void ResetMPEFlags(MPE &mpe)
{
  for(uint32 i = 0; i < 4; i++)
    mpeFlags[i] = mpeFlags_init[i];

  mpeFlags[mpe.mpeIndex] |= (MPE_ALLOC_BIOS|MPE_ALLOC_USER);
}

void MPEAlloc(MPE &mpe)
{
  const uint32 requestedUserFlags = mpe.regs[0] & MPE_USER_FLAGS;
  uint32 allocated_mpe = 0xFFFFFFFF; //-1

  for(uint32 i = 0; i < 3; i++)
  {
    if(!(mpeFlags[i] & MPE_ALLOC_USER))
    {
      const uint32 mpeUserFlags = mpeFlags[i] & MPE_USER_FLAGS;

      //The MPE is not allocated by the USER so it is available
      if((mpeUserFlags & requestedUserFlags) == requestedUserFlags)
      {
        //The MPE supports all requested flags
        if (mpeUserFlags & MPE_HAS_MINI_BIOS)
        {
          const bool bRequestedMiniBIOS = requestedUserFlags & MPE_HAS_MINI_BIOS;
          if(!bRequestedMiniBIOS)
          {
            //MINIBIOS was not explicitly requested and this MPE
            //has MINIBIOS so don't allocate it
            continue;
          }

          if (requestedUserFlags & (MPE_HAS_CACHES | MPE_IRAM_8K | MPE_DTRAM_8K)) {
            //User requested 8K IRMA or DRAM, or Caches. None of these work with an
            //MPE running the MINIBIOS.
            continue;
          }
        }

        //This MPE meets all the requirements
        if(mpeFlags_init[i] != (requestedUserFlags & ~MPE_HAS_MINI_BIOS))
        {
          //The requested flags don't match the initial MPE flags exactly. Save
          //this MPE as a possible solution, but keep checking in case there is
          //another available MPE with fewer capabilities that matches all the
          //requested capabilities, so we can preserve more capable MPEs for
          //potential future requests.
          if(allocated_mpe == 0xFFFFFFFF)
            allocated_mpe = i;
          continue;
        }

        //This MPE is an exact match for the requested capabilities
        allocated_mpe = i;
        break;
      }
    }
  }

  if (allocated_mpe != 0xFFFFFFFF) {
    mpeFlags[allocated_mpe] |= MPE_ALLOC_USER;
  }
  mpe.regs[0] = allocated_mpe;
}

void MPEAllocSpecific(MPE &mpe)
{
  if(mpe.regs[0] < 3)
  {
    if(mpeFlags[mpe.regs[0]] & MPE_ALLOC_ANY)
    {
      //MPE already allocated
      mpe.regs[0] = 0xFFFFFFFF; //-1
    }
    else
    {
      //Mark MPE as allocated
      mpeFlags[mpe.regs[0]] |= MPE_ALLOC_USER;
    }
  }
  else
  {
    //Invalid MPE number, return already allocated
    mpe.regs[0] = 0xFFFFFFFF; //-1
  }
}

void MPEFree(MPE &mpe)
{
  if(mpe.regs[0] < 3)
  {
    if(mpeFlags[mpe.regs[0]] & MPE_ALLOC_USER)
    {
      //MPE allocated by user: mark as free for user allocation
      mpeFlags[mpe.regs[0]] &= ~MPE_ALLOC_USER;
      mpe.regs[0] = 0;
    }
  }
  else
  {
    //Invalid MPE number, return already free
    mpe.regs[0] = 0xFFFFFFFF; //-1
  }
}

void MPEStatus(MPE &mpe)
{
  if(mpe.regs[0] < 4)
  {
    mpe.regs[0] = mpeFlags[mpe.regs[0]];
  }
  else
  {
    mpe.regs[0] = 0;
  }
}

void MPEsAvailable(MPE &mpe)
{
  if(mpe.regs[0] == 1)
  {
    //Return number of MPEs in system
    mpe.regs[0] = 4;
  }
  else
  {
    mpe.regs[0] = 0;

    for(int i = 0; i < 3; i++)
    {
      if(!(mpeFlags[i] & MPE_ALLOC_ANY))
      {
        mpe.regs[0]++;
      }
    }
  }
}

void MPERun(MPE &mpe)
{
  const uint32 which = mpe.regs[0];
  const uint32 entrypoint = mpe.regs[1];

  // NUANCE_LOG_MPE_DISPATCH=1: log every MPERun call for worker MPEs (1,2)
  // with target state captured BEFORE we halt+restart it. Lets us see what
  // pcexec/commxmit the previous task ended on (i.e. whether the worker
  // actually ran or returned immediately), and how many dispatches per
  // unit time MPE3 fires. Caps at NUANCE_LOG_MPE_DISPATCH_CAP=N lines.
  {
    static const char* logEnv = getenv("NUANCE_LOG_MPE_DISPATCH");
    static const uint32 cap = []() -> uint32 {
      const char* c = getenv("NUANCE_LOG_MPE_DISPATCH_CAP");
      return c ? (uint32)atoi(c) : 600;
    }();
    static uint32 callIdx[4] = {0,0,0,0};
    static uint32 logged[4] = {0,0,0,0};
    if (logEnv && which < 4 && (which == 1 || which == 2)) {
      callIdx[which]++;
      if (logged[which] < cap) {
        MPE &t = nuonEnv.mpe[which];
        // Dump first 16 bytes of target MPE's IRAM at the dispatch entry
        // point to verify whether DMA actually wrote real code there.
        const uint8* p = (const uint8*)nuonEnv.GetPointerToMemory(which, entrypoint);
        char iram_hex[64] = "?";
        if (p) {
          snprintf(iram_hex, sizeof(iram_hex),
                   "%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x",
                   p[0],p[1],p[2],p[3], p[4],p[5],p[6],p[7],
                   p[8],p[9],p[10],p[11], p[12],p[13],p[14],p[15]);
        }
        fprintf(stderr,
                "[MPE%u-DISP #%u] from=MPE%u entry=$%08X "
                "prev_pcexec=$%08X mpego=%u "
                "iram@entry=[%s] "
                "commxmit=[$%08X $%08X $%08X $%08X] "
                "commctl=$%08X intsrc=$%08X\n",
                which, callIdx[which], mpe.mpeIndex, entrypoint,
                t.pcexec, (t.mpectl & MPECTRL_MPEGO) ? 1u : 0u,
                iram_hex,
                t.commxmit[0], t.commxmit[1], t.commxmit[2], t.commxmit[3],
                t.commctl, t.intsrc);
        logged[which]++;
      }
    }
  }

  /*
  The following behavior differs from the real BIOS particularly in that
  if the target MPE is allocated the minibios, MPERun should use CommSendInfo
  to send the target MPE a comm packet with the first scalar set to the entry point
  address and comminfo set to $C2.  CommSendInfo does not return until the packet is
  sent, so this behavior requires a native assembly implementation.  This code simply
  duplicates the C2 handler logic, which is normally triggered by a software interrupt
  injected by the level2 handler.  The level1 handler gets called and eventually handles
  the queued C2 packet.  The C2 handler sets rzi1 to the entry point so that it gets called
  when the level1 handler exits
  */

  if(media_mpe_allocated && (which == media_mpe))
  {
    //nuonEnv.mpe[which].rz = nuonEnv.mpe[which].pcexec;
    //nuonEnv.mpe[which].pcexec = entrypoint;
    //nuonEnv.mpe[which].ecuSkipCounter = 0;
    //nuonEnv.mpe[which].excephalten = 0xFFFFFFFE;
    //nuonEnv.mpe[which].sp = 0x20102000;
    //Invalidate cached instruction packets
    //nuonEnv.mpe[which].InvalidateICache();

    //nuonEnv.mpe[which].UpdateInvalidateRegion(MPE_IRAM_BASE, MPE::overlayLengths[which]); //!! this one here was enabled, but why would it be needed?
    nuonEnv.mpe[which].Go();
    nuonEnv.bProcessorStartStopChange = true;
    //Let MPERunMediaMPE set a comm packet to the media MPE to start it
    mpe.regs[2] = mpe.rz;
    mpe.rz = MPERUNMEDIAMPE_ADDRESS;
    mpe.ecuSkipCounter = 0;
  }
  else
  {
    nuonEnv.mpe[which].Halt();
    //Set up entry point
    nuonEnv.mpe[which].pcexec = entrypoint;
    //set return address to zero, per vmlabs implementation (MML3D uses this to halt after the pipeline finishes)
    nuonEnv.mpe[which].rz = 0;
    nuonEnv.mpe[which].ecuSkipCounter = 0;
    //Clear exceptions 
    nuonEnv.mpe[which].excepsrc = 0;
    //Mask level1 and level2 interrupts
    nuonEnv.mpe[which].intctl = 0x88;
    nuonEnv.mpe[which].sp = 0x20101000;
    //Invalidate cached instruction packets
    //nuonEnv.mpe[which].InvalidateICache();
    //nuonEnv.mpe[which].nativeCodeCache->Flush();

    //nuonEnv.mpe[which].UpdateInvalidateRegion(MPE_IRAM_BASE, MPE::overlayLengths[which]); //!! this one here was enabled, but why would it be needed?
    //Sets mpego bit
    nuonEnv.mpe[which].Go();
    nuonEnv.bProcessorStartStopChange = true;
  }
}

void MPERunThread(MPE &mpe)
{
  const uint32 which = mpe.regs[0];
  const uint32 funcptr = mpe.regs[1];
  const uint32 arg = mpe.regs[2];
        uint32 stacktop = mpe.regs[3];

  //assume failure
  mpe.regs[0] = 0;

  //Official implementation requires that the MPE is not allocated by BIOS and 
  //that the MPE has both icache and dcache
  if((mpeFlags[which] & (MPE_ALLOC_BIOS | MPE_HAS_CACHES)) == MPE_HAS_CACHES)
  {
    //Official implementation simply modifies stacktop to be vector aligned
    stacktop &= 0xFFFFFFF0;
    nuonEnv.mpe[which].ecuSkipCounter = 0;

    nuonEnv.mpe[which].Halt();
    //Invalidate cached instruction packets
    //nuonEnv.mpe[which].InvalidateICache();
    //nuonEnv.mpe[which].nativeCodeCache->Flush();

    //nuonEnv.mpe[which].UpdateInvalidateRegion(MPE_IRAM_BASE, MPE::overlayLengths[which]); //!! this one here was enabled, but why would it be needed?
    nuonEnv.mpe[which].rz = MPE_THREAD_RETURN_ADDRESS;
    //Set up entry point
    nuonEnv.mpe[which].pcexec = funcptr;
    nuonEnv.mpe[which].ecuSkipCounter = 0;
    //Set up argument using C calling convention (first arg is r0)
    nuonEnv.mpe[which].regs[0] = arg;
    //Set up C stack pointer (r31)
    nuonEnv.mpe[which].regs[31] = stacktop;

    //** Stuff done by MPERun
    //Clear exceptions 
    nuonEnv.mpe[which].excepsrc = 0;
    //Mask level1 and level2 interrupts
    nuonEnv.mpe[which].intctl = 0x88;

    //** Stuff done by MPERunThread bootcode

    //Clear inten1 
    nuonEnv.mpe[which].inten1 = 0;
    //Clear interrupts 
    nuonEnv.mpe[which].intsrc = 0;
    //Clear dtags
    //Clear itags
    //Clear acshift
    nuonEnv.mpe[which].acshift = 0;

    //Sets mpego bit
    nuonEnv.mpe[which].Go();
    //Return 1 to indicate success;
    mpe.regs[0] = 1;
    nuonEnv.bProcessorStartStopChange = true;
  }
}

void MPEStop(MPE &mpe)
{
  if(mpe.regs[0] < 4)
  {
    nuonEnv.mpe[mpe.regs[0]].mpectl &= ~MPECTRL_MPEGO;
    nuonEnv.bProcessorStartStopChange = true;
  }
}

void MPELoad(MPE &mpe)
{
  const uint32 which = mpe.regs[0];
  const uint32 mpeaddr = mpe.regs[1];
  const uint32 linkaddr = mpe.regs[2];
  const uint32 size = mpe.regs[3];

  if(which < 4)
  {
    if(((mpeaddr & MPE_VALID_MEMORY_MASK) + size - 1) <= MPE_VALID_MEMORY_MASK)
    {
      uint8 *systemMemPtr;
      if(linkaddr < SYSTEM_BUS_BASE)
      {
        systemMemPtr = nuonEnv.mainBusDRAM + (linkaddr & MAIN_BUS_VALID_MEMORY_MASK);
      }
      else
      {
        systemMemPtr = nuonEnv.systemBusDRAM + (linkaddr & SYSTEM_BUS_VALID_MEMORY_MASK);
      }

      uint8 *mpeMemPtr = (uint8 *)nuonEnv.mpe[which].GetPointerToMemory() + (mpeaddr & MPE_VALID_MEMORY_MASK);
      memcpy(mpeMemPtr, systemMemPtr, size);

      //nuonEnv.mpe[which].InvalidateICacheRegion(mpeaddr, mpeaddr + size - 1);
      //nuonEnv.mpe[which].InvalidateICache();
      //nuonEnv.mpe[which].nativeCodeCache->FlushRegion(mpeaddr, mpeaddr + size - 1);
      nuonEnv.mpe[which].UpdateInvalidateRegion(mpeaddr & MPE_LOCAL_MEMORY_MASK, size);
      //nuonEnv.mpe[which].nativeCodeCache->FlushRegion(0x20300000, mpeaddr + size - 1);
    }
  }
}

void MPEReadRegister(MPE &mpe)
{
  const uint32 which = mpe.regs[0];
  const uint32 mpeaddr = mpe.regs[1];

  if((which < 4) && (mpeaddr >= MPE_CTRL_BASE) && (mpeaddr < MPE1_ADDR_BASE))
  {
    //Make sure that the temporary scalar and index registers are in sync
    //with the standard registers so that r0-r31, rx, ry, ru, rv and cc will
    //be read correctly.  Doing this means that MPEReadRegister is no longer
    //safe to use on a running MPE unless done so in between cycle emulation
    //on the target processor (this will always be the case when a single
    //thread handles emulation of all four processors)

    nuonEnv.mpe[which].SaveRegisters();

    mpe.regs[0] = nuonEnv.mpe[which].ReadControlRegister(mpeaddr - MPE_CTRL_BASE, mpe.reg_union);
  }
}

void MPEWriteRegister(MPE&mpe)
{
  const uint32 which = mpe.regs[0];
  const uint32 mpeaddr = mpe.regs[1];
  const uint32 value = mpe.regs[2];

  // NUANCE_LOG_MPE_DISPATCH=1 also logs MPEWriteRegister calls so we can see
  // what task descriptor MPE3 is staging into the worker before MPERun.
  // Note the bounds check below silently drops writes outside the control-
  // register window — log all calls, accepted-or-dropped, so we see if T3K
  // is passing an address we can't service.
  {
    static const char* logEnv = getenv("NUANCE_LOG_MPE_DISPATCH");
    static const uint32 cap = []() -> uint32 {
      const char* c = getenv("NUANCE_LOG_MPE_DISPATCH_CAP");
      return c ? (uint32)atoi(c) : 600;
    }();
    static uint32 logged = 0;
    if (logEnv && which < 4 && (which == 1 || which == 2) && logged < cap) {
      const bool inRange = (mpeaddr >= MPE_CTRL_BASE) && (mpeaddr < MPE1_ADDR_BASE);
      fprintf(stderr,
              "[MPE%u-WREG] from=MPE%u mpeaddr=$%08X value=$%08X %s\n",
              which, mpe.mpeIndex, mpeaddr, value,
              inRange ? "OK" : "DROPPED(out-of-range)");
      logged++;
    }
  }

  if((which < 4) && (mpeaddr >= MPE_CTRL_BASE) && (mpeaddr < MPE1_ADDR_BASE))
  {
    nuonEnv.mpe[which].WriteControlRegister(mpeaddr - MPE_CTRL_BASE,value);
  }
}
