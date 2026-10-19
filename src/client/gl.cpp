#include "gl.h"

#define QGL_DEFINE(ret, name, args) PFN_##name name = nullptr;
QGL_FUNCS(QGL_DEFINE)
#undef QGL_DEFINE

const char* qglLoad(QGLGetProc getProc) {
#define QGL_LOAD(ret, name, args) \
    name = (PFN_##name)getProc(#name); \
    if (!name) return #name;
    QGL_FUNCS(QGL_LOAD)
#undef QGL_LOAD
    return nullptr;
}
