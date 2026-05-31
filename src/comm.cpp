#include "basetypes.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "byteswap.h"
#include "mpe.h"
#include "comm.h"
#include "NuonEnvironment.h"
#include "joystick.h"

extern NuonEnvironment nuonEnv;
extern uint32 vdgCLUT[];
extern ControllerData *controller;

#ifdef LOG_COMM
FILE *commLogFile;
#endif

// NUANCE_LOG_COMMBUS=1 — runtime trace of every DoCommBusController event:
// who sent to whom, whether delivered or failed, and a 4-MPE commctl
// snapshot every N events. Throttled to avoid log spam: first 50 events
// inline, every 1000th thereafter; full snapshot every 5000 events.
// Goal (per nuance-is3-loading-stall-2026-05-14): find which target MPE
// has COMM_RECV_BUFFER_FULL_BIT stuck set during the IS3 LOADING stall.
static inline bool LogCommBusEnabled()
{
  static int s = -1;
  if (s == -1) s = getenv("NUANCE_LOG_COMMBUS") ? 1 : 0;
  return s != 0;
}
static void LogCommBusEvent(const char* what, uint32 src, uint32 dst)
{
  if (!LogCommBusEnabled()) return;
  static uint64 s_n = 0;
  s_n++;
  const bool inline_log = (s_n <= 50) || ((s_n % 1000) == 0);
  if (inline_log) {
    fprintf(stderr, "[COMMBUS] #%lu %s src=%u dst=%u "
            "ctl0=%08X ctl1=%08X ctl2=%08X ctl3=%08X\n",
            (unsigned long)s_n, what, src, dst,
            nuonEnv.mpe[0].commctl, nuonEnv.mpe[1].commctl,
            nuonEnv.mpe[2].commctl, nuonEnv.mpe[3].commctl);
  }
  if ((s_n % 5000) == 0) {
    fprintf(stderr, "[COMMBUS] === 5k-event snapshot pendingReqs=%u ===\n",
            nuonEnv.pendingCommRequests);
  }
}

// NUANCE_LOG_COMMBUS_PAYLOAD=1 — log every successful packet's 4 words
// + comminfo at delivery time. Useful for finding what message codes
// a module sends to trigger a state transition (e.g. IS3 levelsel.run
// asking mcp.run to load ismerlin.run via a comm packet).
// Optional NUANCE_LOG_COMMBUS_PAYLOAD_FILTER=<srcdst_hex> limits output:
//   FILTER=23  → only src=2 dst=3
//   FILTER=13  → only src=1 dst=3
//   FILTER=*3  (any literal nibble = wildcard) → any source to dst=3
// Without filter, logs every deliver event with payload. May be very
// chatty (~170 packets/sec during IS3 LOADING stall).
static void LogCommBusPayload(uint32 src, uint32 dst,
                               uint32 w0, uint32 w1, uint32 w2, uint32 w3,
                               uint32 info)
{
  static int s_inited = 0;
  static int s_enabled = 0;
  static int s_filter_src = -1; // -1 = any
  static int s_filter_dst = -1;
  if (!s_inited) {
    s_inited = 1;
    s_enabled = getenv("NUANCE_LOG_COMMBUS_PAYLOAD") ? 1 : 0;
    const char* f = getenv("NUANCE_LOG_COMMBUS_PAYLOAD_FILTER");
    if (f && f[0]) {
      char a = f[0], b = f[1];
      if (a >= '0' && a <= '9') s_filter_src = a - '0';
      if (b >= '0' && b <= '9') s_filter_dst = b - '0';
    }
  }
  if (!s_enabled) return;
  if (s_filter_src >= 0 && (int)src != s_filter_src) return;
  if (s_filter_dst >= 0 && (int)dst != s_filter_dst) return;
  fprintf(stderr, "[COMM-PAYLOAD] mpe%u->mpe%u w0=%08X w1=%08X w2=%08X w3=%08X info=%08X\n",
          src, dst, w0, w1, w2, w3, info);
}

