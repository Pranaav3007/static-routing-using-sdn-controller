#!/usr/bin/env bash
set -euo pipefail

if ! command -v ryu-manager >/dev/null 2>&1; then
  echo "ryu-manager not found. Install requirements first." >&2
  exit 1
fi

if ! command -v mn >/dev/null 2>&1; then
  echo "Mininet not found. Run this script on Ubuntu with Mininet installed." >&2
  exit 1
fi

mkdir -p screenshots/static-routing/logs

echo "[1/3] Clearing stale Mininet state"
sudo mn -c

echo "[2/3] Starting Ryu controller"
ryu-manager controller/static_routing_controller.py \
  | tee screenshots/static-routing/logs/ryu-manager.log &
CONTROLLER_PID=$!

cleanup() {
  kill "${CONTROLLER_PID}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

sleep 3

echo "[3/3] Launching Mininet CLI"
sudo mn --custom topology/static_routing_topology.py \
  --topo staticrouting \
  --controller remote,ip=127.0.0.1,port=6633 \
  --switch ovsk,protocols=OpenFlow13 \
  --mac
