// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "simd_dispatch_guard.h"

#include <stdio.h>

static int require_state(const char *name, uint32_t cpuidEcx, uint64_t xcr0, int expected) {
	const int actual = mumble_rnnoise_avx_os_state_enabled(cpuidEcx, xcr0);
	if (actual == expected) {
		return 1;
	}
	fprintf(stderr, "%s: expected %d, got %d\n", name, expected, actual);
	return 0;
}

int main(void) {
	const uint32_t avxAndOsxsave = MUMBLE_RNNOISE_CPUID_AVX | MUMBLE_RNNOISE_CPUID_OSXSAVE;
	int ok = 1;
	ok &= require_state("missing OSXSAVE", MUMBLE_RNNOISE_CPUID_AVX, MUMBLE_RNNOISE_XCR0_SSE_YMM, 0);
	ok &= require_state("missing AVX", MUMBLE_RNNOISE_CPUID_OSXSAVE, MUMBLE_RNNOISE_XCR0_SSE_YMM, 0);
	ok &= require_state("missing YMM state", avxAndOsxsave, UINT64_C(0x2), 0);
	ok &= require_state("missing SSE state", avxAndOsxsave, UINT64_C(0x4), 0);
	ok &= require_state("SSE and YMM enabled", avxAndOsxsave, MUMBLE_RNNOISE_XCR0_SSE_YMM, 1);
	return ok ? 0 : 1;
}
