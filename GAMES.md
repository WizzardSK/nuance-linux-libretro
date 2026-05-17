# NUON Game Compatibility (Linux/libretro fork)

Status of the nine commercial NUON discs as of 2026-05-14 on the `linux-libretro`
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
| 7 | Iron Soldier 3 | 🟠 Reaches in-game LOADING screen | `NUANCE_MPX_SKIP_ALL=1 NUANCE_FORCE_AUDIOCNT=1` + `NUANCE_BTN_QUEUE` menu-nav | **2026-05-17 — MAJOR FIX**: GLX vsync was throttling the main loop to ~1 Hz on Intel iGPUs, hanging fmv.run's VidSync wait at 0x800311F4. Commits `c9c09c5` (disable vsync via glXSwapIntervalEXT) + `23a8536` (glFinish before swap) unblock IS3 on hardware GL. With `NUANCE_BTN_QUEUE` driving menu nav (Demo Mode → Level select → A), IS3 reaches in-game LOADING screen (mech silhouette). State machine evolves naturally `0 → 0x64 → 0x66 → 0x69`. **All five modules load**: mcp.run, fmv.run, menu.run, levelsel.run, ismerlin.run (0xE627C = 918KB game engine). 23 MediaOpens incl. levels.dat + audio.dat reads. Next stall: in-game LOADING (asset binding) — same point old "tex11 stall" documented but now reproducible without hacks. Visual confirmation via spectacle screenshots. Workaround `LIBGL_ALWAYS_SOFTWARE=1` no longer needed. |
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
| `NUANCE_AUTOSKIP=<sec>` | Seconds after MPX EOF before auto-tearing-down the decoder (default 5; `=0` disables; `=1` minimises cutscene wait for fast test iteration) |
| `NUANCE_MPX_SKIP_ALL=1` | Skip ALL MPX cutscenes immediately on open — no decoder spawn, no playback time. Saves ~4 min boot time for IS3 (logo1+logo2+titel+intro+menu). VLD-BDU stub reset preserved so multi-cutscene chains still advance |
| **Diagnostic tracing (2026-05-14)** ||
| `NUANCE_LOG_DCACHE=1` | Log every call to D-cache BIOS slots 9/10/11/12 with (mpe, r0, r1, rz). Used to verify the regional-flush calling convention is `(addr, size)` |
| `NUANCE_LOG_COMMBUS=1` | Trace every `DoCommBusController` event (deliver vs BUSY-recv-full vs BUSY-disabled) with all four MPEs' commctl values. Throttled: first 50 inline, every 1000th thereafter |
| `NUANCE_DUMP_PERIOD=<sec>` | Repeat the `NUANCE_DUMP_DELAY` snapshot every N seconds instead of firing once. Useful for watching a memory region evolve over time |
| `NUANCE_LOG_MPESTATE=<sec>` | Every N seconds log `go` / `pcexec` / `intsrc` / `commctl` for all 4 MPEs. Diagnoses MPE0 minibios activity and stuck-comm states. Added 2026-05-15 |

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

**2026-05-14 deep RE pass**: spent ~5h tracing exactly what the LOADING
loop does. Findings narrow the stall to a single failure mode:

- The "LOADING" loop is an **asset-load pipeline**. Input table at
  `0x801046B4` is an ASCII manifest of filenames (`tex3b_bmp`,
  `tex3c_bmp`, ..., `expl1_anim`, `crosshairs`, `wshape%02d_bmp`,
  `radar%02d_bmp`, ...). For each entry, the outer caller at
  `0x8008CE40..0x8008D200` (unrolled) calls a lookup function then
  a per-asset wrapper.
- The lookup at `0x8006EC80` walks a 32-byte-per-entry linked list
  whose head is chosen by mode bits (`r1 & 3`): mode 0 →
  `0x800A9F14` → list at `0x8023F700`, mode 1 → `0x800A9F18` →
  `0x402F0000`, mode 3 → `0x800A9F1C` → `0x80140000`.
