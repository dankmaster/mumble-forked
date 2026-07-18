/* Copyright (c) 2014, Cisco Systems, INC
   Written by XiangMingZhu WeiZhou MinPeng YanWang

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:

   - Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.
   - Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
   AND ANY EXPRESS OR IMPLIED WARRANTIES ARE DISCLAIMED. IN NO EVENT SHALL THE
   COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DAMAGES ARISING IN ANY WAY
   OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
   DAMAGE.
*/

// Reuse RNNoise's upstream CPUID selection under a private name. The wrapper
// below adds the OSXSAVE/XCR0 condition required before any YMM instruction can
// safely execute on Windows, older hypervisors, and other x86 hosts.
#define rnn_select_arch mumble_rnnoise_upstream_select_arch
#include "../rnnoise-src/src/x86/x86cpu.c"
#undef rnn_select_arch

#include "simd_dispatch_guard.h"

#if defined(_MSC_VER)
#	include <immintrin.h>
static uint64_t mumble_rnnoise_read_xcr0(void) {
	return (uint64_t) _xgetbv(0);
}
#elif defined(__GNUC__) || defined(__clang__)
static uint64_t mumble_rnnoise_read_xcr0(void) {
	uint32_t low;
	uint32_t high;
	__asm__ volatile("xgetbv" : "=a"(low), "=d"(high) : "c"(0));
	return ((uint64_t) high << 32) | low;
}
#else
#	error "RNNoise x86 runtime dispatch needs an XGETBV implementation for this compiler"
#endif

static int mumble_rnnoise_os_allows_avx(void) {
	unsigned int info[4];
	cpuid(info, 1);
	if ((info[2] & (MUMBLE_RNNOISE_CPUID_OSXSAVE | MUMBLE_RNNOISE_CPUID_AVX))
		!= (MUMBLE_RNNOISE_CPUID_OSXSAVE | MUMBLE_RNNOISE_CPUID_AVX)) {
		return 0;
	}
	return mumble_rnnoise_avx_os_state_enabled(info[2], mumble_rnnoise_read_xcr0());
}

int rnn_select_arch(void) {
	const int upstreamArch = mumble_rnnoise_upstream_select_arch();
	// 0 is the portable SSE2 baseline, 1 is SSE4.1, and 2 is AVX2.
	return upstreamArch >= 2 && !mumble_rnnoise_os_allows_avx() ? 1 : upstreamArch;
}
