// GPU zones for Tracy profiling builds (-DQUARK_TRACY=ON): each pass's GPU time from timestamp queries, read back a few
// frames later without stalling (TracyOpenGL). Compiled out otherwise, like QZONE (src/common/profile.h).
#pragma once
#ifdef QUARK_TRACY
#include "gl.h"
#include <tracy/TracyOpenGL.hpp>
#define QGPUCONTEXT() TracyGpuContext
#define QGPUZONE(name) TracyGpuZone(name)
#define QGPUCOLLECT() TracyGpuCollect
#else
#define QGPUCONTEXT()
#define QGPUZONE(name)
#define QGPUCOLLECT()
#endif
