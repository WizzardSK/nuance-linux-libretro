// NuanceMain implementation for Linux
#ifndef _WIN32

#include "basetypes.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <GL/glew.h>
#include <GL/gl.h>
#include <mutex>
#include <unistd.h>
#include <sys/stat.h>

#include "byteswap.h"
#include "Utility.h"
#include "comm.h"
#include "GLWindow.h"
#include "audio.h"
#include "mpe.h"
#include "NuonEnvironment.h"
#include "NuonMemoryMap.h"
#include "NuanceRes.h"
#include "joystick.h"
#include "video.h"
#include "ExecuteMEM.h"
#include "timer.h"
#include "Bios.h"
#include "NuanceUI.h"
#include "archive.h"
#include <csignal>

NuonEnvironment nuonEnv;

extern ControllerData *controller;
extern std::mutex gfx_lock;
extern VidChannel structMainChannel, structOverlayChannel;
extern bool bOverlayChannelActive, bMainChannelActive;
extern vidTexInfo videoTexInfo;

extern void SDL2_SwapWindow();

bool bQuit = false;
bool bRun = false;
std::string g_ISOPath;   // path to mounted ISO (for reading data files)
std::string g_ISOPrefix; // NUON directory name inside ISO (e.g. "NUON")
static bool load4firsttime = true;

GLWindow display;

static bool GetMPERunStatus(const uint32 which)
{
  return (nuonEnv.mpe[which & 0x03].mpectl & MPECTRL_MPEGO) != 0;
}

static void SetMPERunStatus(const uint32 which, const bool run)
{
  if(run)
    nuonEnv.mpe[which & 0x03].mpectl |= MPECTRL_MPEGO;
  else
    nuonEnv.mpe[which & 0x03].mpectl &= ~MPECTRL_MPEGO;
}

static void ExecuteSingleStep()
{
  nuonEnv.mpe[3].ExecuteSingleStep();
  nuonEnv.mpe[2].ExecuteSingleStep();
  nuonEnv.mpe[1].ExecuteSingleStep();
  nuonEnv.mpe[0].ExecuteSingleStep();
  if(nuonEnv.pendingCommRequests)
    DoCommBusController();
}

void StopEmulation(int mpeIndex)
{
  bRun = false;
}

bool OnDisplayPaint(WPARAM, LPARAM)
{
  if(bRun)
    RenderVideo(display.clientWidth, display.clientHeight);
  else
    glClear(GL_COLOR_BUFFER_BIT);

  SDL2_SwapWindow();
  return true;
}

// NuanceUI_TogglePause defined in NuanceUI.cpp

bool OnDisplayResize(uint16 width, uint16 height)
{
  glViewport(0, 0, width, height);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_ALPHA_TEST);
  glDisable(GL_LIGHTING);
  glDisable(GL_FOG);
  glClear(GL_COLOR_BUFFER_BIT);
  OnDisplayPaint(0, 0);
  return false;
}

static void ApplyControllerState(const unsigned int controllerIdx, const uint16 buttons)
{
  if (controller)
    controller[controllerIdx].buttons = SwapBytes(buttons);
}

static void Run()
{
  uint32 bpAddr = 0;
  FILE* inFile;
  if (fopen_s(&inFile, "breakpoint.txt", "r") == 0) {
    fscanf(inFile, "%x", &bpAddr);
    fclose(inFile);
  }
  for (int i = 0; i < 4; i++)
    nuonEnv.mpe[i].breakpointAddress = bpAddr;
  bRun = true;
}

bool Load(const char* file)
{
  if (!file) return false;

  std::string actualFile = ResolveGameFile(file);
  if (actualFile.empty()) {
    fprintf(stderr, "Cannot resolve / extract: %s\n", file);
    return false;
  }

  // NUANCE_DEMO_LAUNCH=<basename>: bypass the NUON DVD-menu front-end
  // (nuon.run) and load a specific .cof from the same directory
  // directly. Demo discs (Next Tetris, Interactive Sampler) ship
  // multiple titles behind a DVD-Video selector menu we can't render;
  // this lets the user pick by short name. Example:
  //   NUANCE_DEMO_LAUNCH=tnt ./nuance "Next Tetris.iso"
  if (const char* demoName = getenv("NUANCE_DEMO_LAUNCH")) {
    const size_t lastSlash = actualFile.rfind('/');
    const std::string dir = (lastSlash != std::string::npos)
                                ? actualFile.substr(0, lastSlash + 1)
                                : std::string("");
    const std::string demoCof = dir + demoName + ".cof";
    FILE* probe = fopen(demoCof.c_str(), "rb");
    if (probe) {
      fclose(probe);
      fprintf(stderr, "[DEMO-LAUNCH] redirecting %s -> %s\n",
              actualFile.c_str(), demoCof.c_str());
      actualFile = demoCof;
    } else {
      fprintf(stderr, "[DEMO-LAUNCH] %s not found in %s, falling back\n",
              demoCof.c_str(), dir.c_str());
    }
  }

  // NUANCE_AUTO_DVD=1: kick off DVD-Video playback as soon as the ISO
  // mounts. Most Linux Nuance test cases never reach API_PresentVOB
  // through the NUON-side BIOS path (the demo discs' nuon.run boot
  // does its own setup before calling PE), so this gives the user a
  // way to at least watch the disc's DVD attract video while the NUON
  // side does whatever it does. Off by default — set to 1 for
  // Tetris/Sampler-style demo discs.
  if (getenv("NUANCE_AUTO_DVD")) {
    extern bool DvdPlayerStart(const char* path);
    extern std::string g_ISOPath;
    // Prefer the original .iso path: libdvdread can open .iso files
    // directly, but treats the FUSE mount as a faux block device and
    // bails ("Can't read name block"). Fall back to mount root for
    // non-iso inputs (already-extracted directories).
    std::string dvdPath;
    const size_t inLen = strlen(file);
    if (inLen >= 4 && strcasecmp(file + inLen - 4, ".iso") == 0) {
      dvdPath = file;
    } else {
      dvdPath = g_ISOPath;
    }
    if (!dvdPath.empty()) {
      fprintf(stderr, "[AUTO-DVD] starting: %s\n", dvdPath.c_str());
      if (!DvdPlayerStart(dvdPath.c_str())) {
        const std::string base = g_ISOPath + (g_ISOPath.back() == '/' ? "" : "/");
        const std::string cmd =
            "find \"" + base + "\" -maxdepth 3 -type f -iname 'vts_*.vob' "
            "-printf '%s %p\\n' 2>/dev/null | sort -rn | head -1 | cut -d' ' -f2-";
        FILE* p = popen(cmd.c_str(), "r");
        if (p) {
          char buf[512] = {};
          if (fgets(buf, sizeof(buf), p)) {
            std::string vob(buf);
            while (!vob.empty() && (vob.back() == '\n' || vob.back() == '\r')) vob.pop_back();
            if (!vob.empty()) {
              fprintf(stderr, "[AUTO-DVD] dvdnav failed, falling back to: %s\n", vob.c_str());
              DvdPlayerStart(vob.c_str());
            }
          }
          pclose(p);
        }
      }
    }
  }

  // Try loading as NUONROM-DISK/Bles first, then as raw COFF
  bool bSuccess = nuonEnv.mpe[3].LoadNuonRomFile(actualFile.c_str());
  if (!bSuccess) {
    bSuccess = nuonEnv.mpe[3].LoadCoffFile(actualFile.c_str());
    if (!bSuccess) {
      fprintf(stderr, "Cannot open file or Invalid COFF/NUONROM-DISK/Bles file: %s\n", actualFile.c_str());
      return false;
    }
  }
  fprintf(stderr, "Loaded successfully: %s\n", actualFile.c_str());
  if (const char* dumpPath = getenv("NUANCE_DUMP_BIN")) {
    // NUANCE_DUMP_BIN=/tmp/dump.bin: write 0x80030000..0x8003F000 (game
    // sysram, ~64 KB) to a raw binary file for offline disassembly via
    // the NUON SDK vmdisasm tool.
    const uint32 base = 0x80030000;
    const uint32 size = 0x10000;
    FILE* f = fopen(dumpPath, "wb");
    if (f) {
      uint8* p = (uint8*)nuonEnv.GetPointerToMemory(0, base);
      fwrite(p, 1, size, f);
      fclose(f);
      fprintf(stderr, "[DUMP-BIN] wrote 0x%X bytes from 0x%08X to %s\n", size, base, dumpPath);
    }
  }

  if (bSuccess) {
    nuonEnv.SetDVDBaseFromFileName(actualFile.c_str());
    nuonEnv.mpe[3].Go();
    Run();
    return true;
  }
  return false;
}

