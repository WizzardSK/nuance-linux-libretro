# NUON Game Compatibility (Linux/libretro fork)

Status of the nine commercial NUON discs as of 2026-05-13 on the `linux-libretro`
branch. Tested against the 64-bit asmjit JIT build (`build/nuance`).

## Summary

- **Fully playable**: 6
- **Playable to in-game menu**: 1
- **Demo discs (require workaround)**: 2
- **Effective availability**: 9 / 9 (every disc renders content; gameplay reached for 6)

## Matrix

| # | Game | Status | Env knobs | Notes |
|---|------|--------|-----------|-------|
| 1 | Ballistic | ✅ Playable | — | Plays cleanly on both 32-bit and 64-bit JIT |
| 2 | Tempest 3000 | ✅ Playable | — | Controller-setup + gameplay verified |
| 3 | Merlin Racing | ✅ Playable | `CompilerConstantPropagation=Disabled` (default in `nuance.cfg`) | AI carts drive correctly. ~10 VidChangeBase / sec (60Hz page-flip) confirmed during gameplay. |
| 4 | Freefall 3050 A.D. | ✅ Playable | `CompilerConstantPropagation=Disabled` | |
| 5 | Space Invaders XL | ✅ Playable | — | |
| 6 | Jjangguneun Monmallyeo 3 | ✅ Playable | `CompilerConstantPropagation=Disabled` | Korean exclusive |
| 7 | Iron Soldier 3 | 🟠 Past LOADING, in attract | `NUANCE_MPE_RATIO=1:20:20:1 NUANCE_FORCE_AUDIOCNT=1` | **2026-05-14**: implementing the four D-cache BIOS slots (9 / 10 / 11 / 12) as regional JIT-translation invalidates unblocked the in-game LOADING stall. Screenshots: t=30s in-game "DANGER / DO NOT ENTER" sign scene, t=60s "J CORP / WEX TROOP" story cutscene (rendered 3D character). Previous stall point (mech silhouette + LOADING text) no longer reached. **2026-05-13** preconditions still apply: commit `36ce8d5` (CommSend/CommSendInfo BIOS slots) + `d2150ce` (MPEWait). Drop the old `NUANCE_IS3_STATE=0` / `NUANCE_BTN_QUEUE=...` knobs — they force a broken path. |
| 8 | The Next Tetris | 🟡 Demo disc | `NUANCE_DEMO_LAUNCH=tnt` *or* `NUANCE_AUTO_DVD=1` | `nuon.run` launcher enumerates titles via `_FindName` for path "/" with prefix "app", counts items, then silently proceeds via asm-handler BIOS slots (invisible to LOG_BIOS). Framebuffer stays pure black, never calls MediaOpen / VidConfig. |
| 9 | Interactive Sampler | 🟡 Demo disc | `NUANCE_DEMO_LAUNCH=tempest\|merlinracing` *or* `NUANCE_AUTO_DVD=1` | Same launcher as Tetris. `NUANCE_AUTO_DVD=1` plays the disc's MPEG-2 attract reel via libdvdnav. |

## Env-knob reference

