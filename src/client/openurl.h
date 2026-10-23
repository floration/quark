// Opens a web page in the default browser (only fixed https URLs from the game itself are passed here).
#pragma once
#include <string>
void openUrl(const std::string& url);
// The UI test (quark --uitest) takes the URL / folder instead of opening it. Null in normal play.
extern void (*g_openUrlHook)(const std::string& url);
