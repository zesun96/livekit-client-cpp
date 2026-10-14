#pragma once

/* Use the canonical header so the Swift import always matches the SDK ABI. */
#if defined(_WIN32) && !defined(LKC_SHARED)
#define LKC_SHARED
#endif
#include "../../../../include/livekit/capi/livekit.h"
