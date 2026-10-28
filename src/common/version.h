#pragma once
// The game version (the VERSION file). It lives in one generated file (build/generated/version.cpp), so a version
// bump recompiles that file only. Not a string literal: write std::string("v") + QUARK_VERSION, not "v" QUARK_VERSION.
extern const char kQuarkVersion[];          // e.g. "1.19.1", or "1.19.1.3" for a testing build (1.19.0 to 1.19.1: "1.19.0.testing.3", before: "1.18.9-test.3")
extern const char kQuarkLauncherMarker[];   // "QUARK-LAUNCHER-VERSION:" + the version (src/launcher/selfupdate.cpp)
#define QUARK_VERSION kQuarkVersion
