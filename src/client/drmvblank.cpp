#include "drmvblank.h"
#include "../common/net.h"   // timeNow

#if defined(__linux__)
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
int drmIoctl(int fd, unsigned long req, void* arg) {
    int r;
    do r = ioctl(fd, req, arg); while (r == -1 && (errno == EINTR || errno == EAGAIN));
    return r;
}
struct Crtc { int pipe; drm_mode_modeinfo mode; int x, y; };
// The card's CRTCs that show something (their index in the card's list is the pipe the vblank query takes).
std::vector<Crtc> activeCrtcs(int fd) {
    std::vector<Crtc> out;
    drm_mode_card_res res{};
    if (drmIoctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0 || res.count_crtcs == 0) return out;
    std::vector<uint32_t> ids(res.count_crtcs);
    drm_mode_card_res res2{};
    res2.count_crtcs = res.count_crtcs;
    res2.crtc_id_ptr = (uint64_t)(uintptr_t)ids.data();
    if (drmIoctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res2) != 0) return out;
    ids.resize(std::min(res2.count_crtcs, res.count_crtcs));
    for (size_t i = 0; i < ids.size(); i++) {
        drm_mode_crtc c{};
        c.crtc_id = ids[i];
        if (drmIoctl(fd, DRM_IOCTL_MODE_GETCRTC, &c) != 0 || !c.mode_valid || !c.mode.clock) continue;
        if (c.mode.flags & (DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLSCAN)) continue;
        out.push_back({(int)i, c.mode, (int)c.x, (int)c.y});
    }
    return out;
}
double monoNow() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
}

bool DrmVblank::open(int w, int h, int hz, int x, int y) {
    close();
    if (getenv("QUARK_NO_DRM_VBLANK")) return false;
    int bestFd = -1, bestScore = -1;
    Crtc best{};
    for (int n = 0; n < 16; n++) {
        char path[32];
        snprintf(path, sizeof path, "/dev/dri/card%d", n);
        int fd = ::open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) fd = ::open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        drm_get_cap cap{DRM_CAP_TIMESTAMP_MONOTONIC, 0};
        bool used = false;
        if (drmIoctl(fd, DRM_IOCTL_GET_CAP, &cap) == 0 && cap.value) {
            for (const Crtc& c : activeCrtcs(fd)) {
                // (the mode the monitor shows: the size must match; the refresh GLFW gives is rounded; the place
                // only tells equal monitors apart on X11, where it's the CRTC's)
                const double exact = c.mode.clock * 1000.0 / ((double)c.mode.htotal * c.mode.vtotal);
                int score = 0;
                if (c.mode.hdisplay == w && c.mode.vdisplay == h) score += 4;
                if (hz > 0 && exact > hz - 1.5 && exact < hz + 1.5) score += 2;
                if (c.x == x && c.y == y) score += 1;
                if (score < 6 && !(score >= 4 && hz <= 0)) continue;
                if (score > bestScore) {
                    if (bestFd >= 0 && bestFd != fd) ::close(bestFd);
                    bestFd = fd; bestScore = score; best = c; used = true;
                }
            }
        }
        if (!used && fd != bestFd) ::close(fd);
    }
    if (bestFd < 0) return false;
    fd_ = bestFd;
    pipe_ = best.pipe;
    const double line = best.mode.htotal / (best.mode.clock * 1000.0);
    refresh_ = line * best.mode.vtotal;
    blankLen_ = line * (best.mode.vtotal - best.mode.vdisplay);
    double at, r;
    if (!nextBlank(&at, &r)) { close(); return false; }   // (the query refused: no vblank on this CRTC)
    return true;
}

void DrmVblank::close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
}

bool DrmVblank::nextBlank(double* at, double* refresh) {
    if (fd_ < 0) return false;
    drm_wait_vblank v{};
    unsigned type = _DRM_VBLANK_RELATIVE;
    if (pipe_ == 1) type |= _DRM_VBLANK_SECONDARY;
    else if (pipe_ > 1) type |= ((unsigned)pipe_ << _DRM_VBLANK_HIGH_CRTC_SHIFT) & _DRM_VBLANK_HIGH_CRTC_MASK;
    v.request.type = (drm_vblank_seq_type)type;
    v.request.sequence = 0;   // (a query: the last vblank, now; nothing waits)
    if (drmIoctl(fd_, DRM_IOCTL_WAIT_VBLANK, &v) != 0) return false;
    const double now = monoNow(), tnow = timeNow();
    const double stamp = v.reply.tval_sec + v.reply.tval_usec * 1e-6;   // (the last refresh's first visible line)
    if (stamp <= 0 || stamp > now + 0.001 || now - stamp > 1.0) return false;   // (a stamp a second old: the CRTC is off)
    double start = stamp + refresh_ - blankLen_;   // (this refresh's blank)
    while (start <= now) start += refresh_;
    *at = tnow + (start - now);
    *refresh = refresh_;
    return true;
}

#else
bool DrmVblank::open(int, int, int, int, int) { return false; }
void DrmVblank::close() {}
bool DrmVblank::nextBlank(double*, double*) { return false; }
#endif
