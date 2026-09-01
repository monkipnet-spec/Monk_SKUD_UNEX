#include "skud/RuntimeBootstrap.h"
#include "skud/EmbeddedWeb.h"
#include <filesystem>
#include <fstream>
#include <string_view>

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

bool writeIfMissing(const fs::path& path, const char* data, std::size_t size, std::string& error) {
    std::error_code ec;
    if (fs::exists(path, ec)) {
        if (ec) {
            error = "cannot inspect " + path.string() + ": " + ec.message();
            return false;
        }
        return true;
    }
    if (!makeDir(path.parent_path(), error)) return false;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        error = "cannot create file " + path.string();
        return false;
    }
    f.write(data, static_cast<std::streamsize>(size));
    if (!f) {
        error = "cannot write file " + path.string();
        return false;
    }
    return true;
}

bool writeTextIfMissing(const fs::path& path, std::string_view text, std::string& error) {
    return writeIfMissing(path, text.data(), text.size(), error);
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

    // Runtime tables. These are created with headers only and are never
    // overwritten, so real subscriber/controller data is safe on restart.
    if (!writeTextIfMissing(base / "config" / "users.csv",
            "id;enabled;last_name;first_name;middle_name;department;position;card;card_series;card_number;pin_code;access_mode;controller_port;valid_from;valid_until;telegram_arrival;telegram_departure;cards\n", error)) return false;
    if (!writeTextIfMissing(base / "config" / "departments.csv", "name\n", error)) return false;
    if (!writeTextIfMissing(base / "config" / "controllers.csv", "node;name;model;enabled\n", error)) return false;
    if (!writeTextIfMissing(base / "data" / "card_state.csv", "state_key;state;last_read\n", error)) return false;
    if (!writeTextIfMissing(base / "data" / "active_cards.csv", "card;user_id;user_name;department;last_read;last_event;controller_node\n", error)) return false;
    if (!writeTextIfMissing(base / "data" / "controller_cards.csv", "card;controller_node;controller_name;first_seen;last_seen;read_count;last_raw_hex\n", error)) return false;

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

    return true;
}
}
