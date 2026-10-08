// omnibox-launchpad: a tiny CLI launcher that detects installed apps
// and lets you fuzzy-search + launch them from one prompt.
// Build: see Makefile (g++ -std=c++17). Target: Windows 10/11.

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

struct App {
    std::string name;   // display name (file name without extension)
    std::string path;   // full path to the .lnk or .exe
};

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

static std::string env_or_empty(const char* key) {
    const char* v = std::getenv(key);
    return v ? std::string(v) : std::string();
}

// Folders where Windows keeps Start Menu shortcuts + common install dirs.
static std::vector<fs::path> scan_roots() {
    std::vector<fs::path> roots;
    std::string appdata = env_or_empty("APPDATA");
    std::string programdata = env_or_empty("PROGRAMDATA");
    std::string localapp = env_or_empty("LOCALAPPDATA");

    if (!programdata.empty())
        roots.emplace_back(fs::path(programdata) / "Microsoft/Windows/Start Menu/Programs");
    if (!appdata.empty())
        roots.emplace_back(fs::path(appdata) / "Microsoft/Windows/Start Menu/Programs");
    if (!localapp.empty())
        roots.emplace_back(fs::path(localapp) / "Programs");
    return roots;
}

static std::vector<App> detect_apps() {
    std::map<std::string, App> unique;  // dedupe by lowercase name
    for (const auto& root : scan_roots()) {
        std::error_code ec;
        if (!fs::exists(root, ec)) continue;
        for (fs::recursive_directory_iterator it(
                 root, fs::directory_options::skip_permission_denied, ec),
             end;
             it != end; it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            if (!it->is_regular_file(ec)) continue;
            std::string ext = to_lower(it->path().extension().string());
            if (ext != ".lnk" && ext != ".exe") continue;

            std::string name = it->path().stem().string();
            std::string low = to_lower(name);
            // skip uninstallers / noise
            if (low.find("uninstall") != std::string::npos) continue;
            if (low.find("unins") == 0) continue;

            if (!unique.count(low))
                unique[low] = App{name, it->path().string()};
        }
    }
    std::vector<App> apps;
    apps.reserve(unique.size());
    for (auto& kv : unique) apps.push_back(std::move(kv.second));
    std::sort(apps.begin(), apps.end(), [](const App& a, const App& b) {
        return to_lower(a.name) < to_lower(b.name);
    });
    return apps;
}

// Simple scoring: prefix > substring > subsequence (fuzzy). 0 = no match.
static int score(const std::string& name, const std::string& query) {
    std::string n = to_lower(name), q = to_lower(query);
    if (q.empty()) return 1;
    if (n.rfind(q, 0) == 0) return 300 - (int)n.size();
    if (n.find(q) != std::string::npos) return 200 - (int)n.size();
    size_t qi = 0;
    for (char c : n)
        if (qi < q.size() && c == q[qi]) ++qi;
    return qi == q.size() ? 100 - (int)n.size() : 0;
}

static std::vector<const App*> search(const std::vector<App>& apps,
                                      const std::string& query,
                                      size_t limit = 8) {
    std::vector<std::pair<int, const App*>> hits;
    for (const auto& a : apps) {
        int s = score(a.name, query);
        if (s > 0) hits.emplace_back(s, &a);
    }
    std::sort(hits.begin(), hits.end(),
              [](auto& a, auto& b) { return a.first > b.first; });
    std::vector<const App*> out;
    for (size_t i = 0; i < hits.size() && i < limit; ++i)
        out.push_back(hits[i].second);
    return out;
}

static void launch(const App& app) {
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", app.path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    std::string cmd = "xdg-open \"" + app.path + "\" >/dev/null 2>&1 &";
    std::system(cmd.c_str());
#endif
}

int main() {
    std::cout << "[omnibox] scanning installed apps...\n";
    auto apps = detect_apps();
    std::cout << "[omnibox] " << apps.size() << " apps detected.\n";
    std::cout << "Type a name to search, a number to launch, ':list' to list all, ':q' to quit.\n";

    std::vector<const App*> last;
    std::string line;
    while (true) {
        std::cout << "\n> ";
        if (!std::getline(std::cin, line)) break;
        if (line == ":q" || line == ":quit") break;

        if (line == ":list") {
            for (const auto& a : apps) std::cout << "  " << a.name << "\n";
            continue;
        }
        if (line == ":rescan") {
            apps = detect_apps();
            std::cout << apps.size() << " apps detected.\n";
            continue;
        }

        // numeric choice from the last result list
        if (!line.empty() && std::all_of(line.begin(), line.end(), ::isdigit)) {
            size_t idx = std::stoul(line);
            if (idx >= 1 && idx <= last.size()) {
                std::cout << "Launching " << last[idx - 1]->name << "...\n";
                launch(*last[idx - 1]);
            } else {
                std::cout << "No result with that number.\n";
            }
            continue;
        }

        last = search(apps, line);
        if (last.empty()) {
            std::cout << "No match.\n";
            continue;
        }
        for (size_t i = 0; i < last.size(); ++i)
            std::cout << "  [" << (i + 1) << "] " << last[i]->name << "\n";
        // single strong hit -> convenience: Enter on empty line launches #1 (next prompt)
    }
    std::cout << "bye.\n";
    return 0;
}
