#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
# The FM-1's constraints, checked with the computer's clang before a push (the JieLi toolchain only runs on
# GitHub): the C++ units built as the firmware builds them (no C++ library, no libc beyond felucca-bsp's memset /
# memcpy / memcmp, no libm, firmware/shim on the include path) for a 32-bit core with a float-only FPU (RISC-V rv32imf,
# a stand-in for the AC79's pi32v2), then linked with nothing else: any double left in the code links soft-double
# routines (__adddf3, __extendsfdf2...) and any missing function shows as an undefined symbol.
# The tables moved to the resource region (tools/split_tables.py) must link to the region's addresses, not be in the
# image: the last lines show where three of them landed, and what is left of the modules' code and constants.
#   tools/device_check.sh  (base/ is fm1-base: its include/ for plat.h and fastmath.h)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BASE="${BASE:-$ROOT/base}"
OUT="$ROOT/build/device_check"
CLANG="${CLANG:-clang}"
RES="$($CLANG -print-resource-dir)/include"
rm -rf "$OUT"
mkdir -p "$OUT"
TARGET="--target=riscv32-unknown-elf -march=rv32imfc -mabi=ilp32f"
FLAGS="$TARGET -O2 -ffunction-sections -fdata-sections -fno-builtin -ffreestanding -ffp-contract=off \
  -nostdinc -nostdinc++ -isystem $ROOT/firmware/shim -isystem $BASE/include -isystem $RES \
  -I$ROOT/firmware -I$ROOT/firmware/third_party -I$ROOT/firmware/third_party/lxr -DTEST -DENG_ARENA_BYTES=1024 \
  -std=gnu++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit"
fail=0
for f in "$ROOT"/firmware/mi/*.cc $(find "$ROOT/firmware/third_party" -name '*.cc' | sort); do
    o="$OUT/$(echo "${f#$ROOT/}" | tr '/' '_' | sed 's/\.cc$/.o/')"
    if ! $CLANG $FLAGS -c "$f" -o "$o" 2> "$o.log"; then
        echo "FAILED: ${f#$ROOT/}"; head -30 "$o.log"; fail=1
    elif [ -s "$o.log" ]; then
        grep -E "warning: .*(double|implicit conversion)" "$o.log" | head -5 || true
    fi
done
[ $fail = 0 ] || exit 1
cat > "$OUT/stubs.c" <<'STUB'
/* what the firmware gives the units: felucca-bsp's libc.c, and the platform calls they make */
#include <stdint.h>
void *memset(void *d, int c, unsigned n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }
int memcmp(const void *a, const void *b, unsigned n) { const unsigned char *p = a, *q = b; for (; n; n--, p++, q++) if (*p != *q) return *p - *q; return 0; }
uint32_t plat_ticks24(void) { return 0; }
int plat_cpu2_on(void) { return 0; }
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t a) { (void)fn; (void)a; return 0; }
int plat_cpu2_wait(void) { return 0; }
int plat_cpu2_busy(void) { return 0; }
void plat_cpu2_sync(void) {}
int eng_init(void); void eng_play(int); void eng_render(float *, float *, uint32_t);
const void *mi_res_where(void);
float out_l[128], out_r[128];
void _start(void)
{
    eng_init();
    eng_play(1);
    eng_render(out_l, out_r, 128);
    out_l[0] = (float)(uint32_t)mi_res_where();
    for (;;) ;
}
STUB
$CLANG $TARGET -O2 -ffreestanding -fno-builtin -nostdinc -isystem "$RES" -c "$OUT/stubs.c" -o "$OUT/stubs.o"
if ! ld.lld --gc-sections -e _start -o "$OUT/check.elf" "$OUT"/*.o 2> "$OUT/link.log"; then
    echo "LINK: what the FM-1 build would lack:"
    grep -o "undefined symbol: [^ ]*" "$OUT/link.log" | sort | uniq -c | sort -rn | head -40
    exit 1
fi
llvm-size -A "$OUT/check.elf" | grep -E "^\.(text|rodata|data|sdata|bss|sbss)" || true
for sym in _ZN7marbles13dist_icdf_0_0E _ZN6plaits20wav_integrated_wavesE _ZN5tides13lut_wavetableE lxr_wave_tables \
           lxr_transient_data; do
    line="$(llvm-nm "$OUT/check.elf" | grep " $sym\$" || true)"
    case "$line" in
        *" A $sym") echo "in the region: $line" ;;
        *) echo "NOT in the region: $sym (${line:-absent})"; exit 1 ;;
    esac
done
if llvm-nm -S "$OUT/check.elf" | grep -E " [rR] (_ZN(7marbles13dist_icdf|6plaits20wav_integrated|5tides13lut_wavetable)|sine_table|lxr_wave_tables|lxr_transient_data)"; then
    echo "a moved table is in the image too"; exit 1
fi
python3 "$ROOT/tools/no_static_init.py" "$OUT"/*.o
# fm1-base's rules for an app's C sources, as its firmware build applies them (tools/build.py mmio_check: no register
# or XIP window address, no inline asm, no volatile cast outside the BSP)
python3 - "$ROOT" "$BASE" <<'PY'
import sys
from pathlib import Path
root, base = Path(sys.argv[1]), Path(sys.argv[2])
sys.path.insert(0, str(base / "tools"))
import build
build.APP, build.ROOT = root / "firmware", root
build.CONF = build.read_conf(build.APP / "fm1.toml")
errors = build.mmio_check()
for e in errors:
    print(e)
sys.exit(1 if errors else 0)
PY
echo "device check: ok (no libm, no C++ library, no double; fm1-base's rules for the app's C)"
