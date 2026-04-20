#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JOBS="${JOBS:-4}"

cd "$ROOT_DIR"

echo "[build] configs/adaptive_selector_config.json -> bin/champsim_adaptive_selector"
rm -f _configuration.mk absolute.options
make configclean
./config.sh configs/adaptive_selector_config.json
make -j"${JOBS}" bin/champsim_adaptive_selector
