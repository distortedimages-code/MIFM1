// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1: the little of a C++ runtime the vendored code reaches for. Built with -fno-exceptions -fno-rtti
// -fno-threadsafe-statics, and every object made in place, it needs only these: a pure virtual call (never made)
// and the deleting destructors' operator delete (never called: nothing is allocated).
#include <stddef.h>

extern "C" void __cxa_pure_virtual() {
  for (;;) {
  }
}

void operator delete(void*) noexcept { }
void operator delete(void*, size_t) noexcept { }
