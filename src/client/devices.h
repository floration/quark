// Settings > VIDEO + AUDIO > DEVICES: the device lists and how a saved choice is found in them. No OS calls here (the
// audio backends list sound devices, Game lists monitors through GLFW): the matching and naming rules, kept apart so
// quark_tests checks them without hardware.
#pragma once
#include <string>
#include <vector>

// One device as the page lists it. id: what quark.cfg stores (stable across restarts: a Windows endpoint id, an ALSA
// device name, a CoreAudio UID, a monitor's name); name: what the page shows.
struct DeviceEntry {
    std::string id, name;
};

// Where the saved id is in the list: its index; kDeviceDefault for "" (the system default / the primary monitor);
// kDeviceGone when it's saved but no longer there (unplugged, renamed: the default is used, with a notice).
constexpr int kDeviceDefault = -1, kDeviceGone = -2;
inline int findDevice(const std::vector<DeviceEntry>& list, const std::string& id) {
    if (id.empty()) return kDeviceDefault;
    for (size_t i = 0; i < list.size(); i++)
        if (list[i].id == id) return (int)i;
    return kDeviceGone;
}

// Names made unique in list order: the second "DELL P2419H" becomes "DELL P2419H #2" (two of the same monitor model
// report the same name; the id has to tell them apart, and so does the page).
inline std::vector<std::string> uniqueNames(const std::vector<std::string>& names) {
    std::vector<std::string> out;
    out.reserve(names.size());
    for (size_t i = 0; i < names.size(); i++) {
        int seen = 1;
        for (size_t k = 0; k < i; k++) seen += names[k] == names[i];
        out.push_back(seen > 1 ? names[i] + " #" + std::to_string(seen) : names[i]);
    }
    return out;
}

// A device id or name as a quark.cfg value: one line (a line break would end it early and start another key), at most
// 256 bytes (cut on a UTF-8 character boundary).
inline std::string deviceCfgValue(const std::string& v) {
    std::string o;
    for (char c : v) if (c != '\r' && c != '\n') o += c;
    if (o.size() > 256) {
        size_t n = 256;
        while (n > 0 && ((unsigned char)o[n] & 0xC0) == 0x80) n--;   // (not inside a character)
        o.resize(n);
    }
    return o;
}
