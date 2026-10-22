// Console tab completion (header-only so quark_tests can check it headless).
#pragma once
#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <vector>

struct Completion {
    std::string line;                 // the input after completing
    std::vector<std::string> options; // every match when there was more than one (to list), else empty
};

// Completes the last word of `line`. candidates(words) gives every word that may follow `words` (the
// complete words before the one being typed). One match: it's filled in plus a space. Several: the common
// prefix is filled in and all of them are returned to list. Matching ignores case.
inline Completion completeLine(const std::string& line,
                               const std::function<std::vector<std::string>(const std::vector<std::string>&)>& candidates) {
    std::vector<std::string> words;
    size_t start = 0;
    for (size_t i = 0; i <= line.size(); i++)
        if (i == line.size() || line[i] == ' ') { if (i > start) words.push_back(line.substr(start, i - start)); start = i + 1; }
    std::string prefix;
    size_t headLen = line.size();
    if (!line.empty() && line.back() != ' ' && !words.empty()) { prefix = words.back(); words.pop_back(); headLen = line.size() - prefix.size(); }
    auto lower = [](std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; };
    std::string lp = lower(prefix);
    std::vector<std::string> hits;
    for (auto& c : candidates(words))
        if (lower(c).rfind(lp, 0) == 0 && std::find(hits.begin(), hits.end(), c) == hits.end()) hits.push_back(c);
    Completion out{line, {}};
    if (hits.empty()) return out;
    std::string head = line.substr(0, headLen);
    if (hits.size() == 1) { out.line = head + hits[0] + " "; return out; }
    std::sort(hits.begin(), hits.end());
    std::string common = lower(hits[0]);
    for (auto& h : hits) { std::string l = lower(h); size_t n = 0; while (n < common.size() && n < l.size() && common[n] == l[n]) n++; common.resize(n); }
    out.line = head + (common.size() > prefix.size() ? hits[0].substr(0, common.size()) : prefix);
    out.options = hits;
    return out;
}