| Variable | Purpose |
|----------|---------|
| **Game-launch** ||
| `NUANCE_DEMO_LAUNCH=<name>` | Skip nuon.run launcher and load a specific embedded `.cof` (e.g. `tnt`, `tempest`, `merlinracing`) |
| `NUANCE_AUTO_DVD=1` | Play the largest `VIDEO_TS/VTS_*.VOB` via libdvdnav (DVD-Video front-end) |
| **IS3-specific** ||
| `NUANCE_IS3_STATE=<v>` | One-shot poke `*0x8002125C` — IS3 mcp.run primary selector. `=0` triggers the menu-load path |
| `NUANCE_FORCE_AUDIOCNT=1` | Pin fmv.run audio counters during cutscene → engine handoff |
| `NUANCE_FORCE_AUDIOCNT_OFF_AFTER=<sec>` | Release the audiocnt pin once the game engine has loaded (essential for IS3 to break out of post-MPX VidConfig init loop) |
| **Input automation** ||
| `NUANCE_BTN_QUEUE=tok:frames,...` | Frame-locked controller-state replay (bypasses X11/xdotool timing); tokens: A B Up Down Left Right START NUON L R _ |
| `NUANCE_BTN_QUEUE_DELAY=<fields>` | Delay before first BTN_QUEUE token (in display fields, 60Hz NTSC) |
| `NUANCE_BTN_QUEUE_AFTER_MPX=1` | Gate BTN_QUEUE countdown on MPX cutscene EOF (deterministic regardless of CPU speed) |
| **Diagnostics** ||
| `NUANCE_TRACE_LEVELS=1` | Log MediaOpen/Read/Close for game-data `*.dat` files (skips `programs.dat`, `.mpx`) |
| `NUANCE_LOG_MEDIA=1` | Log all MediaOpen calls regardless of file type |
| `NUANCE_LOG_BIOS=1` | Log every BIOS dispatcher call with slot name + register state |
| `NUANCE_LOG_BIOS_SKIP=<slot>[,slot...]` | Suppress chatty BIOS slots (e.g. 32=BiosPoll, 96=spinwait, 145=kprintf, 22=VidSync, 24=VidConfig) |
| `NUANCE_LOG_BDMA=1` | Histogram of unimplemented bilinear-DMA dispatch indices |
| `NUANCE_LOG_VIDCFG=1` | VidConfig call trace |
| `NUANCE_LOG_RENDER=1` | Render-state transitions + framebuffer pixel hex |
| `NUANCE_LOG_VIDCHANGE=1` | Lightweight VidChangeBase BIOS-call tracer (first 8 + every 60th) |
| `NUANCE_DUMP_MPE_STATE=<N>` | Periodic MPE state dump every N cycles (PC + go-bit for all 4 MPEs) |
| `NUANCE_GAME_PC_PERIOD=<sec>` | Re-dump MPE3 PC histogram every N seconds (clears between windows) |
| `NUANCE_DUMP_DELAY=<sec>` | Delay IRAM/DRAM disasm trigger until N seconds past first MPX EOF |
| `NUANCE_DUMP_FROM_START=1` | Use program start as reference for DUMP_DELAY (for games with no MPX cutscene) |
| `NUANCE_DUMP_MEM=addr:size,...` | Print byte-swapped 32-bit words at each region at disasm-trigger moment |
| `NUANCE_DUMP_MPE_IRAM_PATTERN=<path>` | Dump MPE IRAM to `<path>.0..3` at disasm trigger |
| `NUANCE_DISASM_MPE_IRAM=N:lo:hi:out` | Disassemble MPE N's code from address [lo, hi) to file (handles both IRAM and main DRAM) |
| **Scheduling** ||
| `NUANCE_MPE_RATIO=<n0>:<n1>:<n2>:<n3>` | Cycles per main-loop iteration for each MPE (default 1:1:1:1; for IS3 use `1:20:20:1` to bias workers) |
| `NUANCE_AUTO_YIELD=<threshold>[:<boost>]` | Adaptive: when MPE3 stays at the same PC for `threshold` consecutive iterations, multiply worker MPEs' cycle budget by `boost` (default 50) until MPE3 moves on. Useful for games with bursty comm-response spinwaits. `200:100` is a moderate setting; lower thresholds and higher boosts can starve MPE3 of rendering cycles. |
| **Experimental harnesses** ||
| `NUANCE_FORCE_LOAD_COFF` | Force-load a specific `.cof` from `programs.dat` |
| `NUANCE_FORCE_LOAD_COFF_AFTER=<count>` | Configurable MPX-teardown threshold (default 2; `=1` for single-cutscene games) |
| `NUANCE_SLOT30_RET=<v>` | Override BiosExit (slot 30) return value |
| `NUANCE_AUTOSKIP_SEC=<sec>` | Seconds after MPX EOF before auto-tearing-down the decoder (default 5) |

## Known blockers

### Iron Soldier 3

**2026-05-13 update**: the "Empty AssemblyBiosHandler blocks comm" hypothesis,
previously marked wrong, turned out to be the actual root cause of the boot
LOADING freeze. The asm at 0x80760080 referenced from bios.cof's jump table is
unreachable in this emulator — the BIOS dispatcher in `MPE::FetchDecodeExecute`
calls the C++ slot handler and then does an implicit RTS via `pcexec = rz`, so
nothing in `bios.s` (which only exists as reference) ever runs.

