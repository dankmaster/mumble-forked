// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_3RDPARTY_RNNOISE_BUILD_SIMD_DISPATCH_GUARD_H_
#define MUMBLE_3RDPARTY_RNNOISE_BUILD_SIMD_DISPATCH_GUARD_H_

#include <stdint.h>

#define MUMBLE_RNNOISE_CPUID_OSXSAVE (UINT32_C(1) << 27)
#define MUMBLE_RNNOISE_CPUID_AVX (UINT32_C(1) << 28)
#define MUMBLE_RNNOISE_XCR0_SSE_YMM UINT64_C(0x6)

static int mumble_rnnoise_avx_os_state_enabled(uint32_t cpuid_ecx, uint64_t xcr0) {
	const uint32_t requiredCpuid = MUMBLE_RNNOISE_CPUID_OSXSAVE | MUMBLE_RNNOISE_CPUID_AVX;
	return (cpuid_ecx & requiredCpuid) == requiredCpuid
		   && (xcr0 & MUMBLE_RNNOISE_XCR0_SSE_YMM) == MUMBLE_RNNOISE_XCR0_SSE_YMM;
}

#endif
