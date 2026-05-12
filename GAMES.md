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
| 3 | Merlin Racing | ✅ Playable | `CompilerConstantPropagation=Disabled` (default in `nuance.cfg`) | AI carts drive correctly |
| 4 | Freefall 3050 A.D. | ✅ Playable | `CompilerConstantPropagation=Disabled` | |
| 5 | Space Invaders XL | ✅ Playable | — | |
| 6 | Jjangguneun Monmallyeo 3 | ✅ Playable | `CompilerConstantPropagation=Disabled` | Korean exclusive |
| 7 | Iron Soldier 3 | ⚠️ Partial | `NUANCE_FORCE_AUDIOCNT=1 NUANCE_IS3_STATE=0 NUANCE_BTN_QUEUE=... NUANCE_BTN_QUEUE_DELAY=7800` | Engine reaches `ismerlin.run`, loads level1 from `levels.dat`, then runs silently. Visual gameplay not yet confirmed. BTN_QUEUE timing fragile (~50% repro). |
| 8 | The Next Tetris | 🟡 Demo disc | `NUANCE_DEMO_LAUNCH=tnt` *or* `NUANCE_AUTO_DVD=1` | `nuon.run` launcher enumerates titles via fake `_FindName`, then stalls waiting for selection. Workaround bypasses the DVD-Video selector menu. |
| 9 | Interactive Sampler | 🟡 Demo disc | `NUANCE_DEMO_LAUNCH=tempest\|merlinracing` *or* `NUANCE_AUTO_DVD=1` | Same launcher as Tetris. `NUANCE_AUTO_DVD=1` plays the disc's MPEG-2 attract reel via libdvdnav. |

## Env-knob reference

| Variable | Purpose |
|----------|---------|
| `NUANCE_DEMO_LAUNCH=<name>` | Skip nuon.run launcher and load a specific embedded `.cof` (e.g. `tnt`, `tempest`, `merlinracing`) |
| `NUANCE_AUTO_DVD=1` | Play the largest `VIDEO_TS/VTS_*.VOB` via libdvdnav (DVD-Video front-end) |
| `NUANCE_IS3_STATE=<v>` | One-shot poke `*0x8002125C` — IS3 mcp.run primary selector. `=0` triggers the menu-load path |
| `NUANCE_BTN_QUEUE=tok:frames,...` | Frame-locked controller-state replay (bypasses X11/xdotool timing) |
| `NUANCE_BTN_QUEUE_DELAY=<fields>` | Delay before first BTN_QUEUE token (in display fields) |
| `NUANCE_FORCE_AUDIOCNT=1` | Pin fmv.run audio counters during cutscene → engine handoff |
| `NUANCE_FORCE_AUDIOCNT_OFF_AFTER=<sec>` | Release the audiocnt pin once the game engine has loaded |
| `NUANCE_TRACE_LEVELS=1` | Log MediaOpen/Read/Close for game-data `*.dat` files (skips `programs.dat`, `.mpx`) |
| `NUANCE_LOG_MEDIA=1` | Log all MediaOpen calls regardless of file type |
| `NUANCE_LOG_BIOS=1` | Log every BIOS dispatcher call with slot name + register state |
| `NUANCE_LOG_BIOS_SKIP=<slot>[,slot...]` | Suppress chatty BIOS slots (e.g. 32=BiosPoll, 96=spinwait, 145=kprintf) |
| `NUANCE_LOG_BDMA=1` | Histogram of unimplemented bilinear-DMA dispatch indices |
| `NUANCE_LOG_VIDCFG=1` | VidConfig call trace |
| `NUANCE_LOG_RENDER=1` | Render-state transitions + framebuffer pixel hex |
| `NUANCE_FORCE_LOAD_COFF` | Force-load a specific `.cof` from `programs.dat` (diagnostic harness) |
| `NUANCE_FORCE_LOAD_COFF_AFTER=<count>` | Configurable MPX-teardown threshold (default 2; `=1` for single-cutscene games) |
| `NUANCE_SLOT30_RET=<v>` | Override BiosExit (slot 30) return value |

## Known blockers

### Iron Soldier 3
- Engine (`ismerlin.run`, 0xE627C in `programs.dat`) reaches LOADING screen and successfully streams 4 chunks from `levels.dat` (directory header + sounds_dat + level1_dat + level1_tex)
- TimerInit slot-16 assert fixed in commit `64928a3`
- Unimplemented bilinear-DMA handler (type 6, Write H, A=0, B=0) implemented in commit `e9d4708` (`src/bdma_type6.cpp`)
- Post-load: engine runs silently with no crashes / no errors, but no visible gameplay yet
- BTN_QUEUE timing is fragile (~50% repro on first try); retry until you see `[LEVELS] open ... levels.dat`

### Demo discs (Tetris / Sampler)
- `nuon.run` launcher calls `_FindName` 4× to enumerate selectable titles, then stalls
- Currently the fake enumeration returns `tnt`, `tempest`, `merlinracing` — three universal names
- Launcher likely expects a different response (probably to follow up with `_LoadCoff` once a real title list is returned)
- Workarounds: `NUANCE_DEMO_LAUNCH` (bypass launcher entirely) or `NUANCE_AUTO_DVD` (play DVD-Video attract)
