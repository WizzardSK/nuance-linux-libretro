# NUON Game Compatibility (Linux/libretro fork)

Status of the nine commercial NUON discs as of 2026-05-12 on the `linux-libretro`
branch. Tested against the 64-bit asmjit JIT build (`build64/nuance`).

## Summary

- **Fully playable**: 6
- **Demo discs (require workaround)**: 2
- **Partially working**: 1
- **Effective availability**: 8 / 9

## Matrix

| # | Game | Status | Env knobs | Notes |
|---|------|--------|-----------|-------|
| 1 | Ballistic | ✅ Playable | — | Plays cleanly on both 32-bit and 64-bit JIT |
| 2 | Tempest 3000 | ✅ Playable | — | Controller-setup + gameplay verified |
| 3 | Merlin Racing | ✅ Playable | `CompilerConstantPropagation=Disabled` (default in `nuance.cfg`) | AI carts drive correctly. ~10 VidChangeBase / sec (60Hz page-flip) confirmed during gameplay. |
| 4 | Freefall 3050 A.D. | ✅ Playable | `CompilerConstantPropagation=Disabled` | |
| 5 | Space Invaders XL | ✅ Playable | — | |
| 6 | Jjangguneun Monmallyeo 3 | ✅ Playable | `CompilerConstantPropagation=Disabled` | Korean exclusive |
| 7 | Iron Soldier 3 | ⚠️ Partial | `NUANCE_FORCE_AUDIOCNT=1 NUANCE_FORCE_AUDIOCNT_OFF_AFTER=3 NUANCE_IS3_STATE=0 NUANCE_BTN_QUEUE_AFTER_MPX=1 NUANCE_BTN_QUEUE_DELAY=300 NUANCE_BTN_QUEUE="_:30,A:30,_:120,A:30,_:120,A:30,_:120,A:30,_:120,A:30,_:120,A:30,_:120,A:30,_:120,A:30,_:6000"` | Reaches title → menu → Demo Mode → LOADING screen + loads level1 from `levels.dat` (4 chunks: dir + sounds + level1_dat + level1_tex). Then ismerlin.run engine freezes in a `0x8008C9xx` memcpy + DCacheFlush + verify hot loop (96% time post-LEVELS). 8-minute run confirms framebuffer byte-identical for 4+ min after LEVELS close. Visual gameplay never reached. |
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
| `NUANCE_MPE_RATIO=<n0>:<n1>:<n2>:<n3>` | Cycles per main-loop iteration for each MPE (default 1:1:1:1; try 2:5:5:2 to bias workers) |
| **Experimental harnesses** ||
| `NUANCE_FORCE_LOAD_COFF` | Force-load a specific `.cof` from `programs.dat` |
| `NUANCE_FORCE_LOAD_COFF_AFTER=<count>` | Configurable MPX-teardown threshold (default 2; `=1` for single-cutscene games) |
| `NUANCE_SLOT30_RET=<v>` | Override BiosExit (slot 30) return value |
| `NUANCE_AUTOSKIP_SEC=<sec>` | Seconds after MPX EOF before auto-tearing-down the decoder (default 5) |

## Known blockers

### Iron Soldier 3

Today's deep investigation (2026-05-12) confirmed the engine reaches gameplay
code but freezes on a verify loop. Three popular hypotheses were ruled out:

| Hypothesis | Verdict | Why |
|------------|---------|-----|
| Empty `AssemblyBiosHandler` blocks inter-MPE comm (`_CommSendInfo` slot 1) | ❌ Wrong | bios.cof at 0x80000008 contains `bra $80760080, nop` — the real asm handler runs (verified via `NUANCE_DISASM_MPE_IRAM=3:0x80000000:0x80000040`). `NUANCE_LOG_BIOS=1` shows 0 dispatcher hits for slots 0–5. |
| Missing `VidChangeBase` (BIOS slot 26) prevents page-flipping | ❌ Wrong | IS3 doesn't use the BIOS page-flip API at all (`NUANCE_LOG_VIDCHANGE=1` shows 0 calls). It uses VidConfig with new base each frame, and the base DOES alternate `0x40000000 ↔ 0x4012C000` — the flip works. |
| MPE3 starves workers via tight MPEStop/MPERun cycle | ❌ Wrong | Tested with `NUANCE_MPE_RATIO=2:5:5:2` (workers get 5 cycles vs MPE3's 2): IS3 reaches LEVELS but freezes at the same LOADING screen at t=160s. Cycle bias is not the root cause. |

**Smoking gun (2026-05-12)**: BIOS-slot usage comparison before vs after
`[LEVELS] close`:

| Phase | Unique BIOS slots | Render-path slots |
|-------|-------------------|-------------------|
| Pre-LEVELS (menu / LOADING) | 29 | VidConfig=525× ✅ DMABiLinear ✅ |
| Post-LEVELS (gameplay attempt) | 11 | **VidConfig=0 ❌ DMABiLinear=0 ❌** |

The engine never calls VidConfig or DMABiLinear after `levels.dat` closes,
which means the render-frame pipeline is never reached. Framebuffer is
literally last set to the LOADING-screen base and stays there because no
new page-flip is issued. Post-LEVELS slots are all compute/management:
MPEStop/MPERun, DCacheSync/Flush, MPELoad, TimerInit, DMALinear — no
rendering at all.

**MPE3 hot loop**: spends 96% of post-LEVELS time in a memcpy+DCacheFlush
+verify loop at `0x8008C9BC..0x8008C9F2`:
1. Memcpy r28 bytes from `r16` (source) to `0x400A0000` (dest)
2. JSR via r30 (= 0x80000060 = DCacheFlush BIOS slot 12 — our impl is a no-op)
3. Verify: compare source vs dest word-by-word; set r6=0 on mismatch

The loop retries indefinitely, suggesting the verify always fails. Worker MPEs
(MPE0/1/2) are mostly halted (`MPESTATE` dump: MPE1 at $20300266 in 87% of samples).
They may be supposed to fill the source buffer between MPE3's iterations but
either (a) don't get to write the expected data, or (b) write data MPE3 doesn't
recognise as valid.

Today's progress (commits `c57212d`, `732e5a3`, `1f596f1`, `abd93e8`, `c0d10c0`,
`2e0f48b`, `550f701`):
- Deterministic LEVELS-reach recipe (~80% success rate)
- Visual confirmation: title → menu → Demo Mode → LOADING screen all render correctly
- 9 new diagnostic env knobs for future investigation
- 3 wrong hypotheses ruled out, root cause narrowed to internal compute pipeline

Real fix requires reverse-engineering exactly what's expected at the source
buffer `r16` and which MPE/component is supposed to populate it.

### Demo discs (Tetris / Sampler)
- `nuon.run` launcher calls `_FindName` at path "/" with filter prefix "app"
- Tested name variants ("tnt", "apptnt", "app-tnt") + r4 metadata — none unblock
- After enumeration, launcher proceeds via asm-handler BIOS slots (invisible to LOG_BIOS), framebuffer stays pure black
- Confirmed via spectacle screenshot — no menu rendering at all
- Real blocker likely in nuon.run's video-setup path (no VidConfig ever called → black screen)
- Workaround `NUANCE_DEMO_LAUNCH=<name>` remains the supported path