// Halt all 4 MPEs — used by the F12 skip-cutscene hotkey when fmv.run
// is stuck waiting on FMV hardware we don't emulate. Clears
// MPECTRL_MPEGO so the MPEs stop fetching/executing. Best-effort
// escape hatch: works for IS3-style "play logo, then continue" flows
// where the only thing keeping the game in cutscene state is fmv.run
// spinning; many other games will hang somewhere else after.
void NuanceMain_HaltMPE3()
{
  for (int i = 0; i < 4; i++)
    nuonEnv.mpe[i].mpectl &= ~MPECTRL_MPEGO;
  fprintf(stderr, "[F12] all 4 MPEs halted (mpectl &= ~MPEGO)\n");
}

// Force-load a NUON COFF module from a file path into emulator
// memory, flushing JIT for overwritten regions. Returns the entry
// point (paddr of section 0) on success, 0 on failure. Used by
// NUANCE_FORCE_LOAD_COFF=<path> to swap in a different programs.dat
// module at runtime when mcp.run keeps fmv.run loaded as the menu
// attract loop.
static uint32 ForceLoadCoff(const char* path)
{
  FILE* f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "[FORCE-LOAD] open failed: %s\n", path); return 0; }
  fseek(f, 0, SEEK_END);
  long fsz = ftell(f);
  fseek(f, 0, SEEK_SET);
  std::vector<uint8> buf(fsz);
  if ((long)fread(buf.data(), 1, fsz, f) != fsz) {
    fclose(f);
    fprintf(stderr, "[FORCE-LOAD] read failed: %s\n", path);
    return 0;
  }
  fclose(f);

  const uint16 magic  = (buf[0] << 8) | buf[1];
  const uint16 nscns  = (buf[2] << 8) | buf[3];
  const uint16 opthdr = (buf[16] << 8) | buf[17];
  if (magic != 0x0120) {
    fprintf(stderr, "[FORCE-LOAD] not a NUON COFF: %s (magic=0x%04X)\n", path, magic);
    return 0;
  }
  const uint32 entry =
      ((uint32)buf[20] << 24) | ((uint32)buf[21] << 16) |
      ((uint32)buf[22] <<  8) | ((uint32)buf[23]);
  const uint32 secOffs = 20 + opthdr;
  fprintf(stderr, "[FORCE-LOAD] %s: nscns=%d entry=0x%08X\n", path, nscns, entry);
  for (int i = 0; i < nscns; i++) {
    const uint8* h = buf.data() + secOffs + i * 44;
    auto rd32 = [](const uint8* p) {
      return ((uint32)p[0] << 24) | ((uint32)p[1] << 16) |
             ((uint32)p[2] <<  8) | ((uint32)p[3]);
    };
    const uint32 paddr  = rd32(h + 8);
    const uint32 size   = rd32(h + 16);
    const uint32 scnptr = rd32(h + 20);
    const uint32 flags  = rd32(h + 40);
    if (flags & 0x282) continue;
    if (size == 0)     continue;
    uint8* dst = (uint8*)nuonEnv.GetPointerToMemory(0, paddr);
    if (!dst) { fprintf(stderr, "  sec %d: paddr 0x%08X unmapped, skip\n", i, paddr); continue; }
    memcpy(dst, buf.data() + scnptr, size);
    const uint32 endAddr = paddr + size - 1;
    for (int m = 0; m < 4; m++) {
      nuonEnv.mpe[m].nativeCodeCache.FlushRegion(paddr, endAddr);
      nuonEnv.mpe[m].InvalidateICacheRegion(paddr, endAddr);
    }
    fprintf(stderr, "  sec %d paddr=0x%08X size=0x%X copied\n", i, paddr, size);
  }
  return entry;
}

// Run cleanup on SIGTERM/SIGINT/SIGHUP so timeout-killed runs (and
// Ctrl-C during testing) don't leave behind stale fuseiso mounts in
// /tmp/nuance_*. CleanupArchives() invokes `fusermount -uz` for each
// tracked mount; safe to call from a signal handler in practice
// because system() / vector iteration are reachable from this point.
static void OnFatalSignal(int sig)
{
  CleanupArchives();
  // Re-raise with default handler so the process actually dies with
  // the right exit code (timeout / shells expect 128+sig).
  signal(sig, SIG_DFL);
  raise(sig);
}

