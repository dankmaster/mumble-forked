// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "vec.h"

#if defined(MUMBLE_RNNOISE_REQUIRE_X86_SIMD) || defined(_M_X64) || defined(__x86_64__)
#	if defined(NO_OPTIMIZATIONS)
#		error "RNNoise x64 was configured with scalar kernels; refusing to build"
#	endif
#	if !defined(__SSE2__)
#		error "RNNoise x64 must retain its portable SSE2 baseline"
#	endif
#	if !defined(RNN_ENABLE_X86_RTCD)
#		error "RNNoise x64 must include runtime SIMD dispatch"
#	endif
#endif

// Keep a real translation unit so the checks above are evaluated by every
// target that embeds the RNNoise core sources.
const int mumble_rnnoise_simd_build_check = 1;
