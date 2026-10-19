// Optional Tracy profiler zones. Build with -DQUARK_TRACY=ON, run the game, and connect the Tracy
// profiler (or tracy-capture) to see per-frame CPU zones. Compiles to nothing otherwise.
#pragma once
#ifdef QUARK_TRACY
#include <tracy/Tracy.hpp>
#define QZONE(name) ZoneScopedN(name)
#define QFRAME() FrameMark
#define QPLOT(name, v) TracyPlot(name, (int64_t)(v))
#else
#define QZONE(name)
#define QFRAME()
#define QPLOT(name, v)
#endif