void DoCommBusController(void)
{
  static uint32 currentTransmitID = 0;

  bool bLocked = false;
  bool bPending = false;

  if(!(nuonEnv.mpe[currentTransmitID].commctl & COMM_LOCKED_BIT))
  {
    for(uint32 i = 0; i < 4; i++)
    {
      const uint32 idx = (currentTransmitID + i) & 0x03U;
      if(nuonEnv.mpe[idx].commctl & COMM_XMIT_BUFFER_FULL_BIT)
      {
        currentTransmitID = idx;
        bPending = true;
        break;
      }
    }
  }
  else
  {
    bLocked = true;
    bPending = (nuonEnv.mpe[currentTransmitID].commctl & COMM_XMIT_BUFFER_FULL_BIT);
  }

  if(!bPending)
    return;

  const uint32 target = nuonEnv.mpe[currentTransmitID].commctl & COMM_TARGET_ID_BITS;

  if(target < 4) // target is a MPE?
  {
    if(!(nuonEnv.mpe[target].commctl & (COMM_DISABLED_BITS | COMM_RECV_BUFFER_FULL_BIT)))
    {
      nuonEnv.mpe[currentTransmitID].commctl &= ~(COMM_XMIT_BUFFER_FULL_BIT);
      nuonEnv.mpe[target].commrecv[0] = nuonEnv.mpe[currentTransmitID].commxmit[0];
      nuonEnv.mpe[target].commrecv[1] = nuonEnv.mpe[currentTransmitID].commxmit[1];
      nuonEnv.mpe[target].commrecv[2] = nuonEnv.mpe[currentTransmitID].commxmit[2];
      nuonEnv.mpe[target].commrecv[3] = nuonEnv.mpe[currentTransmitID].commxmit[3];
      nuonEnv.mpe[target].comminfo &= 0xFFU;
      nuonEnv.mpe[target].comminfo |= (nuonEnv.mpe[currentTransmitID].comminfo << 16);
      nuonEnv.mpe[target].commctl &= ~(COMM_SOURCE_ID_BITS);
      nuonEnv.mpe[target].commctl |= (COMM_RECV_BUFFER_FULL_BIT | (currentTransmitID << 16));

      nuonEnv.mpe[currentTransmitID].TriggerInterrupt(INT_COMMXMIT);
      nuonEnv.mpe[target].TriggerInterrupt(INT_COMMRECV);
      nuonEnv.pendingCommRequests--;

      // NUANCE_C2_BRIDGE=1 (default off): when MPE3 sends a CommSendInfo
      // packet with info-byte 0xC2 to MPE0, synthesize a worker code-load.
      // IS3 packet shape from NUANCE_LOG_COMMBUS_PAYLOAD trace:
      //   w0 = load address  (typically 0x20300000 = worker MPE IRAM)
      //   w1 = entry point   (same as w0)
      //   w2 = source address (main DRAM, e.g. 0x800ADC04 / 0x80237C84 /
      //                       0x80209904 — different modules per session)
      //   w3 = stack         (typically 0x20500500)
      // The packet doesn't carry a size, so we guess 0x4000 (16 KB) which
      // covers the whole worker IRAM code area observed in disasm.
      // Skip the load if target/source ranges look implausible (sanity).
      if (target == 0 && currentTransmitID == 3)
      {
        const uint32 c2InfoByte = nuonEnv.mpe[3].comminfo & 0xFFu;
        static int s_c2_inited = 0; static int s_c2_on = 0;
        if (!s_c2_inited) { s_c2_inited = 1; s_c2_on = getenv("NUANCE_C2_BRIDGE") ? 1 : 0; }
        if (s_c2_on && c2InfoByte == 0xC2u)
        {
          const uint32 loadAddr = nuonEnv.mpe[0].commrecv[0];
          const uint32 entryPC  = nuonEnv.mpe[0].commrecv[1];
          const uint32 srcAddr  = nuonEnv.mpe[0].commrecv[2];
          const uint32 stack    = nuonEnv.mpe[0].commrecv[3];
          // Plausibility: loadAddr in some MPE's IRAM (0x20xxxxxx),
          // srcAddr in main DRAM (0x80xxxxxx).
          if ((loadAddr & 0xFF000000u) == 0x20000000u &&
              (srcAddr  & 0xFF000000u) == 0x80000000u)
          {
            const uint32 size = 0x4000u; // 16 KB blob
            // Write into BOTH worker MPEs (1 and 2). The packet target is
            // MPE0 (the minibios dispatcher) but on real HW MPE0 would
            // perform a cross-MPE DMA into the worker IRAM. Without that,
            // we write directly to mpe[1].dtrom and mpe[2].dtrom at the
            // same offset so both workers see the loaded code in their
            // own coordinate space. Skip MPE0 itself to avoid overwriting
            // the running minibios code.
            void* srcPtr = nuonEnv.GetPointerToMemory(0, srcAddr, false);
            // One-shot per (mpe, srcAddr): avoid re-loading the same
            // module repeatedly which can corrupt worker state if a
            // C2 packet fires while the worker is mid-execution between
            // halt windows. Bounded to 8 src×2 mpe slots for IS3 (we
            // observed 3 unique sources in the trace).
            static uint32 c2_seen_src[8] = {0,0,0,0,0,0,0,0};
            static uint32 c2_seen_mpe[8] = {0,0,0,0,0,0,0,0};
            static uint32 c2_seen_n = 0;
            if (srcPtr) {
              uint32 wrote_to = 0;
              for (uint32 tm = 1; tm <= 2; tm++) {
                // NUANCE_C2_HALTCHECK=1: skip if worker is currently
                // running (original session 094c396 default). Off by
                // default — when workers are running, C2 packets are
                // usually a request to reload a different code page
                // and rare-overwrite worked better historically (more
                // C2 fires → more chances for ismerlin module to load).
                static int s_hc_inited = 0; static int s_hc_on = 0;
                if (!s_hc_inited) { s_hc_inited = 1; s_hc_on = getenv("NUANCE_C2_HALTCHECK") ? 1 : 0; }
                if (s_hc_on && (nuonEnv.mpe[tm].mpectl & MPECTRL_MPEGO)) continue;
                bool already = false;
                for (uint32 k = 0; k < c2_seen_n; k++)
                  if (c2_seen_src[k] == srcAddr && c2_seen_mpe[k] == tm)
                    { already = true; break; }
                if (already) continue;
                void* dstPtr = nuonEnv.GetPointerToMemory(tm, loadAddr, false);
                if (dstPtr && dstPtr != srcPtr) {
                  memcpy(dstPtr, srcPtr, size);
                  wrote_to |= (1u << tm);
                  if (c2_seen_n < 8) {
                    c2_seen_src[c2_seen_n] = srcAddr;
                    c2_seen_mpe[c2_seen_n] = tm;
                    c2_seen_n++;
                  }
                }
              }
              if (wrote_to) {
                static uint64 s_c2_log = 0; s_c2_log++;
                if (s_c2_log <= 20)
                  fprintf(stderr, "[C2-BRIDGE #%llu] load=0x%08X entry=0x%08X src=0x%08X stack=0x%08X size=0x%X wrote=0x%X\n",
                          (unsigned long long)s_c2_log, loadAddr, entryPC, srcAddr, stack, size, wrote_to);
              }

              // NUANCE_C2_START_WORKER=1: also start the loaded worker MPE.
              // On real HW MPE0's NUON code would write MPECTRL_MPEGO to the
              // worker's mpectl after the DMA finishes. Our emulator never
              // gets there because MPE0 spins waiting for the worker's
              // bit-4 ack first (chicken-and-egg). Set pcexec=entryPC and
              // toggle MPEGO directly here to break the deadlock.
              // Verified via NUANCE_LOG_MPESTATE on IS3: mpe2.go=0 throughout
              // the LOADING-screen freeze, MPE0 stuck in $20301BBE bit-4 spin
              // (the WriteToBus helper at $20301B40).
              static int s_sw_inited = 0; static int s_sw_on = 0;
              if (!s_sw_inited) { s_sw_inited = 1; s_sw_on = getenv("NUANCE_C2_START_WORKER") ? 1 : 0; }
              if (s_sw_on && wrote_to) {
                for (uint32 tm = 1; tm <= 2; tm++) {
                  if (!(wrote_to & (1u << tm))) continue;
                  if (nuonEnv.mpe[tm].mpectl & MPECTRL_MPEGO) continue; // already running
                  nuonEnv.mpe[tm].pcexec  = entryPC;
                  nuonEnv.mpe[tm].pcfetch = entryPC;
                  if (stack >= 0x20000000 && stack < 0x21000000) // plausible IRAM stack
                    nuonEnv.mpe[tm].sp = stack;
                  nuonEnv.mpe[tm].mpectl |= MPECTRL_MPEGO;
                  fprintf(stderr, "[C2-BRIDGE] started MPE%u at entry=0x%08X sp=0x%08X\n",
                          tm, entryPC, stack);
                }
              }
            }
          }
        }
      }

      // NUANCE_A0_BRIDGE=1 (default off): when MPE3 sends a CommSendInfo
      // packet with info-byte 0xA0 to MPE0, synthesize the DMA in host code.
      // IS3 packet shape from NUANCE_LOG_COMMBUS_PAYLOAD trace:
      //   w0=src (mainDRAM ptr like 0x800B7B00 / 0x802E3C00)
      //   w1=dst (mainBusDRAM via system-bus alias, e.g. 0x407F0000)
      //   w2=size (bytes, e.g. 0x2000)
      //   w3=flags (0x81)
      // The real MPE0 minibios handler is supposed to execute this as a NUON
      // hardware DMA but our emulation of that DMA-trigger path is incomplete.
      // For IS3 boot path the DMA delivers audio/streaming chunks; without it
      // mcp.run waits forever in csd_wait_for_xmit_complete.
      if (target == 0 && currentTransmitID == 3)
      {
        const uint32 a0InfoByte = nuonEnv.mpe[3].comminfo & 0xFFu;
        static int s_a0_inited = 0; static int s_a0_on = 0;
        if (!s_a0_inited) { s_a0_inited = 1; s_a0_on = getenv("NUANCE_A0_BRIDGE") ? 1 : 0; }
        if (s_a0_on && a0InfoByte == 0xA0u)
        {
          const uint32 srcAddr = nuonEnv.mpe[0].commrecv[0];
          const uint32 dstAddr = nuonEnv.mpe[0].commrecv[1];
          const uint32 size    = nuonEnv.mpe[0].commrecv[2];
          if (size > 0 && size <= 0x100000)
          {
            void* srcPtr = nuonEnv.GetPointerToMemory(0, srcAddr, false);
            void* dstPtr = nuonEnv.GetPointerToMemory(0, dstAddr, false);
            if (srcPtr && dstPtr && srcPtr != dstPtr)
            {
              memcpy(dstPtr, srcPtr, size);
              static uint64 s_a0_log = 0; s_a0_log++;
              if (s_a0_log <= 10 || (s_a0_log % 100) == 0)
                fprintf(stderr, "[A0-BRIDGE #%llu] src=0x%08X dst=0x%08X size=0x%X flags=0x%X\n",
                        (unsigned long long)s_a0_log, srcAddr, dstAddr, size,
                        nuonEnv.mpe[0].commrecv[3]);
            }
          }
        }
      }

      // NUANCE_AF_BRIDGE=1 (default off): when MPE3 sends a CommSendInfo
      // packet with info-byte 0xAF to MPE0, synthesize a response packet
      // back to MPE3's commrecv carrying current controller state. This
      // emulates the real-HW MPE0-minibios controller→comm bridge that's
      // missing from our implementation. T3K's gameplay loop polls this
      // every video field; without a response the game logic freezes
      // waiting for input. See issue andkrau/NuanceResurrection#52.
      //
      // The info byte was already shifted into the target's comminfo
      // high-16-bits during delivery above; we recover the low byte from
      // the source's comminfo (set by CommSendCore prior to delivery).
      if (target == 0 && currentTransmitID == 3)
      {
        const uint32 srcInfo = nuonEnv.mpe[3].comminfo & 0xFFu;
        static int s_inited = 0; static int s_on = 0;
        if (!s_inited) { s_inited = 1; s_on = getenv("NUANCE_AF_BRIDGE") ? 1 : 0; }
        if (s_on && srcInfo == 0xAFu && controller != nullptr)
        {
          // Response shape: word 0 = AB120170 magic (per old IS3 protocol
          // notes), word 1 = packed controller state (16 bits buttons,
          // 8 bits xAxis, 8 bits yAxis), word 2/3 = filler magics that
          // T3K hopefully tolerates.
          MPE &mpe3 = nuonEnv.mpe[3];
          const uint32 buttons = (uint32)SwapBytes(controller[1].buttons);
          const uint8  xAxis   = (uint8)controller[1].d1.xAxis;
          const uint8  yAxis   = (uint8)controller[1].d2.yAxis;
          mpe3.commrecv[0] = 0xAB120170u;
          mpe3.commrecv[1] = (buttons << 16) | ((uint32)xAxis << 8) | (uint32)yAxis;
          mpe3.commrecv[2] = 0x12345678u;
          mpe3.commrecv[3] = 0xDEADBEEFu;
          mpe3.comminfo = (mpe3.comminfo & 0xFFFFu) | ((uint32)0xAF << 16);
          mpe3.commctl &= ~(COMM_SOURCE_ID_BITS);
          mpe3.commctl |= (COMM_RECV_BUFFER_FULL_BIT | (0u << 16));  // source = MPE0
          mpe3.TriggerInterrupt(INT_COMMRECV);
          static uint64 s_log = 0; s_log++;
          if (s_log <= 5 || (s_log % 500) == 0)
            fprintf(stderr, "[AF-BRIDGE #%llu] buttons=0x%04X xy=%d,%d\n",
                    (unsigned long long)s_log, (uint32)SwapBytes(controller[1].buttons),
                    (int8)xAxis, (int8)yAxis);
        }
      }

      LogCommBusEvent("deliver", currentTransmitID, target);
      LogCommBusPayload(currentTransmitID, target,
                        nuonEnv.mpe[target].commrecv[0],
                        nuonEnv.mpe[target].commrecv[1],
                        nuonEnv.mpe[target].commrecv[2],
                        nuonEnv.mpe[target].commrecv[3],
                        nuonEnv.mpe[target].comminfo);
#ifdef LOG_COMM
      fprintf(commLogFile,"Comm packet sent: MPE%ld->MPE%ld, packet contents = {$%lx,$%lx,$%lx,$%lx, comminfo = $%lx\n",
        currentTransmitID,
        target,
        nuonEnv.mpe[currentTransmitID].commxmit[0],
        nuonEnv.mpe[currentTransmitID].commxmit[1],
        nuonEnv.mpe[currentTransmitID].commxmit[2],
        nuonEnv.mpe[currentTransmitID].commxmit[3],
        nuonEnv.mpe[currentTransmitID].comminfo);
      fflush(commLogFile);
#endif
    }
    else
    {
      //xmit failed
      LogCommBusEvent(
          (nuonEnv.mpe[target].commctl & COMM_RECV_BUFFER_FULL_BIT) ?
              "BUSY-recv-full" : "BUSY-disabled",
          currentTransmitID, target);
      if(nuonEnv.mpe[currentTransmitID].commctl & COMM_XMIT_RETRY_BIT)
      {
#ifdef LOG_COMM
        fprintf(commLogFile,"Comm packet failed: MPE%ld->MPE%ld, will retry\n",currentTransmitID,target);
#endif
      }
      else
      {
        //No Retry

        //Set Transmit Failed bit
        nuonEnv.mpe[currentTransmitID].commctl |= COMM_XMIT_FAILED_BIT;
        //Clear Transmit Buffer full bit
        nuonEnv.mpe[currentTransmitID].commctl &= ~(COMM_XMIT_BUFFER_FULL_BIT);
        nuonEnv.pendingCommRequests--;

#ifdef LOG_COMM
        fprintf(commLogFile,"Comm packet failed: MPE%ld->MPE%ld, no retry\n",currentTransmitID,target);
#endif
      }
    }
  }
  else
  {
    //Reserved MPE ID or hardware target (eg audio)

    //Pretend the device received the packet
    nuonEnv.mpe[currentTransmitID].commctl &= (~COMM_XMIT_BUFFER_FULL_BIT);
    nuonEnv.mpe[currentTransmitID].TriggerInterrupt(INT_COMMXMIT);

    if (target < 64) // target is a MPE? (on future HW)
    {
        // Ballistic triggers this case on the title screen
#ifdef LOG_COMM
        fprintf(commLogFile,"Comm invalid MPE target: MPE%ld->MPE%ld\n",currentTransmitID,target);
#endif
    }
    else if(target == COMM_ID_AUDIO)
    {
      //audio target
      uint32 cmdValue = nuonEnv.mpe[currentTransmitID].commxmit[0];
      switch(cmdValue >> 31)
      {
        case 0:
          break;
        case 1:
          cmdValue &= 0x7FFFFFFF;
          if(cmdValue == 0x2C)
          {
            //write channel mode register: update NuonEnviroment variable only for now
            //!! TODO modify SetChannelMode to recreate the sound buffer only if the new
            //channel mode buffer size differs from the previous value.
            assert((nuonEnv.mpe[currentTransmitID].commxmit[1] & ~(ENABLE_WRAP_INT | ENABLE_HALF_INT)) == (nuonEnv.nuonAudioChannelMode & ~(ENABLE_WRAP_INT | ENABLE_HALF_INT))); // for now we only support a change in these two flags
            const uint32 newMode = nuonEnv.mpe[currentTransmitID].commxmit[1];
            static int s_log_inited = 0; static int s_log = 0;
            if (!s_log_inited) { s_log_inited = 1; s_log = getenv("NUANCE_LOG_AUDIO") ? 1 : 0; }
            if (s_log) {
              static uint64 s_n = 0; s_n++;
              if (s_n <= 20 || (s_n % 50) == 0)
                fprintf(stderr, "[AUDIO-CH-MODE #%llu] mpe%u: old=0x%08X new=0x%08X\n",
                        (unsigned long long)s_n, currentTransmitID,
                        nuonEnv.nuonAudioChannelMode, newMode);
            }
            nuonEnv.nuonAudioChannelMode = newMode;
          }
          break;
      }
    }
    else if(target == COMM_ID_VDG)
    {
      //vdg target
      uint32 cmdValue = nuonEnv.mpe[currentTransmitID].commxmit[0];
      switch(cmdValue >> 31)
      {
        case 0:
          break;
        case 1:
          cmdValue &= 0x7FFFFFFF;
          if((cmdValue >= 0x200) && (cmdValue < 0x300))
          {
            //write VDG clut entry
            vdgCLUT[cmdValue - 0x200] = SwapBytes(nuonEnv.mpe[currentTransmitID].commxmit[1]);
          }
          break;
      }
    }

    nuonEnv.pendingCommRequests--;
  }

  if(!bLocked)
  {
    currentTransmitID = ((currentTransmitID + 1) & 0x03U);
  }
}