int main(int argc, char* argv[])
{
#ifdef USE_ASMJIT
  extern bool asmjit_selftest();
  asmjit_selftest();
#endif

  signal(SIGTERM, OnFatalSignal);
  signal(SIGINT,  OnFatalSignal);
  signal(SIGHUP,  OnFatalSignal);
  atexit(CleanupArchives);

  init_supported_CPU_extensions();

  GenerateMirrorLookupTable();
  GenerateSaturateColorTables();

  nuonEnv.Init();

  display.applyControllerState = ApplyControllerState;
  display.resizeHandler = OnDisplayResize;
  display.paintHandler = OnDisplayPaint;

  display.Create();

  // Initialize OpenGL viewport
  OnDisplayResize(display.clientWidth, display.clientHeight);

  if (argc > 1) {
    if (Load(argv[1]))
      load4firsttime = false;
  } else {
    printf("Usage: nuance <rom_file>\n");
    printf("  Supported formats: .nuon, .cof\n");
  }

  nuonEnv.videoDisplayCycleCount = 0;
  nuonEnv.MPE3wait_fieldCounter = 0;

  while (!bQuit)
  {
    display.MessagePump();

    uint64 cycles = 0;
    while (bRun && !nuonEnv.trigger_render_video)
    {
      cycles++;

      // NUANCE_MPE_RATIO=<mpe0>:<mpe1>:<mpe2>:<mpe3> — how many cycles
      // each MPE runs per main loop iteration (default 1:1:1:1).
      // IS3 needs workers (MPE0-2) to keep up with MPE3's high-rate
      // MPEStop/MPERun dispatch; try `1:10:10:1` to give workers 10x
      // cycle budget so they can complete tasks before MPE3 interrupts.
      static int s_ratio[4] = {1, 1, 1, 1};
      static int s_ratio_inited = 0;
      if (!s_ratio_inited) {
        s_ratio_inited = 1;
        if (const char* s = getenv("NUANCE_MPE_RATIO")) {
          int v[4] = {1,1,1,1};
          if (sscanf(s, "%d:%d:%d:%d", &v[0], &v[1], &v[2], &v[3]) == 4) {
            for (int j = 0; j < 4; j++)
              s_ratio[j] = (v[j] >= 1 && v[j] <= 100) ? v[j] : 1;
            fprintf(stderr, "[MPE-RATIO] %d:%d:%d:%d\n",
                    s_ratio[0], s_ratio[1], s_ratio[2], s_ratio[3]);
          }
        }
      }

      // NUANCE_AUTO_YIELD=<threshold>[:<boost>]: auto-detect MPE3 spinwait
      // and dynamically boost worker MPE cycles. Threshold is the number
      // of consecutive iterations MPE3 must spend at the same PC before
      // we declare it spinning; boost is the multiplier for workers'
      // s_ratio while spinning is active (default boost=50).
      //
      // Rationale: IS3 (and similar high-dispatch-rate engines) spinwait
      // on memory slots in MPE3 after MPEStop/MPERun. The fixed MPE_RATIO
      // doesn't help if the spinwait is JIT-compiled tight enough to be
      // much faster than the worker code per cycle. Detecting the spin
      // and shifting cycles to workers lets them complete the task and
      // write the response slot. Example: `NUANCE_AUTO_YIELD=200:100`.
      static int s_yield_inited = 0;
      static uint32 s_yield_threshold = 0;  // 0 = disabled
      static uint32 s_yield_boost = 50;
      if (!s_yield_inited) {
        s_yield_inited = 1;
        if (const char* s = getenv("NUANCE_AUTO_YIELD")) {
          uint32 t = 0, b = 0;
          int n = sscanf(s, "%u:%u", &t, &b);
          if (n >= 1) {
            s_yield_threshold = t;
            if (n == 2 && b > 0) s_yield_boost = b;
            fprintf(stderr, "[AUTO-YIELD] threshold=%u boost=%u\n",
                    s_yield_threshold, s_yield_boost);
          }
        }
      }
      static uint32 s_last_mpe3_pc = 0;
      static uint32 s_same_pc_count = 0;
      static bool s_yielding = false;
      if (s_yield_threshold > 0) {
        const uint32 cur_pc = nuonEnv.mpe[3].pcexec;
        if (cur_pc == s_last_mpe3_pc) {
          s_same_pc_count++;
        } else {
          if (s_yielding) {
            fprintf(stderr, "[AUTO-YIELD] MPE3 moved 0x%08X -> 0x%08X after %u iters, normal\n",
                    s_last_mpe3_pc, cur_pc, s_same_pc_count);
          }
          s_same_pc_count = 0;
          s_last_mpe3_pc = cur_pc;
          s_yielding = false;
        }
        if (!s_yielding && s_same_pc_count >= s_yield_threshold) {
          fprintf(stderr, "[AUTO-YIELD] MPE3 stuck at 0x%08X for %u iters, boosting workers %ux\n",
                  cur_pc, s_same_pc_count, s_yield_boost);
          s_yielding = true;
        }
      }

      for (int i = 3; i >= 0; --i) {
        if (i == 3 && nuonEnv.MPE3wait_fieldCounter != 0) continue;
        int reps = s_ratio[i];
        if (s_yielding && i != 3) reps *= s_yield_boost;
        for (int k = 0; k < reps; k++)
          nuonEnv.mpe[i].FetchDecodeExecute();
      }

      // NUANCE_LOG_MPESTATE=<sec>: every <sec> seconds, log go/pcexec
      // for all 4 MPEs. Lets us see whether MPE0 minibios is actually
      // ticking during IS3 attract → menu handoff.
      {
        static uint64 s_log_mpestate_us = 0;
        static int s_log_mpestate_inited = 0;
        if (!s_log_mpestate_inited) {
          s_log_mpestate_inited = 1;
          if (const char* s = getenv("NUANCE_LOG_MPESTATE"))
            s_log_mpestate_us = (uint64)strtoull(s, nullptr, 0) * 1000000ull;
        }
        if (s_log_mpestate_us > 0) {
          static uint64 next_us = 0;
          const uint64 now = useconds_since_start();
          if (now >= next_us) {
            next_us = now + s_log_mpestate_us;
            fprintf(stderr, "[MPESTATE] @%llus", (unsigned long long)(now/1000000));
            for (int i = 0; i < 4; i++) {
              const int go = (nuonEnv.mpe[i].mpectl & 2) ? 1 : 0;
              fprintf(stderr, " mpe%d{go=%d pc=0x%08X intsrc=0x%X commctl=0x%X}",
                      i, go, nuonEnv.mpe[i].pcexec,
                      nuonEnv.mpe[i].intsrc, nuonEnv.mpe[i].commctl);
            }
            fprintf(stderr, "\n");
          }
        }
      }

      if (nuonEnv.pendingCommRequests)
        DoCommBusController();

      // NUANCE_IS3_FORCE_MENU=<idx>: pin IS3 mcp.run's module-selector
      // state variable at 0x80022704 to <idx>. This was the original
      // (incorrect) hypothesis for the IS3 menu→game handoff; kept as
      // a harness because it still forces the secondary state flag.
      // The real selector is *0x8002125C — see NUANCE_IS3_STATE below.
      {
        static int s_inited = 0;
        static int s_idx = -1;
        if (!s_inited) {
          s_inited = 1;
          if (const char* s = getenv("NUANCE_IS3_FORCE_MENU"))
            s_idx = (int)strtol(s, nullptr, 0);
        }
        if (s_idx >= 0 && (cycles & 0x7FF) == 0) {
          if (uint32* p = (uint32*)nuonEnv.GetPointerToMemory(3, 0x80022704)) {
            const uint32 cur = SwapBytes(*p);
            if (cur != (uint32)s_idx && cur != 0xCDCDCDCDu) {
              fprintf(stderr, "[IS3-FORCE] *0x80022704: 0x%08X -> %d\n", cur, s_idx);
              *p = SwapBytes((uint32)s_idx);
            }
          }
        }
      }

      // NUANCE_IS3_STATE=<value>: poke mcp.run's *0x8002125C state
      // selector ONCE when it first becomes initialized (transitions
      // from 0xCDCDCDCD to a real value, normally 0x12 = attract).
      // This kicks the state machine out of attract into the chosen
      // state, then leaves it alone so the natural state-transition
      // logic can run. Useful values:
      //   0    -> calls the "go to menu" sub at 0x800182E0 which sets
      //           state=0x64 and returns r0=3 (menu.run)
      //   0x12 -> "play next attract video" (live state in normal run)
      //   0x64 -> menu state; handler 0x800184D0 calls 0x80018312
      // See nuance-is3-mcp.md for the switch table dump.
      {
        static int s_inited = 0;
        static int s_state = -1;
        static int s_done = 0;
        static uint32 s_lastSeen = 0xCDCDCDCDu;
        if (!s_inited) {
          s_inited = 1;
          if (const char* s = getenv("NUANCE_IS3_STATE"))
            s_state = (int)strtol(s, nullptr, 0);
        }
        if (s_state >= 0 && !s_done && (cycles & 0xFF) == 0) {
          if (uint32* p = (uint32*)nuonEnv.GetPointerToMemory(3, 0x8002125C)) {
            const uint32 cur = SwapBytes(*p);
            // Only fire once: when we first see the var as a real
            // value (game initialised it past the 0xCDCDCDCD bss
            // sentinel), poke it and stop touching it after.
            if (cur != 0xCDCDCDCDu && cur != (uint32)s_state) {
              fprintf(stderr, "[IS3-STATE] one-shot poke *0x8002125C: 0x%08X -> 0x%X (cycle=%llu)\n",
                      cur, s_state, (unsigned long long)cycles);
              *p = SwapBytes((uint32)s_state);
              s_done = 1;
            }
            s_lastSeen = cur;
          }
        }
      }

      // Periodic MPE state dump for debugging stuck conditions.
      // Use a static counter so frame boundaries (which reset `cycles`)
      // don't break the cadence — past a single frame all MPESTATEs would
      // otherwise be lost.
      static int dump_inited = 0; static uint64 dump_period = 0; static uint64 dump_total = 0;
      if (!dump_inited) { dump_inited = 1; const char* s = getenv("NUANCE_DUMP_MPE_STATE"); if (s) dump_period = (uint64)atoll(s); }
      dump_total++;
      if (dump_period > 0 && (dump_total % dump_period) == 0) {
        fprintf(stderr, "[MPESTATE] mpe0 pc=$%08X go=%d  mpe1 pc=$%08X go=%d  mpe2 pc=$%08X go=%d  mpe3 pc=$%08X go=%d\n",
                nuonEnv.mpe[0].pcexec, (nuonEnv.mpe[0].mpectl & 2) ? 1 : 0,
                nuonEnv.mpe[1].pcexec, (nuonEnv.mpe[1].mpectl & 2) ? 1 : 0,
                nuonEnv.mpe[2].pcexec, (nuonEnv.mpe[2].mpectl & 2) ? 1 : 0,
                nuonEnv.mpe[3].pcexec, (nuonEnv.mpe[3].mpectl & 2) ? 1 : 0);
      }
      if ((cycles % 500) == 0)
      {
        static uint64 last_time0 = useconds_since_start();
        static uint64 last_time1 = useconds_since_start();
        static uint64 last_time2 = useconds_since_start();
        static uint64 last_time3 = useconds_since_start();
        const uint64 new_time = useconds_since_start();

        if (nuonEnv.timer_rate[0] > 0) {
          if (new_time >= last_time0 + (uint64)nuonEnv.timer_rate[0]) {
            nuonEnv.ScheduleInterrupt(INT_SYSTIMER0);
            last_time0 = new_time;
          }
        } else last_time0 = new_time;

        if (nuonEnv.timer_rate[1] > 0) {
          if (new_time >= last_time1 + (uint64)nuonEnv.timer_rate[1]) {
            nuonEnv.ScheduleInterrupt(INT_SYSTIMER1);
            last_time1 = new_time;
          }
        } else last_time1 = new_time;

        if (nuonEnv.timer_rate[2] > 0) {
          if (new_time >= last_time2 + (uint64)nuonEnv.timer_rate[2]) {
            IncrementVideoFieldCounter();
            nuonEnv.TriggerVideoInterrupt();
            nuonEnv.trigger_render_video = true;
            const uint32 fieldCounter = SwapBytes(*((uint32*)&nuonEnv.systemBusDRAM[VIDEO_FIELD_COUNTER_ADDRESS & SYSTEM_BUS_VALID_MEMORY_MASK]));
            if (fieldCounter >= nuonEnv.MPE3wait_fieldCounter)
              nuonEnv.MPE3wait_fieldCounter = 0;
            last_time2 = new_time;

            // NUANCE_BTN_QUEUE=tok:frames,tok:frames,...  : drive
            // controller[1].buttons directly from a per-video-field
            // schedule. Bypasses xdotool/X11 entirely so timing is
            // deterministic relative to the game's own polling cadence.
            // Tokens: A B Up Down Left Right START NUON L R _ (idle).
            // Optional NUANCE_BTN_QUEUE_DELAY=<fields> skips the first
            // N video fields before starting (use ~7800 = 130 s @ 60Hz
            // to wait through the IS3 attract/setup phase).
            // Optional NUANCE_BTN_QUEUE_AFTER_MPX=1: gate the delay
            // countdown on MPX cutscene EOF (deterministic regardless of
            // CPU speed). The DELAY then counts fields AFTER MPX ended.
            // Example to navigate IS3 menu:
            //   NUANCE_BTN_QUEUE=A:20,_:60,Down:20,_:60,A:20,_:120
            //   NUANCE_BTN_QUEUE_DELAY=7800
            {
              static int s_inited = 0;
              static std::vector<std::pair<uint16,uint32>> s_queue;
              static size_t s_qIdx = 0;
              static uint32 s_qRemaining = 0;
              static uint32 s_delayLeft = 0;
              static int s_afterMpx = 0;

              if (!s_inited) {
                s_inited = 1;
                if (getenv("NUANCE_BTN_QUEUE_AFTER_MPX")) s_afterMpx = 1;
                if (const char* spec = getenv("NUANCE_BTN_QUEUE")) {
                  if (const char* d = getenv("NUANCE_BTN_QUEUE_DELAY"))
                    s_delayLeft = (uint32)strtoul(d, nullptr, 0);
                  std::string tok;
                  std::string s = spec;
                  s += ',';
                  for (char c : s) {
                    if (c == ',' || c == ' ') {
                      if (!tok.empty()) {
                        size_t colon = tok.find(':');
                        std::string name = (colon == std::string::npos) ? tok : tok.substr(0, colon);
                        uint32 frames = (colon == std::string::npos) ? 1
                                          : (uint32)strtoul(tok.c_str() + colon + 1, nullptr, 0);
                        uint16 mask = 0;
                        if      (name == "A")     mask = 1u << CTRLR_BITNUM_BUTTON_A;
                        else if (name == "B")     mask = 1u << CTRLR_BITNUM_BUTTON_B;
                        else if (name == "Up")    mask = 1u << CTRLR_BITNUM_DPAD_UP;
                        else if (name == "Down")  mask = 1u << CTRLR_BITNUM_DPAD_DOWN;
                        else if (name == "Left")  mask = 1u << CTRLR_BITNUM_DPAD_LEFT;
                        else if (name == "Right") mask = 1u << CTRLR_BITNUM_DPAD_RIGHT;
                        else if (name == "START") mask = 1u << CTRLR_BITNUM_BUTTON_START;
                        else if (name == "NUON")  mask = 1u << CTRLR_BITNUM_BUTTON_NUON;
                        else if (name == "L")     mask = 1u << CTRLR_BITNUM_BUTTON_L;
                        else if (name == "R")     mask = 1u << CTRLR_BITNUM_BUTTON_R;
                        else if (name == "_" || name == "0") mask = 0;
                        else { fprintf(stderr, "[BTN-QUEUE] unknown token: '%s'\n", name.c_str()); }
                        if (frames > 0) s_queue.emplace_back(mask, frames);
                        tok.clear();
                      }
                    } else {
                      tok += c;
                    }
                  }
                  fprintf(stderr, "[BTN-QUEUE] parsed %zu entries, delay=%u fields\n",
                          s_queue.size(), s_delayLeft);
                  for (size_t i = 0; i < s_queue.size(); i++)
                    fprintf(stderr, "  [%zu] mask=0x%04X frames=%u\n",
                            i, s_queue[i].first, s_queue[i].second);
                }
              }

              if (!s_queue.empty() && s_qIdx < s_queue.size()) {
                // Latch: stays gated until MPX hits EOF for the first time,
                // then stays ungated forever (queue runs even after decoder
                // is torn down).
                static bool s_mpxGateCleared = false;
                bool gated = false;
                if (s_afterMpx && !s_mpxGateCleared) {
                  extern bool MpxDecoderActive_Probe(uint32*, uint32*);
                  extern bool MpxDecoderActive_IsAtEnd();
                  uint32 mw = 0, mh = 0;
                  if (MpxDecoderActive_Probe(&mw, &mh) && MpxDecoderActive_IsAtEnd()) {
                    s_mpxGateCleared = true;
                    fprintf(stderr, "[BTN-QUEUE] MPX gate cleared, starting delay countdown\n");
                  } else {
                    gated = true;
                  }
                }
                if (gated) {
                  // do nothing — wait for MPX to finish
                } else if (s_delayLeft > 0) {
                  s_delayLeft--;
                } else {
                  if (s_qRemaining == 0) {
                    s_qRemaining = s_queue[s_qIdx].second;
                    fprintf(stderr, "[BTN-QUEUE] entry %zu/%zu mask=0x%04X frames=%u\n",
                            s_qIdx, s_queue.size(), s_queue[s_qIdx].first, s_qRemaining);
                  }
                  if (controller) {
                    const uint16 mask = s_queue[s_qIdx].first;
                    controller[1].buttons = SwapBytes((uint16)mask);
                  }
                  s_qRemaining--;
                  if (s_qRemaining == 0) s_qIdx++;
                }
              }
            }
          }
        } else last_time2 = new_time;

        // audTimer — push one Nuon audio period into the host audio ring (byte-swapped), advance the
        // DMA half pointer, fire INT_AUDIO. Ring full -> skip this iteration
        if (nuonEnv.timer_rate[2] > 0) {
          if (nuonEnv.pNuonAudioBuffer &&
              (new_time >= last_time3 + (uint64)nuonEnv.timer_rate[2]) &&
              ((nuonEnv.nuonAudioChannelMode & (ENABLE_WRAP_INT | ENABLE_HALF_INT)) != (nuonEnv.oldNuonAudioChannelMode & (ENABLE_WRAP_INT | ENABLE_HALF_INT))) &&
              ((((nuonEnv.mpe[0].intsrc & nuonEnv.mpe[0].inten1) | (nuonEnv.mpe[1].intsrc & nuonEnv.mpe[1].inten1) | (nuonEnv.mpe[2].intsrc & nuonEnv.mpe[2].inten1) | (nuonEnv.mpe[3].intsrc & nuonEnv.mpe[3].inten1)) & INT_AUDIO) == 0)) {
            if (nuonEnv.TryPushAudioPeriod())
              last_time3 = new_time;
          }
        } else last_time3 = new_time;
      }

      nuonEnv.TriggerScheduledInterrupts();
    }

    // mcp.run module table watch — log mem[0x800226D0..0x80022740]
    // every 3 seconds + dereference pointers at 0x80023350-0x800235A0.
    if (getenv("NUANCE_WATCH_MOD_TABLE")) {
      static uint64 last_dump_us = 0;
      const uint64 now = useconds_since_start();
      if (now - last_dump_us > 3000000) {
        last_dump_us = now;
        fprintf(stderr, "[MOD-TABLE] @ %llus header:\n", (unsigned long long)(now/1000000));
        for (uint32 a = 0x800226D0; a < 0x80022740; a += 4) {
          uint32* p = (uint32*)nuonEnv.GetPointerToMemory(3, a);
          if (p) fprintf(stderr, "  [0x%08X]=0x%08X\n", a, SwapBytes(*p));
        }
        // Pointer table records — each pointer may be a module entry.
        // Pointers we've seen: 0x80023358, 0x80023364, 0x80023370, 0x8002337C, 0x80023388, 0x80023394.
        // Each looks 12 bytes apart — likely a 12-byte struct.
        fprintf(stderr, "[MOD-TABLE] entries:\n");
        for (uint32 a = 0x80023358; a < 0x80023400; a += 12) {
          uint32* p0 = (uint32*)nuonEnv.GetPointerToMemory(3, a);
          uint32* p1 = (uint32*)nuonEnv.GetPointerToMemory(3, a + 4);
          uint32* p2 = (uint32*)nuonEnv.GetPointerToMemory(3, a + 8);
          if (p0 && p1 && p2)
            fprintf(stderr, "  [0x%08X] %08X %08X %08X\n", a,
                    SwapBytes(*p0), SwapBytes(*p1), SwapBytes(*p2));
        }
      }
    }

    // Once any MPX decoder has completed (or skip pending), profile
    // MPE3 PC into a histogram. After 30 s of post-EOF wall time, dump
    // top 30 PCs. This shows where MPE3 is now stuck in the actual
    // game state machine.
    {
      static std::unordered_map<uint32, uint64> pc_hits;
      static uint64 sample_total = 0;
      static uint64 sample_start_us = 0;
      static bool dumped = false;
      extern bool MpxDecoderActive_IsAtEnd();
      if (!dumped && MpxDecoderActive_IsAtEnd()) {
        uint32 pc = nuonEnv.mpe[3].pcexec;
        pc_hits[pc]++;
        sample_total++;
        if (sample_start_us == 0) {
          sample_start_us = useconds_since_start();
          fprintf(stderr, "[GAME-PC] sampling start (mpe3 pc=0x%08X go=%d)\n",
                  pc, (nuonEnv.mpe[3].mpectl & 2) ? 1 : 0);
        }
        // Watch the suspected audio-buffer counters at 0x800BC468 +
        // 0x800BC46C — fmv.run's spin loop at 0x80031628 calls helper
        // 0x80030374 which exits when mem[BC46C] + 2 > 0xFF.
        if ((sample_total & 0x1FF) == 0) {
          uint32* p46c = (uint32*)nuonEnv.GetPointerToMemory(3, 0x800BC46C);
          uint32* p468 = (uint32*)nuonEnv.GetPointerToMemory(3, 0x800BC468);
          uint32* p470 = (uint32*)nuonEnv.GetPointerToMemory(3, 0x800BC470);
          fprintf(stderr, "[BC46x] 468=0x%08X 46C=0x%08X 470=0x%08X\n",
                  p468 ? SwapBytes(*p468) : 0,
                  p46c ? SwapBytes(*p46c) : 0,
                  p470 ? SwapBytes(*p470) : 0);
        }
        // NUANCE_FORCE_AUDIOCNT=1: pin the audio buffer counters at
        // 0xFF so fmv.run's spin loop at 0x80031628 (poll helper at
        // 0x80030374 which exits when mem[BC46C] > 0xFD) returns 1
        // and the post-menu loading state advances. Without audio
        // MPE emulation these counters never grow.
        // NUANCE_FORCE_AUDIOCNT_OFF_AFTER=<seconds> stops pinning
        // after that many seconds of post-MPX-EOF wall time, so
        // games like IS3 that move on to a different module past
        // attract (ismerlin.run, gameplay engine) don't have their
        // own state at 0x800BC4xx clobbered by the still-firing knob.
        if (getenv("NUANCE_FORCE_AUDIOCNT")) {
          static int s_off_inited = 0;
          static uint64 s_off_after_us = 0;
          if (!s_off_inited) {
            s_off_inited = 1;
            if (const char* s = getenv("NUANCE_FORCE_AUDIOCNT_OFF_AFTER"))
              s_off_after_us = (uint64)strtoull(s, nullptr, 0) * 1000000ull;
          }
          const bool off_now = (s_off_after_us > 0) &&
              (useconds_since_start() - sample_start_us >= s_off_after_us);
          if (!off_now) {
            for (uint32 a = 0x800BC468; a <= 0x800BC478; a += 4) {
              if (uint32* p = (uint32*)nuonEnv.GetPointerToMemory(3, a))
                *p = SwapBytes(0xFFu);
            }
          }
        }
        // First dump at 30 s of post-EOF wall time, then re-dump every
        // NUANCE_GAME_PC_PERIOD seconds (default 30) — clears the
        // histogram between dumps so each window shows the CURRENT
        // hot loop, not the cumulative one.
        static uint64 next_dump_us = 30000000;
        static uint64 dump_period_us = 30000000;
        static int s_period_inited = 0;
        if (!s_period_inited) {
          s_period_inited = 1;
          if (const char* s = getenv("NUANCE_GAME_PC_PERIOD"))
            dump_period_us = (uint64)strtoull(s, nullptr, 0) * 1000000ull;
        }
        if (useconds_since_start() - sample_start_us > next_dump_us) {
          std::vector<std::pair<uint32, uint64>> sorted(pc_hits.begin(), pc_hits.end());
          std::sort(sorted.begin(), sorted.end(),
                    [](auto& a, auto& b){ return a.second > b.second; });
          fprintf(stderr, "[GAME-PC] @%llus %llu samples, %zu unique PCs:\n",
                  (unsigned long long)((useconds_since_start() - sample_start_us)/1000000),
                  (unsigned long long)sample_total, sorted.size());
          int n = (int)sorted.size(); if (n > 15) n = 15;
          for (int i = 0; i < n; i++)
            fprintf(stderr, "[GAME-PC] #%2d pc=0x%08X hits=%llu (%.1f%%)\n",
                    i, sorted[i].first,
                    (unsigned long long)sorted[i].second,
                    100.0 * sorted[i].second / sample_total);
          // Clear for next window
          pc_hits.clear();
          sample_total = 0;
          next_dump_us += dump_period_us;
          dumped = (dump_period_us == 0); // legacy mode if period=0
        }
      }
    }

    // Auto-skip: if libavcodec hit EOF but the game has NOT opened a
    // new .mpx within NUANCE_AUTOSKIP_SEC seconds (default 5),
    // automatically tear down the decoder to force fmv.run advance.
    // Helps long-running cutscenes (IS3 intro = 8 minutes) where the
    // user shouldn't have to press F12 manually. Set NUANCE_AUTOSKIP=0
    // to disable.
    {
      static uint64 eof_start_us = 0;
      static uint64 last_decoder_token = 0;
      static int autoskip_sec = -1;
      if (autoskip_sec == -1) {
        const char* v = getenv("NUANCE_AUTOSKIP");
        // Accept any non-negative integer: 0 disables, >0 = seconds.
        // Default 5 when unset. Previously only "0" was honored and any
        // other value silently fell back to 5, which surprised people.
        if (!v) autoskip_sec = 5;
        else    autoskip_sec = atoi(v);
        if (autoskip_sec < 0) autoskip_sec = 0;
      }
      extern bool MpxDecoderActive_IsAtEnd();
      extern void* MpxDecoderActive_Token();
      void* tok = MpxDecoderActive_Token();
      uint64 token = (uint64)tok;
      if (token != last_decoder_token) {
        // New decoder (or torn down) — reset EOF wait timer.
        eof_start_us = 0;
        last_decoder_token = token;
      }
      if (autoskip_sec > 0 && tok && MpxDecoderActive_IsAtEnd()) {
        if (eof_start_us == 0) eof_start_us = useconds_since_start();
        else if (useconds_since_start() - eof_start_us > (uint64)autoskip_sec * 1000000) {
          extern void MpxSkipCutscene();
          fprintf(stderr, "[AUTOSKIP] %ds post-EOF without advance, tearing down decoder\n",
                  autoskip_sec);
          MpxSkipCutscene();
          eof_start_us = 0;
        }
      }
    }

    // NUANCE_FORCE_LOAD_COFF=<path>: when MPE3 is in fmv.run range
    // and an MPX decoder has just been torn down (autoskip or
    // attract-loop teardown), load the given COFF file over fmv.run
    // and teleport MPE3 to its entry point. Last-ditch attempt to
    // swap in the menu UI module after fmv.run won't naturally
    // return to mcp.run.
    if (const char* coffpath = getenv("NUANCE_FORCE_LOAD_COFF")) {
      static uint32 last_token = 0;
      static int teardown_count = 0;
      static bool loaded = false;
      static int s_threshold = -1;
      if (s_threshold < 0) {
        const char* t = getenv("NUANCE_FORCE_LOAD_COFF_AFTER");
        s_threshold = t ? (int)strtol(t, nullptr, 0) : 2;
        if (s_threshold < 1) s_threshold = 1;
      }
      extern void* MpxDecoderActive_Token();
      uint32 cur_token = (uint32)(uintptr_t)MpxDecoderActive_Token();
      if (cur_token == 0 && last_token != 0) {
        teardown_count++;
        fprintf(stderr, "[FORCE-LOAD] decoder teardown #%d (threshold %d)\n",
                teardown_count, s_threshold);
      }
      last_token = cur_token;
      if (!loaded && teardown_count >= s_threshold) {
        loaded = true;
        // Halt MPE3 first so we don't execute partially-written code.
        const uint32 prev_ctl = nuonEnv.mpe[3].mpectl;
        nuonEnv.mpe[3].mpectl &= ~2u;  // clear MPEGO
        const uint32 entry = ForceLoadCoff(coffpath);
        if (entry) {
          fprintf(stderr, "[FORCE-LOAD] teleporting MPE3 0x%08X -> 0x%08X (was r31=0x%08X)\n",
                  nuonEnv.mpe[3].pcexec, entry, nuonEnv.mpe[3].regs[31]);
          nuonEnv.mpe[3].pcexec = entry;
          // Reset MPE3 stack to a known-safe value (the new module's
          // prologue should set r31 itself, but provide a reasonable
          // default in case it doesn't).
          nuonEnv.mpe[3].regs[31] = 0x80800000;
        }
        nuonEnv.mpe[3].mpectl = prev_ctl | 2u;  // resume MPE3
      }
    }

    // NUANCE_STACK_WALK=1: GENUINE-stuck heuristic for fmv.run ->
    // mcp.run return. Scans MPE3's stack at r31 for a saved return
    // address pointing into mcp.run (0x80018000-0x800239A0) and
    // teleports pcexec there. Only fires after the LAST .mpx file
    // (which is currently open) has been EOF AND auto-skip has
    // already torn it down — i.e. mcp.run definitely isn't going to
    // open any new module on its own. Best-effort hack to bridge
    // the dangling fmv.run state into mcp.run's "load next module"
    // path.
    if (getenv("NUANCE_STACK_WALK")) {
      static uint64 idle_start_us = 0;
      static bool walked = false;
      const uint32 pc = nuonEnv.mpe[3].pcexec;
      const bool in_fmv = (pc >= 0x80030000 && pc < 0x800C3580);
      // Only consider unwinding when there's NO active MPX decoder
      // (so we know mcp.run hasn't queued the next cutscene either)
      // AND MPE3 is still in fmv.run code.
      extern void* MpxDecoderActive_Token();
      const bool any_decoder = (MpxDecoderActive_Token() != nullptr);
      if (in_fmv && !walked && !any_decoder) {
        if (idle_start_us == 0) idle_start_us = useconds_since_start();
        else if (useconds_since_start() - idle_start_us > 8000000) {
          // 8 s after autoskip teardown without mcp.run reacting —
          // definitely stuck, try the walk.
          uint32 r31 = nuonEnv.mpe[3].regs[31];
          uint32 found_pc = 0;
          uint32 found_off = 0;
          for (uint32 off = 0; off < 0x4000; off += 4) {
            uint32* p = (uint32*)nuonEnv.GetPointerToMemory(3, r31 + off);
            if (!p) break;
            uint32 v = SwapBytes(*p);
            if (v >= 0x80018000 && v < 0x800239A0) {
              found_pc = v;
              found_off = off;
              break;
            }
          }
          if (found_pc) {
            fprintf(stderr, "[STACK-WALK] found mcp.run pc=0x%08X at r31+0x%X (r31=0x%08X) — teleporting MPE3\n",
                    found_pc, found_off, r31);
            nuonEnv.mpe[3].pcexec = found_pc;
            nuonEnv.mpe[3].regs[31] = r31 + found_off + 4;
            walked = true;
          } else {
            fprintf(stderr, "[STACK-WALK] no mcp.run rz in MPE3 stack [0x%08X..0x%08X)\n",
                    r31, r31 + 0x4000);
            walked = true;
          }
        }
      } else {
        idle_start_us = 0;  // reset if state changed
      }
    }

    // When our libavcodec MPX decoder has finished, poke the IS3
    // fmv.run "cutscene done" flag at mem[0x800BC4E8] so the polling
    // loop at 0x800AB854 detects the end and the game advances.
    // Re-poke every frame because fmv.run might clear the flag itself.
    // Address found by static disasm of fmv.run after _MemLoadCoffX.
    // See nuance-stuck-loading.md for the disasm trail.
    // Latch s_first_eof_us when MPX first hits EOF — used by the
    // disasm/IRAM dumper below which fires AFTER MPX teardown for games
    // that load gameplay code post-cutscene (IS3 → ismerlin.run).
    static uint64 s_first_eof_us = 0;
    {
      extern bool MpxDecoderActive_Probe(uint32*, uint32*);
      extern bool MpxDecoderActive_IsAtEnd();
      uint32 mw = 0, mh = 0;
      if (MpxDecoderActive_Probe(&mw, &mh) && MpxDecoderActive_IsAtEnd()) {
        if (s_first_eof_us == 0) s_first_eof_us = useconds_since_start();
        if (uint32* p = (uint32*)nuonEnv.GetPointerToMemory(0, 0x800BC4E8)) {
          static int n = 0;
          uint32 was = SwapBytes(*p);
          *p = SwapBytes(1u);
          if (n < 5 || (n % 200) == 0)
            fprintf(stderr, "[MPX-EOF] poke #%d mem[0x800BC4E8]: was 0x%08X -> 1\n", ++n, was);
        }
      }
    }
    // IRAM/DRAM dump trigger — runs even after MPX is torn down, so the
    // delay covers the period where gameplay code (ismerlin.run for IS3)
    // gets DMA-loaded into main DRAM. NUANCE_DUMP_DELAY=<sec> = wait
    // this many seconds past first MPX EOF before dumping.
    // NUANCE_DUMP_FROM_START=1 instead uses program start as the
    // reference (for games with no MPX cutscene like Tetris demo disc).
    static uint64 s_dump_ref_us = 0;
    if (s_dump_ref_us == 0 && getenv("NUANCE_DUMP_FROM_START"))
      s_dump_ref_us = useconds_since_start();
    if (s_first_eof_us != 0 && s_dump_ref_us == 0) s_dump_ref_us = s_first_eof_us;
    if (s_dump_ref_us != 0) {
        static bool s_dumped = false;
        static uint64 s_dump_at_us = 0;
        static uint64 s_dump_period_us = 0;
        static int s_delay_inited = 0;
        if (!s_delay_inited) {
          s_delay_inited = 1;
          if (const char* d = getenv("NUANCE_DUMP_DELAY"))
            s_dump_at_us = (uint64)strtoull(d, nullptr, 0) * 1000000ull;
          // NUANCE_DUMP_PERIOD=<sec>: repeat dump every N seconds
          // instead of firing once. Useful for watching how a memory
          // region evolves over time (e.g. "is 0x402F0000 ever
          // populated past the uninit fill?").
          if (const char* p = getenv("NUANCE_DUMP_PERIOD"))
            s_dump_period_us = (uint64)strtoull(p, nullptr, 0) * 1000000ull;
        }
        bool dump_now = !s_dumped &&
            (useconds_since_start() - s_dump_ref_us) >= s_dump_at_us;
        if (dump_now) {
          // Dump MPE3 register state at the moment of the disasm — useful
          // for diagnosing what the stuck loop is comparing against.
          fprintf(stderr, "[MPE3-REGS] pc=0x%08X rz=0x%08X go=%d\n",
                  nuonEnv.mpe[3].pcexec,
                  nuonEnv.mpe[3].rz,
                  (nuonEnv.mpe[3].mpectl & 2) ? 1 : 0);
          for (int rb = 0; rb < 32; rb += 8) {
            fprintf(stderr, "  r%-2d-%-2d:", rb, rb+7);
            for (int j = 0; j < 8; j++)
              fprintf(stderr, " %08X", nuonEnv.mpe[3].regs[rb+j]);
            fprintf(stderr, "\n");
          }
          // Dump the MPE3 stack frame so we can find the caller chain.
          // r31 is the SP; stack grows down so frame links are at [SP], [SP+4], ...
          const uint32 sp = nuonEnv.mpe[3].regs[31];
          fprintf(stderr, "[MPE3-STACK] sp=0x%08X:", sp);
          for (uint32 off = 0; off < 64; off += 4) {
            uint32* mp = (uint32*)nuonEnv.GetPointerToMemory(3, sp + off, false);
            if (mp) fprintf(stderr, " %08X", SwapBytes(*mp));
            else    fprintf(stderr, " ?");
          }
          fprintf(stderr, "\n");
          // NUANCE_DUMP_MEM=addr1:size1,addr2:size2,... — print byte-swapped
          // 32-bit words at each region. Useful for inspecting state
          // pointers / sync slots at the disasm moment.
          if (const char* memSpec = getenv("NUANCE_DUMP_MEM")) {
            const char* p = memSpec;
            while (*p) {
              uint32 a = 0, sz = 0;
              int n = 0;
              if (sscanf(p, "%x:%x%n", &a, &sz, &n) == 2 && sz > 0 && sz <= 256) {
                fprintf(stderr, "[MEM] @0x%08X (%u bytes):", a, sz);
                for (uint32 off = 0; off < sz; off += 4) {
                  uint32* mp = (uint32*)nuonEnv.GetPointerToMemory(3, a + off);
                  if (mp) fprintf(stderr, " %08X", SwapBytes(*mp));
                  else    fprintf(stderr, " ?");
                }
                fprintf(stderr, "\n");
                p += n;
                while (*p == ',' || *p == ' ') p++;
              } else break;
            }
          }
          if (const char* dumpPattern = getenv("NUANCE_DUMP_MPE_IRAM_PATTERN")) {
            for (int m = 0; m < 4; m++) {
              char path[256];
              snprintf(path, sizeof(path), "%s.%d", dumpPattern, m);
              FILE* f = fopen(path, "wb");
              if (f) {
                fwrite(&nuonEnv.mpe[m].dtrom[MPE_IRAM_OFFSET], 1, 0x100000, f);
                fclose(f);
                fprintf(stderr, "[DUMP-IRAM] mpe%d -> %s\n", m, path);
              }
            }
          }
          // NUANCE_DISASM_MPE_IRAM=N:lo:hi:out — disassemble code from MPE N's
          // memory view [lo, hi) to <out>. Handles BOTH MPE-local IRAM
          // addresses (0x20300000..) and main DRAM (0x80000000..) by
          // resolving the byte pointer per-packet via GetPointerToMemory.
          if (const char* disasmSpec = getenv("NUANCE_DISASM_MPE_IRAM")) {
            int n = 0;
            uint32 lo = 0, hi = 0;
            char out[200] = {};
            if (sscanf(disasmSpec, "%d:%x:%x:%199s", &n, &lo, &hi, out) == 4 &&
                n >= 0 && n < 4) {
              FILE* f = fopen(out, "w");
              if (f) {
                char dasm[512];
                uint32 pc = lo;
                int count = 0;
                while (pc < hi && count < 200000) {
                  dasm[0] = 0;
                  nuonEnv.mpe[n].PrintInstructionCachePacket(dasm, sizeof(dasm), pc);
                  fprintf(f, "%08X: %s", pc, dasm);
                  if (dasm[0] && dasm[strlen(dasm)-1] != '\n') fputc('\n', f);
                  uint8* p = (uint8*)nuonEnv.GetPointerToMemory(n, pc);
                  if (!p) break;
                  uint32 delta = nuonEnv.mpe[n].GetPacketDelta(p, 1);
                  if (delta == 0 || delta > 64) delta = 2;
                  pc += delta;
                  count++;
                }
                fclose(f);
                fprintf(stderr, "[DISASM-IRAM] mpe%d %d packets [0x%08X..0x%08X) -> %s\n",
                        n, count, lo, pc, out);
              }
            }
          }
          s_dumped = true;
          // If a repeat period is configured, schedule the NEXT dump and
          // clear s_dumped so the trigger fires again. We bump
          // s_dump_at_us instead of using `now` so the cadence is exact.
          if (s_dump_period_us > 0) {
            s_dump_at_us += s_dump_period_us;
            s_dumped = false;
          }
        }
    }

    if (nuonEnv.trigger_render_video)
    {
      static uint64 old_time = 0;
      static uint64 old_acc_time = 0;
      static int acc_kcs = 0;
      static uint64 acc_cycles = 0;
      acc_cycles += cycles;

      const uint64 new_time = useconds_since_start();
      if (new_time - old_acc_time > 2000000) {
        acc_kcs = (int)(acc_cycles * 1000 / (double)(new_time - old_acc_time));
        acc_cycles = 0;
        old_acc_time = new_time;
      }
      int fps = (old_time != 0) ? (int)(1000000.0 / (double)(new_time - old_time)) : 0;
      old_time = new_time;

      NuanceUI_UpdateTitle(acc_kcs, fps);
      OnDisplayPaint(0, 0);
      nuonEnv.trigger_render_video = false;
    }
    else if (!bRun)
    {
      OnDisplayPaint(0, 0);
      usleep(16000);
    }
  }

  VideoCleanup();
  display.CleanUp();
  CleanupArchives();

  return 0;
}

#endif // !_WIN32
