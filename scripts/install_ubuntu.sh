#!/usr/bin/env bash
set -euo pipefail
sudo apt update
sudo apt install -y build-essential cmake pkg-config libssl-dev curl
if ! id skud >/dev/null 2>&1; then sudo useradd --system --home /opt/Monk_SKUD_UNEX --shell /usr/sbin/nologin skud; fi
sudo usermod -aG dialout skud
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
sudo chown -R skud:skud /opt/Monk_SKUD_UNEX 2>/dev/null || true
sudo cp systemd/monk-skud-unex.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now monk-skud-unex
