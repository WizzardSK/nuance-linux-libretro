#include "basetypes.h"
#include "InstructionCache.h"

InstructionCache::InstructionCache(const uint32 desiredEntries) : numEntries(!desiredEntries ? DEFAULT_NUM_CACHE_ENTRIES : desiredEntries)
{
  validBitmap = new uint32[(numEntries/32) + 1];
  // init_array((uint8*)validBitmap, ((numEntries / 32) + 1)*sizeof(uint32)); // not needed as Invalidate is called below
  cacheEntries = new InstructionCacheEntry[numEntries];
  //!! init_array((uint8*)cacheEntries, numEntries*sizeof(InstructionCacheEntry));

  Invalidate();
}

InstructionCache::~InstructionCache()
{
  if(cacheEntries)
  {
    delete [] cacheEntries;
  }

  if(validBitmap)
  {
    delete [] validBitmap;
  }
}

void InstructionCache::Invalidate()
{
  memset(validBitmap, 0, (numEntries/32 + 1)*sizeof(uint32));
}

void InstructionCache::InvalidateRegion(const uint32 start, const uint32 end)
{
  // The instruction cache is direct-mapped: address X always lives at
  // cacheEntries[(X >> 1) & (numEntries - 1)]. So instead of scanning all
  // numEntries (8K) cacheEntries linearly per call, iterate the actual
  // address range and probe just the slot each address would map to.
  //
  // For Ballistic's typical (addr, size=20) DCacheFlush args that's 10
  // iterations instead of 8192. T3K's attract-mode polling spam (size=2)
  // is 1 iter instead of 8192. Makes DCacheFlush calls cheap enough that
  // T3K runs at full 60 fps instead of dropping to 2.
  //
  // Falls back to the old linear scan when the address range is bigger
  // than the cache itself (a code-segment-sized region invalidation —
  // module load/unload), because then the old O(numEntries) scan beats
  // O(end-start) iteration.
  const uint32 rangeBytes = end - start + 1;
  if (rangeBytes >= numEntries * 2)
  {
    // Fall back to linear scan over the cache
    uint32 mask = 0x80000000U;
    uint32 validBitmapIndex = 0;
    for (uint32 i = 0; i < numEntries; i++)
    {
      const uint32 tag = cacheEntries[i].pcexec;
      if (tag >= start && tag <= end)
        validBitmap[validBitmapIndex] &= ~mask;
      if (mask == 0x01) { validBitmapIndex++; mask = 0x80000000U; }
      else mask >>= 1;
    }
    return;
  }

  const uint32 idxMask = numEntries - 1;
  for (uint32 addr = start; addr <= end; addr += 2)
  {
    const uint32 idx = (addr >> 1) & idxMask;
    const uint32 tag = cacheEntries[idx].pcexec;
    if (tag >= start && tag <= end)
      validBitmap[idx >> 5] &= ~(0x80000000U >> (idx & 0x1FU));
    if (addr == 0xFFFFFFFE) break;  // address-space wrap guard
  }
}

void InstructionCacheEntry::CopyInstructionData(const uint32 toSlot, const InstructionCacheEntry &src, const uint32 fromSlot)
{
  nuances[FIXED_FIELD(toSlot,0)] = src.nuances[FIXED_FIELD(fromSlot,0)];
  nuances[FIXED_FIELD(toSlot,1)] = src.nuances[FIXED_FIELD(fromSlot,1)];
  nuances[FIXED_FIELD(toSlot,2)] = src.nuances[FIXED_FIELD(fromSlot,2)];
  nuances[FIXED_FIELD(toSlot,3)] = src.nuances[FIXED_FIELD(fromSlot,3)];
  nuances[FIXED_FIELD(toSlot,4)] = src.nuances[FIXED_FIELD(fromSlot,4)];
  scalarInputDependencies[toSlot] = src.scalarInputDependencies[fromSlot];
  miscInputDependencies[toSlot] = src.miscInputDependencies[fromSlot];
  scalarOutputDependencies[toSlot] = src.scalarOutputDependencies[fromSlot];
  miscOutputDependencies[toSlot] = src.miscOutputDependencies[fromSlot];
}
