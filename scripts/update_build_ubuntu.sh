#!/usr/bin/env bash
set -euo pipefail
git pull --ff-only origin main
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
sudo systemctl restart monk-skud-unex 2>/dev/null || true
