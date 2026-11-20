#pragma once
// DrmVblank (Linux, PRESENT: LATENT SYNC): when the monitor's last vertical blank happened, from the kernel's display
// driver (DRM: /dev/dri/card*, the vblank query any program on the seat may make; no master, no libdrm). Works the
// same on X11, Wayland and XWayland, and on AMD, Intel, nouveau and NVIDIA's own driver (nvidia-drm, modeset on).
// The kernel stamps each vblank as the time the first visible line starts (CLOCK_MONOTONIC, from the scanout
// position); the CRTC's mode gives the exact line timing, so the blank's start is that stamp minus its lines.

class DrmVblank {
public:
    ~DrmVblank() { close(); }
    // The CRTC showing a w x h mode at about hz (x, y: the monitor's place on the desktop, picks among equal
    // monitors on X11). False: no card answers (no access to /dev/dri, no such mode, not Linux).
    bool open(int w, int h, int hz, int x, int y);
    void close();
    bool ok() const { return fd_ >= 0; }
    // When the next vertical blank begins, as timeNow() (refresh: the exact period, seconds, from the mode).
    bool nextBlank(double* at, double* refresh);

private:
    int fd_ = -1, pipe_ = 0;
    double refresh_ = 0, blankLen_ = 0;   // (seconds: a whole refresh; the blank's lines)
};
