// Watching the HUD's files for changes off the frame (hud.cfg or the mode's HUD profile, theme.cfg).
#pragma once
#include <string>

// A file's change key (its write time and size; -2: no file) as the shared watcher thread last saw it: each file asked
// for (up to 4) is looked at once a second on that thread. The first ask for a file looks at it at once.
long long watchedFileKey(const std::string& path);
// After our own save: the key the file has now (so the save isn't read back as a change).
long long fileWritten(const std::string& path);
