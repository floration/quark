#include "gl.h"

#define QGL_DEFINE(ret, name, args) PFN_##name name = nullptr;
QGL_FUNCS(QGL_DEFINE)
#undef QGL_DEFINE

PFN_glBufferStorage glBufferStorageOpt = nullptr;
PFN_glGetStringi glGetStringiOpt = nullptr;
PFN_glGetProgramBinary glGetProgramBinaryOpt = nullptr;
PFN_glProgramBinary glProgramBinaryOpt = nullptr;
PFN_glProgramParameteri glProgramParameteriOpt = nullptr;
PFN_glMaxShaderCompilerThreads glMaxShaderCompilerThreadsKHROpt = nullptr, glMaxShaderCompilerThreadsARBOpt = nullptr;

const char* qglLoad(QGLGetProc getProc) {
    glBufferStorageOpt = (PFN_glBufferStorage)getProc("glBufferStorage");   // (optional: see gl.h)
    glGetStringiOpt = (PFN_glGetStringi)getProc("glGetStringi");
    glGetProgramBinaryOpt = (PFN_glGetProgramBinary)getProc("glGetProgramBinary");
    glProgramBinaryOpt = (PFN_glProgramBinary)getProc("glProgramBinary");
    glProgramParameteriOpt = (PFN_glProgramParameteri)getProc("glProgramParameteri");
    glMaxShaderCompilerThreadsKHROpt = (PFN_glMaxShaderCompilerThreads)getProc("glMaxShaderCompilerThreadsKHR");
    glMaxShaderCompilerThreadsARBOpt = (PFN_glMaxShaderCompilerThreads)getProc("glMaxShaderCompilerThreadsARB");
#define QGL_LOAD(ret, name, args) \
    name = (PFN_##name)getProc(#name); \
    if (!name) return #name;
    QGL_FUNCS(QGL_LOAD)
#undef QGL_LOAD
    return nullptr;
}
