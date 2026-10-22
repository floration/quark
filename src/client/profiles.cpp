#include "profiles.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "../game/map.h"

namespace fs = std::filesystem;

std::string profileDir(const char* sub) {
    std::string d = findDataPath("maps") + "/../" + sub;
    std::error_code ec;
    fs::create_directories(d, ec);
    return d;
}

std::vector<std::string> listProfiles(const char* sub) {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(profileDir(sub), ec))
        if (e.is_regular_file() && e.path().extension() == ".cfg") out.push_back(e.path().filename().string());
    std::sort(out.begin(), out.end());
    return out;
}

std::string readTextFile(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool writeTextFile(const std::string& path, const std::string& text) { return writeFileAtomic(path, text); }

std::string stampedName(const char* prefix) {
    time_t t = time(nullptr);
    char b[64];
    strftime(b, sizeof b, "-%Y%m%d-%H%M%S.cfg", localtime(&t));
    return prefix + std::string(b);
}

// binds are keyed by action name (e.g. bind_alt_fire), so profiles survive new or reordered actions
static std::string actionKey(int a) {
    std::string k = actionName(a);
    for (char& c : k) c = c == ' ' ? '_' : (char)tolower((unsigned char)c);
    return k;
}

std::string controlsToText(const Binds& b, int socd, bool altToggle) {
    std::string out = "# quark controls profile\n";
    out += "socd=" + std::to_string(socd) + "\nalt_toggle=" + std::to_string(altToggle ? 1 : 0) + "\n";
    for (int a = 0; a < A_COUNT; a++) out += "bind_" + actionKey(a) + "=" + std::to_string(b.code[a][0]) + "," + std::to_string(b.code[a][1]) + "\n";
    return out;
}

bool controlsFromText(const std::string& text, Binds& b, int& socd, bool& altToggle) {
    std::istringstream f(text);
    std::string line;
    Binds nb = b;
    int ns = socd, binds = 0;
    bool na = altToggle;
    while (std::getline(f, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "socd") ns = std::clamp(atoi(v.c_str()), 0, 2);
        else if (k == "alt_toggle") na = atoi(v.c_str()) != 0;
        else if (k.rfind("bind_", 0) == 0) {
            std::string name = k.substr(5);
            int a = -1, c0 = 0, c1 = 0;
            for (int i = 0; i < A_COUNT; i++) if (actionKey(i) == name) a = i;
            if (a < 0 && !name.empty() && isdigit((unsigned char)name[0])) a = atoi(name.c_str()); // old numeric form
            if (a >= 0 && a < A_COUNT && sscanf(v.c_str(), "%d,%d", &c0, &c1) >= 1) { nb.code[a][0] = c0; nb.code[a][1] = c1; binds++; }
        }
    }
    if (!binds) return false;
    b = nb; socd = ns; altToggle = na;
    return true;
}