Implementing slots 0/1 in C++ (`src/bios.cpp`, commit `36ce8d5`) drives MPE3's
unique-PC count from 1 (the spinwait at `0x80237FD8`) to ~1200 in 40 seconds.
With `NUANCE_MPE_RATIO=1:20:20:1 NUANCE_FORCE_AUDIOCNT=1`, IS3 now plays the
intro cutscene, briefly hits a boot LOADING screen, and reaches the in-game
Demo Mode menu with all four MPEs running.

**Remaining blocker**: Demo Mode auto-attract loads a demo level and stalls
at the in-game LOADING screen (mech silhouette + "LOADING..." text).
ismerlin.run is actively reading from `levels.dat` (4.4 MB, multiple MediaOpen
+ MediaRead + MediaClose cycles, MemLoadCoffX into 0x80400000 and 0x80600000,
multiple MPELoad-and-dispatch cycles). The engine is genuinely doing the
level-load work — registers (r0, r1, r9) change between samples, workers
have 200k+ active samples per minute — it's not a tight deadlock but a
slow-or-incomplete process. Possible deeper causes per memory analysis:

1. **JIT divergence in a specific worker instruction** that the 6 working games
   don't hit. asmjit recently had four 64-bit pointer-truncation bugs fixed
   (`nuance-asmjit-debug-2026-05-06.md`); another instruction may have a
   similar latent bug only IS3 triggers.
2. **DCacheFlush no-op** — `_DCacheFlush` (slot 12) is currently a no-op via
   `UnimplementedCacheHandler`. IS3 calls it ~20× during level load. If the
   workers write to DRAM and IS3 expects an explicit cache flush before
   reading, our no-op is silently incorrect when readers see stale cached
   data. Hard to verify without cache-line-tracking instrumentation.
3. **Specific MPE0/1/2 task ID** at `0x20300266` that we don't model. Workers
   only run a few hundred cycles per MPE3-MPERun pair; the dispatcher reads
   a task ID from `0x201003DC` and jumps to a handler. If the handler we'd
   need lives in code we don't faithfully execute (JIT bug or pixel handler),
   tasks would "complete" without producing a result.

The `NUANCE_AUTO_YIELD=<threshold>[:<boost>]` knob added in commit `5cba6c8`
detects per-PC MPE3 spinwaits and boosts worker cycles. Moderate settings
(`200:100`) confirm the mechanism works (2877 activations across a 7-min
run, MPE3 moves 2 bytes between yields as expected) but do NOT unblock the
level-LOADING macro state — the remaining hot spot is the legitimate slow
memcpy at `0x802397CE..0x802397E6` plus fmv.run audio-counter polling
(which workers cannot write — needs `NUANCE_FORCE_AUDIOCNT=1`).

Today's commits (`36ce8d5`, `d2150ce`, `5cba6c8`):
- Inter-MPE comm BIOS slots 0/1 implemented in C++
- `_MPEWait` slot 106 implemented with cycle-yield (no game in the 9-disc
  set actually calls it, but the AssemblyBiosHandler stub is gone)
- `NUANCE_AUTO_YIELD` adaptive worker-cycle boost knob

Real fix for in-game gameplay still requires either: trace `0x20300266` task
dispatcher disasm + simulate one task end-to-end on real-hardware reference
to find the divergence, or implement DCache-coherency tracking so the workers'
writes become visible to MPE3 deterministically.

### Demo discs (Tetris / Sampler)
- `nuon.run` launcher calls `_FindName` at path "/" with filter prefix "app"
- Tested name variants ("tnt", "apptnt", "app-tnt") + r4 metadata — none unblock
- After enumeration, launcher proceeds via asm-handler BIOS slots (invisible to LOG_BIOS), framebuffer stays pure black
- Confirmed via spectacle screenshot — no menu rendering at all
- Real blocker likely in nuon.run's video-setup path (no VidConfig ever called → black screen)
- Workaround `NUANCE_DEMO_LAUNCH=<name>` remains the supported path
