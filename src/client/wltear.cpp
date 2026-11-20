#include "wltear.h"

#if defined(__linux__)
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <dlfcn.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
struct wl_display;
struct wl_surface;
wl_display* glfwGetWaylandDisplay(void);
wl_surface* glfwGetWaylandWindow(GLFWwindow* window);
}

namespace {
// libwayland-client's ABI (wayland-util.h / wayland-client-core.h), stable since 1.0; declared here so the build
// needs no Wayland headers and the game no Wayland library
struct wl_interface;
struct wl_message { const char* name; const char* signature; const wl_interface** types; };
struct wl_interface { const char* name; int version; int method_count; const wl_message* methods; int event_count; const wl_message* events; };
struct wl_proxy;
struct wl_event_queue;
struct RegistryListener {
    void (*global)(void* data, wl_proxy* registry, uint32_t name, const char* iface, uint32_t version);
    void (*globalRemove)(void* data, wl_proxy* registry, uint32_t name);
};

struct Wl {
    wl_proxy* (*marshalFlags)(wl_proxy*, uint32_t, const wl_interface*, uint32_t, uint32_t, ...) = nullptr;
    int (*addListener)(wl_proxy*, void (**)(void), void*) = nullptr;
    void (*destroy)(wl_proxy*) = nullptr;
    uint32_t (*version)(wl_proxy*) = nullptr;
    void* (*createWrapper)(void*) = nullptr;
    void (*wrapperDestroy)(void*) = nullptr;
    void (*setQueue)(wl_proxy*, wl_event_queue*) = nullptr;
    wl_event_queue* (*createQueue)(wl_display*) = nullptr;
    void (*queueDestroy)(wl_event_queue*) = nullptr;
    int (*roundtripQueue)(wl_display*, wl_event_queue*) = nullptr;
    int (*flush)(wl_display*) = nullptr;
    const wl_interface* registryIface = nullptr;
    const wl_interface* surfaceIface = nullptr;
    bool ok = false;
};
const Wl& wl() {
    static Wl w = [] {
        Wl w;
        void* lib = dlopen("libwayland-client.so.0", RTLD_LAZY | RTLD_LOCAL);
        if (!lib) return w;
        w.marshalFlags = (decltype(w.marshalFlags))dlsym(lib, "wl_proxy_marshal_flags");   // (libwayland 1.20+)
        w.addListener = (decltype(w.addListener))dlsym(lib, "wl_proxy_add_listener");
        w.destroy = (decltype(w.destroy))dlsym(lib, "wl_proxy_destroy");
        w.version = (decltype(w.version))dlsym(lib, "wl_proxy_get_version");
        w.createWrapper = (decltype(w.createWrapper))dlsym(lib, "wl_proxy_create_wrapper");
        w.wrapperDestroy = (decltype(w.wrapperDestroy))dlsym(lib, "wl_proxy_wrapper_destroy");
        w.setQueue = (decltype(w.setQueue))dlsym(lib, "wl_proxy_set_queue");
        w.createQueue = (decltype(w.createQueue))dlsym(lib, "wl_display_create_queue");
        w.queueDestroy = (decltype(w.queueDestroy))dlsym(lib, "wl_event_queue_destroy");
        w.roundtripQueue = (decltype(w.roundtripQueue))dlsym(lib, "wl_display_roundtrip_queue");
        w.flush = (decltype(w.flush))dlsym(lib, "wl_display_flush");
        w.registryIface = (const wl_interface*)dlsym(lib, "wl_registry_interface");
        w.surfaceIface = (const wl_interface*)dlsym(lib, "wl_surface_interface");
        w.ok = w.marshalFlags && w.addListener && w.destroy && w.version && w.createWrapper && w.wrapperDestroy &&
               w.setQueue && w.createQueue && w.queueDestroy && w.roundtripQueue && w.flush && w.registryIface &&
               w.surfaceIface;
        return w;
    }();
    return w;
}

// staging/tearing-control/tearing-control-v1.xml (wayland-protocols 1.30), version 1
extern const wl_interface kControlIface;
const wl_interface* kManagerTypes[] = {&kControlIface, nullptr};   // (filled: wl_surface, at run time)
const wl_message kManagerReqs[] = {{"destroy", "", nullptr}, {"get_tearing_control", "no", kManagerTypes}};
const wl_interface kManagerIface = {"wp_tearing_control_manager_v1", 1, 2, kManagerReqs, 0, nullptr};
const wl_interface* kControlTypes[] = {nullptr};
const wl_message kControlReqs[] = {{"set_presentation_hint", "u", kControlTypes}, {"destroy", "", nullptr}};
const wl_interface kControlIface = {"wp_tearing_control_v1", 1, 2, kControlReqs, 0, nullptr};
constexpr uint32_t kMarshalDestroy = 1;   // (WL_MARSHAL_FLAG_DESTROY)

struct State {
    GLFWwindow* win = nullptr;
    wl_proxy* manager = nullptr;
    wl_proxy* control = nullptr;
    bool tried = false;
} g;

void onGlobal(void* data, wl_proxy* registry, uint32_t name, const char* iface, uint32_t) {
    if (strcmp(iface, kManagerIface.name) != 0) return;
    *(wl_proxy**)data = wl().marshalFlags(registry, 0, &kManagerIface, 1, 0, name, kManagerIface.name, 1u, nullptr);
}
void onGlobalRemove(void*, wl_proxy*, uint32_t) {}
const RegistryListener kRegistryListener = {onGlobal, onGlobalRemove};

bool bind(GLFWwindow* w) {
    const Wl& l = wl();
    wl_display* d = glfwGetWaylandDisplay();
    wl_surface* s = glfwGetWaylandWindow(w);
    if (!l.ok || !d || !s) return false;
    kManagerTypes[1] = l.surfaceIface;
    // (on a queue of its own, so GLFW's events aren't dispatched here)
    wl_event_queue* q = l.createQueue(d);
    void* disp = l.createWrapper(d);
    if (!q || !disp) { if (disp) l.wrapperDestroy(disp); if (q) l.queueDestroy(q); return false; }
    l.setQueue((wl_proxy*)disp, q);
    wl_proxy* reg = l.marshalFlags((wl_proxy*)disp, 1, l.registryIface, l.version((wl_proxy*)disp), 0, nullptr);
    wl_proxy* manager = nullptr;
    if (reg) {
        l.addListener(reg, (void (**)(void))&kRegistryListener, &manager);
        l.roundtripQueue(d, q);
        l.destroy(reg);
    }
    if (manager) l.setQueue(manager, nullptr);   // (back on the default queue: it has no events)
    l.wrapperDestroy(disp);
    l.queueDestroy(q);
    if (!manager) return false;
    g.manager = manager;
    g.control = l.marshalFlags(manager, 1, &kControlIface, 1, 0, nullptr, (wl_proxy*)s);
    return g.control != nullptr;
}
}

namespace wltear {
bool set(GLFWwindow* w, bool async) {
    if (glfwGetPlatform() != GLFW_PLATFORM_WAYLAND || !w) return false;
    if (g.win != w) { release(); g.win = w; }
    if (!g.control) {
        if (g.tried) return false;
        g.tried = true;
        const bool ok = bind(w);
        fprintf(stderr, "wayland: %s\n", ok ? "tearing control on" : "no tearing control from the compositor");
        if (!ok) return false;
    }
    wl().marshalFlags(g.control, 0, nullptr, 1, 0, async ? 1u : 0u);   // (0 vsync, 1 async: applies at the next commit)
    wl().flush(glfwGetWaylandDisplay());
    return true;
}

void release() {
    if (g.control) wl().marshalFlags(g.control, 1, nullptr, 1, kMarshalDestroy);
    if (g.manager) wl().marshalFlags(g.manager, 0, nullptr, 1, kMarshalDestroy);
    g = State{};
}
}

#else
namespace wltear {
bool set(GLFWwindow*, bool) { return false; }
void release() {}
}
#endif