- The IS3 outer caller passes `r1 = 1` (MODE 1) for every texture,
  so all lookups go through the list at `0x402F0000`. This list is
  populated from levels.dat around t=240 s with a "DATA-START"
  header followed by 32-byte entries (`tex1_bmp`, `tex1b_bmp`,
  `tex1c_bmp`, ..., `tex11_bmp`, `tex12_bmp`, `ground1_bmp`, ...).
- The per-asset wrapper at `0x8008C460` allocates a target buffer
  (sizes 16/128 bytes based on caller's mode), copies a 60-byte
  asset header in, flushes D-cache, then enters the 100KB
  memcpy + verify loop at `0x8008C9B0..0x8008CA20`. The verify
  exits with `r6=1` on byte-identical buffers (memcpy works in
  this emulator — we confirmed source and dest match for the
  loaded entries).
- A 20-minute observation with `NUANCE_DUMP_PERIOD=60` watching
  the result table at `0x800A596C` shows IS3 successfully bind
  **exactly 15 textures** (input entries 0..14, tex3b_bmp through
  tex10_bmp) by t=420 s, then **zero progress for the next 12
  minutes**. The next requested asset is `tex11_bmp`.
- `tex11_bmp` is in the mode-1 list at `0x402F02E0` with
  `+0x14 = 0x9500` (non-zero, so list-walk doesn't exit there),
  and the outer caller's hardcoded address for entry 15 is
  `0x80104768` (correct — that's `"tex11_bmp"` in the input table).
  So neither the list nor the call site explains the failure.
- Comm bus is healthy throughout (`NUANCE_LOG_COMMBUS=1` shows
  62 000+ deliveries and zero BUSY events per 6 min) — this
  conclusively rules out the comm-bus / `COMM_RECV_BUFFER_FULL_BIT`
  hypothesis.

Remaining suspects for the stall at the 16th asset:

1. **JIT divergence specific to tex11's data values**. The verify
   loop at 0x8008C9D2..F2 does `ld_b` + `asr #24` + `cmp` byte
   compares; a JIT mistranslation could miscompare for the bytes
   in tex11's data but not tex10's. Earlier asmjit fixes (commit
   `nuance-asmjit-debug-2026-05-06.md`) addressed 4 pointer-trunc
   classes; a 5th instruction-specific class is plausible.
2. **Worker MPE0/1/2 corrupting `0x400A0000`** — the verify buffer
   sits in MAIN BUS DRAM. If any worker writes to that address
   range between MPE3's memcpy and re-read, the verify fails. Would
   need MAIN BUS write-tracking per-MPE to confirm.
3. **`MemAlloc` at `0x80091D3E` returning a different buffer on the
   16th call** that overlaps another MPE's working memory. Off-by-one
   in our alloc bookkeeping could land an allocation on a poisoned
   slot starting at the 16th texture.

Full RE notes and diagnostic env-knob recipes for the next session
are in memory file `nuance-is3-loading-stall-2026-05-14.md` (path:
`~/.claude/projects/-home-wizzard-share-GitHub/memory/`). Today's
commits (`8170259`, `d31ff09`, `83f46f2`, `ca868a9`, `031f5b6`) add
diagnostic tooling (DCache implementation, MPE3 rz/stack dump,
COMMBUS tracer, AUTOSKIP integer parsing, periodic DUMP) so the
next investigation step (likely worker-write tracing on
`0x400A0000`) can run with one-line env-var config.

### Demo discs (Tetris / Sampler)
- `nuon.run` launcher calls `_FindName` at path "/" with filter prefix "app"
- Tested name variants ("tnt", "apptnt", "app-tnt") + r4 metadata — none unblock
- After enumeration, launcher proceeds via asm-handler BIOS slots (invisible to LOG_BIOS), framebuffer stays pure black
- Confirmed via spectacle screenshot — no menu rendering at all
- Real blocker likely in nuon.run's video-setup path (no VidConfig ever called → black screen)
- Workaround `NUANCE_DEMO_LAUNCH=<name>` remains the supported path
