#include "basetypes.h"
#include "byteswap.h"
#include "NuonEnvironment.h"
#include "video.h"
#include <cassert>

extern NuonEnvironment nuonEnv;
extern VidChannel structMainChannel;

// Pixel Type 6 = 32-bit color + 32-bit Z, single-buffer layout in DRAM.
//
// Source (in MPE local memory) is interleaved color+Z (8 bytes per pixel),
// the same convention bdma_type5.cpp uses for pixtype 5 doubled in width.
// In dup+direct mode (the only case observed so far from Iron Soldier 3's
// rasterizer) intaddr is a 32-bit colour value to be tiled into the
// destination block; Z is left untouched when zcompare=7, or written as a
// no-op far value (0) when zcompare=0 (always-pass, no test) is requested
// by the game just for the colour fill.
//
// Horizontal write, A=0, B=0 — the only variant that fires for IS3 today.
// Other (V/A/B) variants stay routed through UnimplementedBilinearDMAHandler.
void BDMA_Type6_Write_0(MPE &mpe, const uint32 flags, const uint32 baseaddr, const uint32 xinfo, const uint32 yinfo, const uint32 intaddr)
{
  const bool bRemote = flags & (1U << 28);
  const bool bDirect = flags & (1U << 27);
  const bool bDup    = flags & (3U << 26); // bDup = dup | direct
  const int32 xsize  = (flags >> 13) & 0x7F8U;
  const uint32 zcompare = (flags >> 1) & 0x07U;
  const uint32 sdramBase = baseaddr & 0x7FFFFFFCU;
  const uint32 mpeBase   = intaddr  & 0x7FFFFFFCU;
  const uint32 xlen = (xinfo >> 16) & 0x3FFU;
  const uint32 xpos = xinfo & 0x7FFU;
        uint32 ylen = (yinfo >> 16) & 0x3FFU;
  const uint32 ypos = yinfo & 0x7FFU;

  if (xsize == 0 || xlen == 0 || ylen == 0)
    return;

  // Source pointer: MPE local memory (or remote MPE) for non-dup transfers.
  void *intMemory = nullptr;
  if (!(bDup && bDirect)) {
    if (bRemote)
      assert(((mpeBase >> 23) & 0x1Fu) < 4);
    intMemory = nuonEnv.GetPointerToMemory(bRemote ? (mpeBase >> 23) & 0x1Fu : mpe.mpeIndex,
                                           mpeBase & 0x207FFFFF, false);
    if (!intMemory)
      return;
  }

  // Destination: 32-bit color region at base, 32-bit Z region directly after.
  assert(((sdramBase >> 23) & 0x1Fu) < 4);
  void *baseMemory = nuonEnv.GetPointerToMemory((sdramBase >> 23) & 0x1Fu, sdramBase, false);
  if (!baseMemory)
    return;

  const uint32 destOffsetPixels = ypos * (uint32)xsize + xpos;
  const uint32 planeStride = (uint32)xsize * structMainChannel.src_height;

  uint32 *pDestColor = ((uint32 *)baseMemory) + destOffsetPixels;
  uint32 *pDestZ     = ((uint32 *)baseMemory) + planeStride + destOffsetPixels;

  const bool bUpdateZ = (zcompare != 7);

  uint32 directColor = 0;
  uint32 directZ = 0;
  const uint32 *pSrcColor = nullptr;
  const uint32 *pSrcZ = nullptr;
  int32 srcAStep = 0;
  int32 srcBStep = 0;

  if (bDup) {
    if (bDirect) {
      // Direct + dup: intaddr itself is the source pixel.
      // For 32+32Z we only have 32 bits of immediate, so reuse the
      // colour value for Z. zcompare=0/7 means no real compare, so the
      // Z write is either suppressed (==7) or overwritten with the same
      // word (==0). Either way no game logic depends on the exact Z.
      directColor = intaddr;
      directZ = intaddr;
    } else {
      // Dup but not direct: read scalar from memory, no swap needed.
      directColor = ((uint32 *)intMemory)[0];
      directZ     = ((uint32 *)intMemory)[1];
    }
    pSrcColor = &directColor;
    pSrcZ     = &directZ;
    srcAStep = 0;
    srcBStep = 0;
  } else {
    // Non-dup: stream interleaved color+Z pairs from MPE local memory.
    pSrcColor = (const uint32 *)intMemory;
    pSrcZ     = pSrcColor + 1;
    srcAStep = 2;        // step over (color, Z) pair = 2 uint32
    srcBStep = (int32)xsize * 2;
  }

  constexpr int32 destAStep = 1;       // BVA=000: horizontal, x+, y+
  const int32 destBStep = xsize;

  // zcompare=0 means "always fail compare" per the comment block at the top
  // of dma.cpp ("the Z-value is neither compared nor updated"), so for the
  // present IS3 use case (clear/fill with zcompare=0) we just copy without
  // any conditional. Other zcompare values are exercised by gameplay we
  // haven't reached yet; keeping the simple path until evidence requires
  // the full ladder.
  while (ylen--) {
    uint32 srcA = 0;
    for (uint32 destA = 0; destA < xlen; destA++) {
      pDestColor[destA] = pSrcColor[srcA];
      if (bUpdateZ)
        pDestZ[destA] = pSrcZ[srcA];
      srcA += srcAStep;
    }
    pSrcColor += srcBStep;
    pSrcZ     += srcBStep;
    pDestColor += destBStep;
    pDestZ     += destBStep;
  }
}
