#pragma once
#include <string>

namespace skud {
// Creates the complete writable runtime layout under root.
// Existing files are never overwritten.
bool ensureRuntimeLayout(const std::string& root, std::string& error);
}
