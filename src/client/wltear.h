#pragma once
// Wayland tearing (Linux, GLFW on Wayland): wp_tearing_control_v1's presentation hint on the game's surface. A
// Wayland compositor shows every frame in a vertical blank unless the program asks it not to: ASYNC lets a
// fullscreen frame flip at once (NO VSYNC tears, LATENT SYNC flips on its own clock), VSYNC asks for the blank again.
// The compositor decides: KDE Plasma 6 needs "Allow tearing in full screen applications", Hyprland its immediate
// rule, Sway allow_tearing; GNOME never tears. libwayland-client is loaded at run time (GLFW 3.4 loads it too).
struct GLFWwindow;

namespace wltear {
// async: tear (true) or wait for the blank (false). False back: not on Wayland, or the compositor has no
// tearing control.
bool set(GLFWwindow* w, bool async);
void release();   // before the window goes
}
