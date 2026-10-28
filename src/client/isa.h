// Picking the game client built for this CPU. Packages ship quark (x86-64-v2: SSE4.2) and quark_avx2 (x86-64-v3:
// AVX2, BMI2, FMA as an instruction set) side by side; quark runs first and, when the CPU has AVX2 and quark_avx2 sits
// next to it, hands over to it before doing anything else. Both builds simulate bit for bit the same (no fast-math, no
// FMA contraction: CMakeLists.txt), so a lobby can mix them. --no-avx2 or QUARK_NO_AVX2=1 stays on the SSE4.2 build.
#pragma once
namespace isa {

// What this executable was built for (the benchmark's setup shows it).
inline const char* buildName() {
#if defined(QUARK_ISA_AVX2)
    return "AVX2";
#elif defined(__SSE4_2__)
    return "SSE4.2";
#else
    return "DEFAULT";
#endif
}

// Runs quark_avx2 with the same arguments when it should: returns its exit code (Windows: this process waits, so
// whatever watches quark.exe, the launcher too, sees the game run until it closes), or -1 to carry on here.
// Linux replaces this process (execv), so it only returns when that fails. Always -1 in the AVX2 build itself.
int runAvx2Build(int argc, char** argv);

} // namespace isa
