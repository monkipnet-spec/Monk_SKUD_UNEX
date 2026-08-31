#include "skud/App.h"
#include <csignal>
#include <filesystem>
#include <iostream>
#include <string>

static skud::App* g_app = nullptr;
static void sig(int) { if (g_app) g_app->stop(); }

namespace {
namespace fs = std::filesystem;

fs::path normalized(const fs::path& p) {
    std::error_code ec;
    auto a = fs::absolute(p, ec);
    if (ec) return p;
    auto c = fs::weakly_canonical(a, ec);
    return ec ? a : c;
}

std::string resolveRuntimeRoot(int argc, char** argv) {
    // Runtime data belongs to the directory from which the application is
    // started. An explicit first argument remains available for systemd or
    // administrators who intentionally want another runtime directory.
    if (argc > 1 && argv[1] && *argv[1]) return normalized(argv[1]).string();

    std::error_code ec;
    auto cwd = fs::current_path(ec);
    if (ec) {
        std::cerr << "Cannot determine current working directory: " << ec.message() << "\n";
        return ".";
    }
    return normalized(cwd).string();
}
}

int main(int argc, char** argv) {
    const std::string root = resolveRuntimeRoot(argc, argv);
    std::cout << "Monk_SKUD_UNEX runtime root: " << root << "\n";

    skud::App app(root);
    g_app = &app;
    std::signal(SIGINT, sig);
    std::signal(SIGTERM, sig);
    if (!app.init()) return 1;
    return app.run() ? 0 : 2;
}
