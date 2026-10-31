#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
# The FM-1's constraints, checked with the computer's clang before a push (the JieLi toolchain only runs on
# GitHub): the C++ units built as the firmware builds them (no C++ library, no libc beyond felucca-bsp's memset /
# memcpy / memcmp, no libm, bench/shim on the include path) for a 32-bit core with a float-only FPU (RISC-V rv32imf,
# a stand-in for the AC79's pi32v2), then linked with nothing else: any double left in the code links soft-double
# routines (__adddf3, __extendsfdf2...) and any missing function shows as an undefined symbol.
#   VARIANT=1 tools/device_check.sh  (base/ is fm1-base: its include/ for plat.h and fastmath.h; VARIANT: scenes.cc)
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
  -nostdinc -nostdinc++ -isystem $ROOT/bench/shim -isystem $BASE/include -isystem $RES \
  -I$ROOT/bench -I$ROOT/bench/third_party -DTEST -DMI_ARENA_BYTES=1024 -DMI_VARIANT=${VARIANT:-0} \
  -std=gnu++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit"
fail=0
for f in "$ROOT"/bench/mi/*.cc $(find "$ROOT/bench/third_party" -name '*.cc' | sort); do
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
int sc_count(void); int sc_start(int); void sc_render(float *, float *, uint32_t);
uint32_t sc_size_plaits(void), sc_size_rings(void), sc_size_clouds(void), sc_size_warps(void), sc_size_tides(void), sc_size_marbles(void);
float out_l[128], out_r[128];
void _start(void)
{
    int i;
    for (i = 0; i < sc_count(); i++) {
        sc_start(i);
        sc_render(out_l, out_r, 128);
    }
    out_l[0] = (float)(sc_size_plaits() + sc_size_rings() + sc_size_clouds() + sc_size_warps() + sc_size_tides() + sc_size_marbles());
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
echo "device check: ok (no libm, no C++ library, no double)"
