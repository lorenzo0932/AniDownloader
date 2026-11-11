#!/bin/bash
# uninstall-mac.sh — rimuove il demone AniDownloader installato su macOS.
# Wrapper di install-mac.sh --uninstall: stessa logica, un solo posto.
#   ./uninstall-mac.sh            rimuove LaunchAgent e binario, conserva config/cache/log
#   ./uninstall-mac.sh --purge    rimuove anche config, cache e log
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
exec "$DIR/install-mac.sh" --uninstall "$@"
