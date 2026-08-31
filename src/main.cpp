#include "skud/App.h"
#include <csignal>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

static skud::App* g_app = nullptr;
static void sig(int) { if (g_app) g_app->stop(); }

namespace {
namespace fs = std::filesystem;

bool hasWebUi(const fs::path& root) {
    std::error_code ec;
    return fs::is_regular_file(root / "web" / "login.html", ec) &&
           fs::is_regular_file(root / "web" / "index.html", ec);
}

bool looksLikeSourceRoot(const fs::path& root) {
    std::error_code ec;
    return hasWebUi(root) && fs::is_regular_file(root / "CMakeLists.txt", ec);
}

fs::path normalized(const fs::path& p) {
    std::error_code ec;
    auto c = fs::weakly_canonical(p, ec);
    if (!ec) return c;
    auto a = fs::absolute(p, ec);
    return ec ? p : a;
}

std::string resolveProjectRoot(int argc, char** argv) {
    std::vector<fs::path> candidates;

    // An explicit root has the highest priority, but an invalid path no longer
    // leaves the web server silently serving 404 for all static files.
    if (argc > 1 && argv[1] && *argv[1]) candidates.emplace_back(argv[1]);

    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (!ec) {
        candidates.push_back(cwd);
        candidates.push_back(cwd.parent_path());
    }

    if (argc > 0 && argv[0] && *argv[0]) {
        fs::path exe = normalized(argv[0]);
        fs::path exeDir = exe.parent_path();
        candidates.push_back(exeDir);                  // build/web copied by CMake
        candidates.push_back(exeDir.parent_path());   // source root when binary is build/monk-skud-unex
    }

    candidates.emplace_back("/opt/Monk_SKUD_UNEX");

    // Prefer the real source/project root over build/web copied by CMake,
    // otherwise config/data would accidentally be created under build/.
    for (const auto& c : candidates) {
        if (c.empty()) continue;
        auto n = normalized(c);
        if (looksLikeSourceRoot(n)) return n.string();
    }

    // Packaged layouts may only have a web/ directory next to the executable.
    for (const auto& c : candidates) {
        if (c.empty()) continue;
        auto n = normalized(c);
        if (hasWebUi(n)) return n.string();
    }

    // Preserve the old behavior as a last resort, but emit a useful diagnostic.
    fs::path fallback = (argc > 1 && argv[1] && *argv[1]) ? fs::path(argv[1]) : cwd;
    fallback = normalized(fallback);
    std::cerr << "WARNING: web/login.html not found. Tried project root candidates; using: "
              << fallback << "\n";
    std::cerr << "Expected UI file: " << (fallback / "web" / "login.html") << "\n";
    return fallback.string();
}
}

int main(int argc, char** argv) {
    const std::string root = resolveProjectRoot(argc, argv);
    std::cout << "Monk_SKUD_UNEX root: " << root << "\n";

    skud::App app(root);
    g_app = &app;
    std::signal(SIGINT, sig);
    std::signal(SIGTERM, sig);
    if (!app.init()) return 1;
    return app.run() ? 0 : 2;
}
