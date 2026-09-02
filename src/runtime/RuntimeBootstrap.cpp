#include "skud/RuntimeBootstrap.h"
#include "skud/EmbeddedWeb.h"
#include <filesystem>
#include <fstream>

namespace skud {
namespace {
namespace fs = std::filesystem;

bool makeDir(const fs::path& path, std::string& error) {
    std::error_code ec;
    fs::create_directories(path, ec);
    if (ec) {
        error = "cannot create directory " + path.string() + ": " + ec.message();
        return false;
    }
    return true;
}

bool writeOrRefresh(const fs::path& path, const char* data, std::size_t size, std::string& error) {
    if (!makeDir(path.parent_path(), error)) return false;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        error = "cannot refresh file " + path.string();
        return false;
    }
    f.write(data, static_cast<std::streamsize>(size));
    if (!f) {
        error = "cannot refresh file " + path.string();
        return false;
    }
    return true;
}
}

bool ensureRuntimeLayout(const std::string& root, std::string& error) {
    const fs::path base(root);
    error.clear();

    for (const auto& dir : {
             base,
             base / "config",
             base / "data",
             base / "data" / "events",
             base / "data" / "reports",
             base / "backup",
             base / "web"}) {
        if (!makeDir(dir, error)) return false;
    }

    // Runtime CSV tables are intentionally NOT recreated here. In MariaDB
    // mode they are one-time migration sources and must stay removed after a
    // verified migration. In legacy CSV mode each manager creates its own file
    // on the first save.

    // The complete UI is embedded in the executable. This makes a copied
    // binary self-contained: an empty working directory becomes runnable on
    // first start without separately copying web/. Web assets are refreshed on
    // every start so an upgraded binary cannot keep an old cached/runtime UI.
    if (!writeOrRefresh(base / "web" / "login.html",
            reinterpret_cast<const char*>(embedded::login_html), embedded::login_html_size, error)) return false;
    if (!writeOrRefresh(base / "web" / "index.html",
            reinterpret_cast<const char*>(embedded::index_html), embedded::index_html_size, error)) return false;
    if (!writeOrRefresh(base / "web" / "app.js",
            reinterpret_cast<const char*>(embedded::app_js), embedded::app_js_size, error)) return false;
    if (!writeOrRefresh(base / "web" / "style.css",
            reinterpret_cast<const char*>(embedded::style_css), embedded::style_css_size, error)) return false;
    if (!writeOrRefresh(base / "web" / "monitor.html",
            reinterpret_cast<const char*>(embedded::monitor_html), embedded::monitor_html_size, error)) return false;
    if (!writeOrRefresh(base / "web" / "monitor.js",
            reinterpret_cast<const char*>(embedded::monitor_js), embedded::monitor_js_size, error)) return false;
    if (!writeOrRefresh(base / "web" / "monitor.css",
            reinterpret_cast<const char*>(embedded::monitor_css), embedded::monitor_css_size, error)) return false;

    return true;
}
}
