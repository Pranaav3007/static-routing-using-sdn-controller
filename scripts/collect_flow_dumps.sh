#!/usr/bin/env bash
set -euo pipefail

OUTPUT_DIR="screenshots/static-routing/logs"
mkdir -p "${OUTPUT_DIR}"

for sw in s1 s2 s3 s4; do
  echo "Saving flow table for ${sw}"
  sudo ovs-ofctl -O OpenFlow13 dump-flows "${sw}" | tee "${OUTPUT_DIR}/${sw}-flows.txt"
done
