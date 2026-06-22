#!/bin/sh
# JIT<->interpreter differential harness for the AArch64 asmjit JIT.
#
# Runs the same NUON workload twice through the libretro core in RetroArch:
#   1. native  - the a64 JIT (default on aarch64 builds with USE_ASMJIT)
#   2. il      - the IL interpreter (NUANCE_FORCE_IL=1)
# Each run records "<pc> <hash>" per executed cached block for one MPE
# (NUANCE_TRACE / NUANCE_TRACE_MPE, FNV-1a over reg_union). The two traces are
# then diffed: the first differing line is the first block whose result the JIT
# got wrong relative to the interpreter (or a control-flow divergence).
#
# Requires the core built WITH the harness hooks and installed in RetroArch.
# Usage: tools/jitdiff.sh [game_path] [num_blocks]
set -u

CORES=/home/user/.var/app/org.libretro.RetroArch/config/retroarch/cores
CORE="$CORES/nuance_libretro.so"
GAME="${1:-/home/user/share/roms/nuon/Redump/Ballistic_NUON/nuon.run}"
WANT="${2:-150000}"          # block count to capture before stopping
MAX=$((WANT + 50000))
MPE="${NUANCE_TRACE_MPE:-0}"
OUT=/home/user/jitdiff
mkdir -p "$OUT"

run() {
  label="$1"; force_il="$2"
  trace="$OUT/trace_$label.txt"
  rm -f "$trace"
  set -- flatpak run --filesystem=host \
      --env=NUANCE_TRACE="$trace" --env=NUANCE_TRACE_MPE="$MPE" --env=NUANCE_TRACE_MAX="$MAX"
  [ -n "$force_il" ] && set -- "$@" --env=NUANCE_FORCE_IL=1
  set -- "$@" org.libretro.RetroArch -L "$CORE" "$GAME"
  echo ">> [$label] launching (force_il='${force_il:-no}')"
  DISPLAY=:0 "$@" >"/tmp/ra_jitdiff_$label.log" 2>&1 &
  i=0
  while [ "$i" -lt 60 ]; do
    [ -f "$trace" ] && [ "$(wc -l <"$trace" 2>/dev/null)" -ge "$WANT" ] && break
    sleep 1; i=$((i + 1))
  done
  pkill -f "org.libretro.RetroArch" 2>/dev/null
  sleep 2; pkill -9 -f "org.libretro.RetroArch" 2>/dev/null
  echo ">> [$label] captured $(wc -l <"$trace" 2>/dev/null || echo 0) blocks"
}

run native ""
run il 1

N="$OUT/trace_native.txt"; I="$OUT/trace_il.txt"
echo
echo "=== diff (MPE $MPE) ==="
common=$(wc -l <"$N"); ic=$(wc -l <"$I")
# first differing line number
firstdiff=$(diff --unchanged-line-format='' --old-line-format='%dn ' --new-line-format='' "$N" "$I" 2>/dev/null | awk '{print $1; exit}')
matched=$(comm -12 <(cat -n "$N") <(cat -n "$I") 2>/dev/null | wc -l)
if [ -z "$firstdiff" ]; then
  echo "NO DIVERGENCE: native and IL traces identical over $(wc -l <"$N") blocks. JIT matches interpreter."
else
  echo "FIRST DIVERGENCE at block #$firstdiff:"
  echo "  native: $(sed -n "${firstdiff}p" "$N")"
  echo "  il    : $(sed -n "${firstdiff}p" "$I")"
  echo "  (matched $((firstdiff - 1)) blocks before diverging)"
  echo "  context (native around divergence):"; sed -n "$((firstdiff-2)),$((firstdiff+2))p" "$N" | sed 's/^/    /'
fi
